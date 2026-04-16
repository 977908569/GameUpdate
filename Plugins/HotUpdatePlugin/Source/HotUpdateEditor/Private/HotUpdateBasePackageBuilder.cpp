// Copyright czm. All Rights Reserved.

#include "HotUpdateBasePackageBuilder.h"
#include "HotUpdatePatchPackageBuilder.h"
#include "Core/HotUpdateFileUtils.h"
#include "HotUpdateEditor.h"
#include "HotUpdateEditorSettings.h"
#include "HotUpdateUtils.h"
#include "HotUpdatePackagingSettingsHelper.h"
#include "HotUpdateAssetFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/MonitoredProcess.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "JsonObjectConverter.h"
#include "UObject/SavePackage.h"

UHotUpdateBasePackageBuilder::UHotUpdateBasePackageBuilder()
	: bIsBuilding(false)
	, bIsCancelled(false)
{
}

// 本地辅助函数：获取资产文件大小
static int64 GetAssetSizeFromPaths(const FString& AssetPath, const TMap<FString, FString>& AssetDiskPaths)
{
	const FString* DiskPath = AssetDiskPaths.Find(AssetPath);
	if (DiskPath && FPaths::FileExists(**DiskPath))
	{
		return IFileManager::Get().FileSize(**DiskPath);
	}
	return 0;
}

FHotUpdateBasePackageResult UHotUpdateBasePackageBuilder::BuildBasePackage(const FHotUpdateBasePackageConfig& Config)
{
	UE_LOG(LogHotUpdateEditor, Log, TEXT("BuildBasePackage (同步) 开始调用"));

	// 从 EditorSettings 注入加密配置
	FHotUpdateBasePackageConfig ModifiedConfig = Config;
	if (ModifiedConfig.IoStoreConfig.EncryptionKey.IsEmpty())
	{
		UHotUpdateEditorSettings* EditorSettings = UHotUpdateEditorSettings::Get();
		ModifiedConfig.IoStoreConfig.EncryptionKey = EditorSettings->DefaultEncryptionKey;
		ModifiedConfig.IoStoreConfig.bEncryptIndex = EditorSettings->bDefaultEncryptIndex;
		ModifiedConfig.IoStoreConfig.bEncryptContent = EditorSettings->bDefaultEncryptContent;
	}

	FHotUpdateBasePackageResult Result;

	// 验证配置
	FString ErrorMessage;
	if (!ValidateConfig(ModifiedConfig, ErrorMessage))
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("配置验证失败: %s"), *ErrorMessage);
		Result.bSuccess = false;
		Result.ErrorMessage = ErrorMessage;
		return Result;
	}

	// 注意：不在此同步版本中检查 bIsBuilding，因为异步版本会在调用前设置此标志
	// 直接调用此同步方法的用户需要自行管理并发控制
	bIsCancelled = false;

	// 编译项目：确保 Cook 使用最新的游戏代码
	if (!ModifiedConfig.bSkipBuild)
	{
		UpdateProgress(TEXT("编译项目"), TEXT(""), 0, 0);
		if (!CompileProject(ModifiedConfig))
		{
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("项目编译失败");
			bIsBuilding = false;
			return Result;
		}
	}
	else
	{
		UE_LOG(LogHotUpdateEditor, Log, TEXT("跳过编译步骤 (bSkipBuild = true)"));
	}

	// Cook 资源：确保使用最新的 cooked 文件
	if (!ModifiedConfig.bSkipCook)
	{
		UpdateProgress(TEXT("Cook 资源"), TEXT(""), 0, 0);
		if (!CookAssets(ModifiedConfig))
		{
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("Cook 资源失败");
			bIsBuilding = false;
			return Result;
		}
	}
	else
	{
		UE_LOG(LogHotUpdateEditor, Log, TEXT("跳过 Cook 步骤 (bSkipCook = true)"));
	}

	// 1. 收集资源
	UpdateProgress(TEXT("收集资源"), TEXT(""), 0, 0);

	TArray<FString> AssetPaths;
	TMap<FString, FString> AssetDiskPaths;

	if (!CollectAssets(Config, AssetPaths, AssetDiskPaths, ErrorMessage))
	{
		Result.bSuccess = false;
		Result.ErrorMessage = ErrorMessage;
		bIsBuilding = false;
		return Result;
	}

	if (AssetPaths.Num() == 0)
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("未找到任何资源");
		bIsBuilding = false;
		return Result;
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("收集了 %d 个资源"), AssetPaths.Num());

	return BuildBasePackageInternal(ModifiedConfig, AssetPaths, AssetDiskPaths);
}

FHotUpdateBasePackageResult UHotUpdateBasePackageBuilder::BuildBasePackageInternal(
	const FHotUpdateBasePackageConfig& Config,
	const TArray<FString>& AssetPaths,
	const TMap<FString, FString>& AssetDiskPaths)
{
	FHotUpdateBasePackageResult Result;

	// 2. 分析 Chunk
	UpdateProgress(TEXT("分析 Chunk"), TEXT(""), 0, AssetPaths.Num());

	FHotUpdateChunkAnalysisResult ChunkResult;

	if (Config.MinimalPackageConfig.bEnableMinimalPackage &&
		(MinimalWhitelistAssets.Num() > 0 || MinimalEngineAssets.Num() > 0))
	{
// 小包模式：手动创建 chunk 0（白名单 + 引擎资产）
		FHotUpdateChunkDefinition Chunk0;
		Chunk0.ChunkId = 0;
		Chunk0.ChunkName = TEXT("Base_Whitelist");
		Chunk0.Priority = 0;

		// 白名单资产和引擎资产放入 chunk 0
		Chunk0.AssetPaths = MinimalWhitelistAssets;
		Chunk0.AssetPaths.Append(MinimalEngineAssets);

		int64 Chunk0Size = 0;
		for (const FString& AssetPath : Chunk0.AssetPaths)
		{
			Chunk0Size += GetAssetSizeFromPaths(AssetPath, AssetDiskPaths);
		}
		Chunk0.UncompressedSize = Chunk0Size;
		Chunk0.CompressedSize = Chunk0Size;

		ChunkResult.Chunks.Add(Chunk0);

// 排除资产按配置策略分到 chunk 1, 2, ...
		if (MinimalExcludedAssets.Num() > 0)
		{
			UHotUpdateChunkManager* BusinessChunkManager = NewObject<UHotUpdateChunkManager>();
			FHotUpdateChunkAnalysisConfig BusinessConfig;
			BusinessConfig.ChunkStrategy = Config.ChunkStrategy;
			BusinessConfig.MaxChunkSizeMB = Config.MaxChunkSizeMB;
			BusinessConfig.SizeBasedConfig = Config.SizeBasedConfig;
			BusinessConfig.SizeBasedConfig.ChunkIdStart = 1;
			BusinessConfig.DirectoryChunkRules = Config.DirectoryChunkRules;
			BusinessConfig.bAnalyzeDependencies = true;
			BusinessConfig.BaseChunkIdStart = 1;
			BusinessConfig.DefaultChunkName = TEXT("Business");
			BusinessConfig.DefaultChunkId = -1;

			FHotUpdateChunkAnalysisResult BusinessResult = BusinessChunkManager->AnalyzeAndCreateChunks(
				MinimalExcludedAssets, AssetDiskPaths, BusinessConfig);

			if (BusinessResult.bSuccess)
			{
				ChunkResult.Chunks.Append(BusinessResult.Chunks);
			}
		}

// 构建资产到 Chunk 的映射
		for (const FHotUpdateChunkDefinition& Chunk : ChunkResult.Chunks)
		{
			for (const FString& AssetPath : Chunk.AssetPaths)
			{
				ChunkResult.AssetToChunkMap.Add(AssetPath, Chunk.ChunkId);
			}
		}

		ChunkResult.TotalAssetCount = AssetPaths.Num();
		ChunkResult.TotalChunkCount = ChunkResult.Chunks.Num();
		ChunkResult.TotalSize = 0;
		for (const FString& AssetPath : AssetPaths)
		{
			ChunkResult.TotalSize += GetAssetSizeFromPaths(AssetPath, AssetDiskPaths);
		}
		ChunkResult.bSuccess = true;
	}
	else
	{
		// 非小包模式：原有逻辑
		UHotUpdateChunkManager* ChunkManager = NewObject<UHotUpdateChunkManager>();
		FHotUpdateChunkAnalysisConfig ChunkConfig;
		ChunkConfig.ChunkStrategy = Config.ChunkStrategy;
		ChunkConfig.MaxChunkSizeMB = Config.MaxChunkSizeMB;
		ChunkConfig.SizeBasedConfig = Config.SizeBasedConfig;
		ChunkConfig.DirectoryChunkRules = Config.DirectoryChunkRules;
		ChunkConfig.bAnalyzeDependencies = true;
		ChunkConfig.BaseChunkIdStart = 0;
		ChunkConfig.PatchChunkIdStart = 10000;
		ChunkConfig.DefaultChunkName = TEXT("Default");
		ChunkConfig.DefaultChunkId = -1;

		ChunkResult = ChunkManager->AnalyzeAndCreateChunks(
			AssetPaths, AssetDiskPaths, ChunkConfig);
	}

	if (!ChunkResult.bSuccess)
	{
		Result.bSuccess = false;
		Result.ErrorMessage = ChunkResult.ErrorMessage;
		bIsBuilding = false;
		return Result;
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("创建了 %d 个 Chunk"), ChunkResult.TotalChunkCount);

	// 3. 确定输出目录
	FString OutputDir = Config.OutputDirectory.Path;
	if (OutputDir.IsEmpty())
	{
		OutputDir = FPaths::ProjectSavedDir() / TEXT("HotUpdatePackages");
	}

	FString VersionStr = Config.VersionString;
	if (VersionStr.IsEmpty())
	{
		VersionStr = FDateTime::Now().ToString();
	}

	FString PlatformStr = HotUpdateUtils::GetPlatformString(Config.Platform);
	OutputDir = FPaths::Combine(OutputDir, VersionStr, PlatformStr);
	FPaths::NormalizeDirectoryName(OutputDir);

	IPlatformFile::GetPlatformPhysical().CreateDirectoryTree(*OutputDir);

	Result.OutputDirectory = OutputDir;
	Result.VersionString = VersionStr;

	// 4. 创建 IoStore 容器
	UpdateProgress(TEXT("创建 IoStore 容器"), TEXT(""), 0, ChunkResult.Chunks.Num());

	UHotUpdateIoStoreBuilder* IoStoreBuilder = NewObject<UHotUpdateIoStoreBuilder>();

	// 收集容器信息
	TArray<FHotUpdateContainerInfo> ContainerInfos;

	for (int32 i = 0; i < ChunkResult.Chunks.Num(); i++)
	{
		if (bIsCancelled)
		{
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("构建已取消");
			bIsBuilding = false;
			return Result;
		}

		const FHotUpdateChunkDefinition& Chunk = ChunkResult.Chunks[i];

// 收集 Chunk 资源的 AssetPath -> DiskPath 映射
		TMap<FString, FString> ChunkAssetPathToDiskPath;
		for (const FString& AssetPath : Chunk.AssetPaths)
		{
			const FString* DiskPath = AssetDiskPaths.Find(AssetPath);
			if (DiskPath && FPaths::FileExists(**DiskPath))
			{
				ChunkAssetPathToDiskPath.Add(AssetPath, *DiskPath);
			}
		}

		if (ChunkAssetPathToDiskPath.Num() == 0)
		{
			continue;
		}

		// 配置 IoStore
		FHotUpdateIoStoreConfig IoStoreConfig = Config.IoStoreConfig;
		IoStoreConfig.ContainerName = FString::Printf(TEXT("Chunk_%d"), Chunk.ChunkId);

		FString ChunkOutputPath = FPaths::Combine(OutputDir, IoStoreConfig.ContainerName);

		FHotUpdateIoStoreResult IoStoreResult = IoStoreBuilder->BuildIoStoreContainer(
			ChunkAssetPathToDiskPath, ChunkOutputPath, IoStoreConfig);

		if (!IoStoreResult.bSuccess)
		{
			Result.bSuccess = false;
			Result.ErrorMessage = FString::Printf(TEXT("Chunk %d 创建失败: %s"),
				Chunk.ChunkId, *IoStoreResult.ErrorMessage);
			bIsBuilding = false;
			return Result;
		}

		Result.ChunkUtocPaths.Add(IoStoreResult.UtocPath);
		// 收集容器信息
		FHotUpdateContainerInfo ContainerInfo;
		ContainerInfo.ContainerName = IoStoreConfig.ContainerName;
		ContainerInfo.ChunkId = Chunk.ChunkId;
		ContainerInfo.ContainerType = EHotUpdateContainerType::Base;

		// utoc 文件信息
		if (FPaths::FileExists(*IoStoreResult.UtocPath))
		{
			ContainerInfo.UtocPath = FPaths::GetCleanFilename(IoStoreResult.UtocPath);
			ContainerInfo.UtocSize = IFileManager::Get().FileSize(*IoStoreResult.UtocPath);
			ContainerInfo.UtocHash = UHotUpdateFileUtils::CalculateFileHash(IoStoreResult.UtocPath);
		}

		// ucas 文件信息（可选）
		if (!IoStoreResult.UcasPath.IsEmpty() && FPaths::FileExists(*IoStoreResult.UcasPath))
		{
			ContainerInfo.UcasPath = FPaths::GetCleanFilename(IoStoreResult.UcasPath);
			ContainerInfo.UcasSize = IFileManager::Get().FileSize(*IoStoreResult.UcasPath);
			ContainerInfo.UcasHash = UHotUpdateFileUtils::CalculateFileHash(IoStoreResult.UcasPath);
		}

		ContainerInfos.Add(ContainerInfo);

		UpdateProgress(TEXT("创建 IoStore 容器"),
			FString::Printf(TEXT("Chunk_%d"), Chunk.ChunkId),
			i + 1, ChunkResult.Chunks.Num());
	}

	// 5. 生成 Manifest
	UpdateProgress(TEXT("生成 Manifest"), TEXT(""), 0, 0);

	FString ManifestPath = FPaths::Combine(OutputDir, TEXT("manifest.json"));

	if (!GenerateManifest(ManifestPath, AssetPaths, AssetDiskPaths, ChunkResult.Chunks, ContainerInfos, Config))
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("生成 Manifest 失败");
		bIsBuilding = false;
		return Result;
	}

	Result.ManifestFilePath = ManifestPath;

	// 6. 注册版本
	UpdateProgress(TEXT("注册版本"), TEXT(""), 0, 0);

	UHotUpdateVersionManager* VersionManager = NewObject<UHotUpdateVersionManager>();

	FHotUpdateEditorVersionInfo VersionInfo;
	VersionInfo.VersionString = VersionStr;
	VersionInfo.PackageKind = EHotUpdatePackageKind::Base;
	VersionInfo.Platform = Config.Platform;
	VersionInfo.CreatedTime = FDateTime::Now();
	VersionInfo.ManifestPath = ManifestPath;
	VersionInfo.AssetCount = AssetPaths.Num();
	VersionInfo.PackageSize = ChunkResult.TotalSize;

	if (Result.ChunkUtocPaths.Num() > 0)
	{
		VersionInfo.UtocPath = Result.ChunkUtocPaths[0];
	}

	VersionManager->RegisterVersion(VersionInfo);

	// 7. 完成
	Result.Chunks = ChunkResult.Chunks;
	Result.TotalAssetCount = AssetPaths.Num();
	Result.TotalSize = ChunkResult.TotalSize;
	Result.bSuccess = true;

	bIsBuilding = false;

	UpdateProgress(TEXT("完成"), TEXT(""), 1, 1);

	UE_LOG(LogHotUpdateEditor, Log, TEXT("基础包构建成功: %s"), *OutputDir);

	return Result;
}

void UHotUpdateBasePackageBuilder::BuildBasePackageAsync(const FHotUpdateBasePackageConfig& Config)
{
	UE_LOG(LogHotUpdateEditor, Log, TEXT("BuildBasePackageAsync 开始调用"));
	UE_LOG(LogHotUpdateEditor, Log, TEXT("  bIsBuilding: %s"), bIsBuilding ? TEXT("true") : TEXT("false"));
	UE_LOG(LogHotUpdateEditor, Log, TEXT("  BuildTask.IsValid(): %s"), BuildTask.IsValid() ? TEXT("true") : TEXT("false"));
	UE_LOG(LogHotUpdateEditor, Log, TEXT("  BuildTask.IsReady(): %s"), BuildTask.IsReady() ? TEXT("true") : TEXT("false"));

	// 检查是否有正在运行的构建任务
	if (bIsBuilding)
	{
		if (BuildTask.IsValid() && !BuildTask.IsReady())
		{
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("已有基础包构建任务正在运行，拒绝新的构建请求"));
			FHotUpdateBasePackageResult Result;
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("已有构建任务正在进行中");
			OnComplete.Broadcast(Result);
			return;
		}
		else
		{
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("检测到之前的构建异常终止，正在重置构建状态"));
			bIsBuilding = false;
			bIsCancelled = false;
		}
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("开始新的基础包构建任务，版本: %s"), *Config.VersionString);

	bIsBuilding = true;
	bIsCancelled = false;

	// 在游戏线程上预先收集所有 AssetRegistry 数据
	TArray<FString> PreCollectedAssetPaths;
	TMap<FString, FString> PreCollectedAssetDiskPaths;
	FString PreCollectError;

	if (Config.PackageType == EHotUpdatePackageType::FromPackagingSettings)
	{
		FHotUpdatePackagingSettingsResult SettingsResult = FHotUpdatePackagingSettingsHelper::ParsePackagingSettings(true);
		if (SettingsResult.Errors.Num() > 0)
		{
			PreCollectError = FString::Join(SettingsResult.Errors, TEXT("\n"));
		}
		else
		{
			PreCollectedAssetPaths = SettingsResult.AssetPaths;
		}
	}
	else if (Config.PackageType == EHotUpdatePackageType::Directory)
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry* AssetRegistry = &AssetRegistryModule.Get();

		for (const FString& DirPath : Config.AssetPaths)
		{
			FARFilter Filter;
			Filter.PackagePaths.Add(FName(*DirPath));
			Filter.bRecursivePaths = true;

			TArray<FAssetData> AssetDataList;
			AssetRegistry->GetAssets(Filter, AssetDataList);

			for (const FAssetData& AssetData : AssetDataList)
			{
				PreCollectedAssetPaths.Add(AssetData.PackageName.ToString());
			}
		}
	}
	else if (Config.PackageType == EHotUpdatePackageType::Asset)
	{
		PreCollectedAssetPaths = Config.AssetPaths;
	}

	if (Config.bIncludeDependencies && PreCollectedAssetPaths.Num() > 0 && PreCollectError.IsEmpty())
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry* AssetRegistry = &AssetRegistryModule.Get();

		TSet<FString> UniquePaths(PreCollectedAssetPaths);

		for (const FString& AssetPath : PreCollectedAssetPaths)
		{
			TArray<FName> Dependencies;
			if (AssetRegistry->GetDependencies(FName(*AssetPath), Dependencies))
			{
				for (const FName& Dep : Dependencies)
				{
					FString DepStr = Dep.ToString();
					if (DepStr.StartsWith(TEXT("/Game/")) || DepStr.StartsWith(TEXT("/Engine/")))
					{
						UniquePaths.Add(DepStr);
					}
				}
			}
		}

		PreCollectedAssetPaths = UniquePaths.Array();
	}


	// 最小包过滤逻辑
	TArray<FString> AsyncMinimalWhitelist;
	TArray<FString> AsyncMinimalEngine;
	TArray<FString> AsyncMinimalExcluded;

	if (Config.MinimalPackageConfig.bEnableMinimalPackage && PreCollectedAssetPaths.Num() > 0 && PreCollectError.IsEmpty())
	{
		UE_LOG(LogHotUpdateEditor, Log, TEXT("异步构建: 启用最小包模式，开始三分类过滤"));

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry* AssetRegistry = &AssetRegistryModule.Get();

		FHotUpdateAssetFilter::FilterAssetsForMinimalPackage(
			PreCollectedAssetPaths,
			Config.MinimalPackageConfig,
			AssetRegistry,
			AsyncMinimalWhitelist,
			AsyncMinimalEngine,
			AsyncMinimalExcluded);

		UE_LOG(LogHotUpdateEditor, Log,
			TEXT("异步构建: 小包模式三分类完成: 白名单 %d, 引擎依赖 %d, 排除 %d"),
			AsyncMinimalWhitelist.Num(), AsyncMinimalEngine.Num(), AsyncMinimalExcluded.Num());

		if (AsyncMinimalWhitelist.Num() == 0 && AsyncMinimalEngine.Num() == 0)
		{
			bIsBuilding = false;
			FHotUpdateBasePackageResult Result;
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("最小包模式过滤后白名单和引擎资产为空，请检查必须包含的目录配置");
			OnComplete.Broadcast(Result);
			return;
		}

		PreCollectedAssetPaths = AsyncMinimalWhitelist;
		PreCollectedAssetPaths.Append(AsyncMinimalEngine);
		PreCollectedAssetPaths.Append(AsyncMinimalExcluded);
	}

	MinimalWhitelistAssets = MoveTemp(AsyncMinimalWhitelist);
	MinimalEngineAssets = MoveTemp(AsyncMinimalEngine);
	MinimalExcludedAssets = MoveTemp(AsyncMinimalExcluded);

	FString CookedPlatformDir = FPaths::ProjectSavedDir() / TEXT("Cooked") / HotUpdateUtils::GetPlatformString(Config.Platform);
	for (const FString& AssetPath : PreCollectedAssetPaths)
	{
		FString DiskPath = UHotUpdatePatchPackageBuilder::GetAssetDiskPath(AssetPath, CookedPlatformDir);

		if (!DiskPath.IsEmpty() && FPaths::FileExists(*DiskPath))
		{
			PreCollectedAssetDiskPaths.Add(AssetPath, DiskPath);
		}
	}

	if (!PreCollectError.IsEmpty())
	{
		bIsBuilding = false;
		FHotUpdateBasePackageResult Result;
		Result.bSuccess = false;
		Result.ErrorMessage = PreCollectError;
		OnComplete.Broadcast(Result);
		return;
	}

	if (PreCollectedAssetPaths.Num() == 0)
	{
		bIsBuilding = false;
		FHotUpdateBasePackageResult Result;
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("未找到任何资源");
		OnComplete.Broadcast(Result);
		return;
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("在游戏线程预收集了 %d 个资源"), PreCollectedAssetPaths.Num());

	FHotUpdateBasePackageConfig ModifiedConfig = Config;
	ModifiedConfig.bIncludeDependencies = false;

	if (ModifiedConfig.IoStoreConfig.EncryptionKey.IsEmpty())
	{
		UHotUpdateEditorSettings* EditorSettings = UHotUpdateEditorSettings::Get();
		ModifiedConfig.IoStoreConfig.EncryptionKey = EditorSettings->DefaultEncryptionKey;
		ModifiedConfig.IoStoreConfig.bEncryptIndex = EditorSettings->bDefaultEncryptIndex;
		ModifiedConfig.IoStoreConfig.bEncryptContent = EditorSettings->bDefaultEncryptContent;
	}

	BuildTask = Async(EAsyncExecution::Thread, [this, ModifiedConfig,
		PreCollectedAssetPaths, PreCollectedAssetDiskPaths]()
	{
		struct FBuildGuard
		{
			UHotUpdateBasePackageBuilder* Builder;
			FHotUpdateBasePackageResult Result;
			bool bNormalCompletion = false;
			FBuildGuard(UHotUpdateBasePackageBuilder* InBuilder) : Builder(InBuilder) {}
			~FBuildGuard()
			{
				if (Builder && Builder->bIsBuilding && !bNormalCompletion)
				{
					Builder->bIsBuilding = false;
					UE_LOG(LogHotUpdateEditor, Warning, TEXT("基础包构建异常终止，已重置构建状态"));

					AsyncTask(ENamedThreads::GameThread, [this]()
					{
						Builder->OnComplete.Broadcast(Result);
					});
				}
			}
		};
		FBuildGuard Guard(this);

		FHotUpdateBasePackageResult Result = BuildBasePackageWithPreCollectedAssets(
			ModifiedConfig, PreCollectedAssetPaths, PreCollectedAssetDiskPaths);
		Guard.Result = Result;
		Guard.bNormalCompletion = true;

		AsyncTask(ENamedThreads::GameThread, [this, Result]()
		{
			OnComplete.Broadcast(Result);
		});
	});
}

void UHotUpdateBasePackageBuilder::CancelBuild()
{
	bIsCancelled = true;
}

FHotUpdatePackageProgress UHotUpdateBasePackageBuilder::GetCurrentProgress() const
{
	FScopeLock Lock(&ProgressCriticalSection);
	return CurrentProgress;
}

bool UHotUpdateBasePackageBuilder::ValidateConfig(const FHotUpdateBasePackageConfig& Config, FString& OutErrorMessage)
{
	if (!Config.VersionString.IsEmpty())
	{
		TArray<FString> Parts;
		Config.VersionString.ParseIntoArray(Parts, TEXT("."));

		if (Parts.Num() < 2)
		{
			OutErrorMessage = TEXT("版本号格式不正确，应为 Major.Minor.Patch.Build 或 Major.Minor");
			return false;
		}
	}

	return true;
}

bool UHotUpdateBasePackageBuilder::CompileProject(const FHotUpdateBasePackageConfig& Config)
{
	UE_LOG(LogHotUpdateEditor, Log, TEXT("开始编译项目..."));

	// 使用 UnrealBuildTool 直接编译游戏目标
	// 比 UAT BuildCookRun 更轻量，且在编辑器运行时更可靠
	FString EngineDir = FPaths::EngineDir();
	FString UBTPath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(EngineDir, TEXT("Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll")));

	FString ProjectPath = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
	FString PlatformName = HotUpdateUtils::GetPlatformDirectoryName(Config.Platform);

	// 构建配置字符串
	FString BuildConfig;
	switch (Config.BuildConfiguration)
	{
	case EHotUpdateBuildConfiguration::DebugGame:
		BuildConfig = TEXT("DebugGame"); break;
	case EHotUpdateBuildConfiguration::Development:
		BuildConfig = TEXT("Development"); break;
	case EHotUpdateBuildConfiguration::Shipping:
		BuildConfig = TEXT("Shipping"); break;
	default:
		BuildConfig = TEXT("Development"); break;
	}

	// UBT 命令: dotnet UnrealBuildTool.dll <TargetName> <Platform> <Config> -project="..."
	FString Params = FString::Printf(
		TEXT("\"%s\" GameUpdate %s %s -project=\"%s\""),
		*UBTPath, *PlatformName, *BuildConfig, *ProjectPath);

	UE_LOG(LogHotUpdateEditor, Log, TEXT("执行编译: dotnet %s"), *Params);

	FString CommandLine = FString::Printf(TEXT("/c dotnet %s"), *Params);

	// 使用 FMonitoredProcess 执行，实时输出编译日志
	FMonitoredProcess Process(TEXT("cmd.exe"), CommandLine, true);

	Process.OnOutput().BindLambda([](const FString& Output)
	{
		UE_LOG(LogHotUpdateEditor, Log, TEXT("%s"), *Output);
	});

	if (!Process.Launch())
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("无法启动编译进程"));
		return false;
	}

	// 等待进程完成，同时检查取消状态
	while (Process.Update())
	{
		if (bIsCancelled)
		{
			Process.Cancel();
			break;
		}
		FPlatformProcess::Sleep(0.1f);
	}

	int32 ReturnCode = Process.GetReturnCode();

	if (bIsCancelled)
	{
		UE_LOG(LogHotUpdateEditor, Warning, TEXT("编译已取消"));
		return false;
	}

	if (ReturnCode != 0)
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("编译失败，返回码: %d"), ReturnCode);
		return false;
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("编译完成"));
	return true;
}

bool UHotUpdateBasePackageBuilder::CookAssets(const FHotUpdateBasePackageConfig& Config)
{
	UE_LOG(LogHotUpdateEditor, Log, TEXT("开始 Cook 资源..."));

	// 使用子进程执行 Cook，避免在当前 Editor 进程中调用 CookCommandlet 导致的平台冲突
	FString EngineDir = FPaths::EngineDir();
	FString ExePath = FPaths::ConvertRelativePathToFull(EngineDir / TEXT("Binaries/Win64/UnrealEditor-Cmd.exe"));
	FString ProjectPath = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());

	FString CookPlatform = HotUpdateUtils::GetPlatformString(Config.Platform);
	FString Params = FString::Printf(TEXT("\"%s\" -run=cook -targetplatform=%s -NullRHI -unattended -NoSound"),
		*ProjectPath, *CookPlatform);

	UE_LOG(LogHotUpdateEditor, Log, TEXT("执行 Cook: %s %s"), *ExePath, *Params);

	// 使用 FMonitoredProcess 执行，实时输出 Cook 日志
	FMonitoredProcess Process(ExePath, Params, true);

	Process.OnOutput().BindLambda([](const FString& Output)
	{
		UE_LOG(LogHotUpdateEditor, Log, TEXT("%s"), *Output);
	});

	if (!Process.Launch())
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("无法启动 Cook 进程"));
		return false;
	}

	// 等待进程完成，同时检查取消状态
	while (Process.Update())
	{
		if (bIsCancelled)
		{
			Process.Cancel();
			break;
		}
		FPlatformProcess::Sleep(0.1f);
	}

	int32 ReturnCode = Process.GetReturnCode();

	if (bIsCancelled)
	{
		UE_LOG(LogHotUpdateEditor, Warning, TEXT("Cook 已取消"));
		return false;
	}

	if (ReturnCode != 0)
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("Cook 失败，返回码: %d"), ReturnCode);
		return false;
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("Cook 完成"));
	return true;
}

bool UHotUpdateBasePackageBuilder::CollectAssets(
	const FHotUpdateBasePackageConfig& Config,
	TArray<FString>& OutAssetPaths,
	TMap<FString, FString>& OutAssetDiskPaths,
	FString& OutErrorMessage)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry* AssetRegistry = &AssetRegistryModule.Get();

	TArray<FString> AllAssetPaths;

	switch (Config.PackageType)
	{
	case EHotUpdatePackageType::Asset:
		AllAssetPaths = Config.AssetPaths;
		break;

	case EHotUpdatePackageType::Directory:
		for (const FString& DirPath : Config.AssetPaths)
		{
			FARFilter Filter;
			Filter.PackagePaths.Add(FName(*DirPath));
			Filter.bRecursivePaths = true;

			TArray<FAssetData> AssetDataList;
			AssetRegistry->GetAssets(Filter, AssetDataList);

			for (const FAssetData& AssetData : AssetDataList)
			{
				AllAssetPaths.Add(AssetData.PackageName.ToString());
			}
		}
		break;

	case EHotUpdatePackageType::FromPackagingSettings:
		{
			FHotUpdatePackagingSettingsResult SettingsResult = FHotUpdatePackagingSettingsHelper::ParsePackagingSettings(true);
			if (SettingsResult.Errors.Num() > 0)
			{
				OutErrorMessage = FString::Join(SettingsResult.Errors, TEXT("\n"));
				return false;
			}
			AllAssetPaths = SettingsResult.AssetPaths;
		}
		break;
	}

	if (Config.bIncludeDependencies)
	{
		TSet<FString> UniquePaths(AllAssetPaths);

		for (const FString& AssetPath : AllAssetPaths)
		{
			TArray<FName> Dependencies;
			if (AssetRegistry->GetDependencies(FName(*AssetPath), Dependencies))
			{
				for (const FName& Dep : Dependencies)
				{
					FString DepStr = Dep.ToString();
					if (DepStr.StartsWith(TEXT("/Game/")) || DepStr.StartsWith(TEXT("/Engine/")))
					{
						UniquePaths.Add(DepStr);
					}
				}
			}
		}

		AllAssetPaths = UniquePaths.Array();
	}


	// 最小包过滤逻辑
	if (Config.MinimalPackageConfig.bEnableMinimalPackage)
	{
		UE_LOG(LogHotUpdateEditor, Log, TEXT("启用最小包模式，开始三分类过滤"));

		TArray<FString> WhitelistAssets;
		TArray<FString> EngineAssets;
		TArray<FString> ExcludedAssets;

		FHotUpdateAssetFilter::FilterAssetsForMinimalPackage(
			AllAssetPaths,
			Config.MinimalPackageConfig,
			AssetRegistry,
			WhitelistAssets,
			EngineAssets,
			ExcludedAssets);

		UE_LOG(LogHotUpdateEditor, Log,
			TEXT("小包模式三分类完成: 白名单 %d, 引擎依赖 %d, 排除 %d"),
			WhitelistAssets.Num(), EngineAssets.Num(), ExcludedAssets.Num());

		if (WhitelistAssets.Num() == 0 && EngineAssets.Num() == 0)
		{
			OutErrorMessage = TEXT("最小包模式过滤后白名单和引擎资产为空，请检查必须包含的目录配置");
			return false;
		}

		MinimalWhitelistAssets = MoveTemp(WhitelistAssets);
		MinimalEngineAssets = MoveTemp(EngineAssets);
		MinimalExcludedAssets = MoveTemp(ExcludedAssets);

		AllAssetPaths = MinimalWhitelistAssets;
		AllAssetPaths.Append(MinimalEngineAssets);
		AllAssetPaths.Append(MinimalExcludedAssets);
	}

	FString CookedPlatformDir = FPaths::ProjectSavedDir() / TEXT("Cooked") / HotUpdateUtils::GetPlatformString(Config.Platform);
	for (const FString& AssetPath : AllAssetPaths)
	{
		FString DiskPath = UHotUpdatePatchPackageBuilder::GetAssetDiskPath(AssetPath, CookedPlatformDir);

		if (!DiskPath.IsEmpty() && FPaths::FileExists(*DiskPath))
		{
			OutAssetPaths.Add(AssetPath);
			OutAssetDiskPaths.Add(AssetPath, DiskPath);
		}
	}

	return true;
}

bool UHotUpdateBasePackageBuilder::GenerateManifest(
	const FString& ManifestPath,
	const TArray<FString>& AssetPaths,
	const TMap<FString, FString>& AssetDiskPaths,
	const TArray<FHotUpdateChunkDefinition>& Chunks,
	const TArray<FHotUpdateContainerInfo>& Containers,
	const FHotUpdateBasePackageConfig& Config)
{
	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);

	TSharedPtr<FJsonObject> VersionObject = MakeShareable(new FJsonObject);

	TArray<FString> VersionParts;
	Config.VersionString.ParseIntoArray(VersionParts, TEXT("."));

	int32 Major = VersionParts.Num() > 0 ? FCString::Atoi(*VersionParts[0]) : 0;
	int32 Minor = VersionParts.Num() > 1 ? FCString::Atoi(*VersionParts[1]) : 0;
	int32 Patch = VersionParts.Num() > 2 ? FCString::Atoi(*VersionParts[2]) : 0;
	int32 Build = VersionParts.Num() > 3 ? FCString::Atoi(*VersionParts[3]) : 0;

	VersionObject->SetNumberField(TEXT("major"), Major);
	VersionObject->SetNumberField(TEXT("minor"), Minor);
	VersionObject->SetNumberField(TEXT("patch"), Patch);
	VersionObject->SetNumberField(TEXT("buildNumber"), Build);
	VersionObject->SetStringField(TEXT("versionString"), Config.VersionString);
	VersionObject->SetStringField(TEXT("platform"), HotUpdateUtils::GetPlatformString(Config.Platform));
	VersionObject->SetNumberField(TEXT("timestamp"), FDateTime::Now().ToUnixTimestamp());

	RootObject->SetObjectField(TEXT("version"), VersionObject);

	TArray<TSharedPtr<FJsonValue>> ChunksArray;
	for (const FHotUpdateContainerInfo& Container : Containers)
	{
		TSharedPtr<FJsonObject> ChunkObject = MakeShareable(new FJsonObject);
		ChunkObject->SetStringField(TEXT("ChunkName"), Container.ContainerName);
		ChunkObject->SetStringField(TEXT("utocPath"), Container.UtocPath);
		ChunkObject->SetNumberField(TEXT("utocSize"), Container.UtocSize);
		ChunkObject->SetStringField(TEXT("utocHash"), Container.UtocHash);
		ChunkObject->SetStringField(TEXT("ucasPath"), Container.UcasPath.IsEmpty() ? TEXT("") : Container.UcasPath);
		ChunkObject->SetNumberField(TEXT("ucasSize"), Container.UcasSize);
		ChunkObject->SetStringField(TEXT("ucasHash"), Container.UcasHash.IsEmpty() ? TEXT("") : Container.UcasHash);
		ChunksArray.Add(MakeShareable(new FJsonValueObject(ChunkObject)));
	}
	RootObject->SetArrayField(TEXT("chunks"), ChunksArray);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

	if (!FFileHelper::SaveStringToFile(OutputString, *ManifestPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		return false;
	}

	FString FileManifestPath = FPaths::Combine(FPaths::GetPath(ManifestPath),
		TEXT("filemanifest.json"));

	TSharedPtr<FJsonObject> FileManifestObj = MakeShareable(new FJsonObject);
	FileManifestObj->SetObjectField(TEXT("version"), VersionObject);

	TArray<TSharedPtr<FJsonValue>> FilesArray;

	for (const FString& AssetPath : AssetPaths)
	{
		const FString* DiskPath = AssetDiskPaths.Find(AssetPath);
		if (!DiskPath) continue;

		TSharedPtr<FJsonObject> FileObj = MakeShareable(new FJsonObject);
		FileObj->SetStringField(TEXT("filePath"), UHotUpdatePatchPackageBuilder::ConvertAssetPathToFileName(AssetPath));

		int64 FileSize = IFileManager::Get().FileSize(**DiskPath);
		FileObj->SetNumberField(TEXT("fileSize"), FileSize);
		FileObj->SetStringField(TEXT("fileHash"), UHotUpdateFileUtils::CalculateFileHash(*DiskPath));
		FileObj->SetNumberField(TEXT("chunkId"), 0);
		FileObj->SetNumberField(TEXT("priority"), 0);
		FileObj->SetBoolField(TEXT("isCompressed"), Config.IoStoreConfig.CompressionFormat != TEXT("None"));

		FilesArray.Add(MakeShareable(new FJsonValueObject(FileObj)));
	}

	FileManifestObj->SetArrayField(TEXT("files"), FilesArray);

	FString FileManifestString;
	TSharedRef<TJsonWriter<>> FileWriter = TJsonWriterFactory<>::Create(&FileManifestString);
	FJsonSerializer::Serialize(FileManifestObj.ToSharedRef(), FileWriter);

	return FFileHelper::SaveStringToFile(FileManifestString, *FileManifestPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void UHotUpdateBasePackageBuilder::UpdateProgress(
	const FString& Stage,
	const FString& CurrentFile,
	int32 ProcessedFiles,
	int32 TotalFiles)
{
	{
		FScopeLock Lock(&ProgressCriticalSection);
		CurrentProgress.CurrentStage = Stage;
		CurrentProgress.CurrentFile = CurrentFile;
		CurrentProgress.ProcessedFiles = ProcessedFiles;
		CurrentProgress.TotalFiles = TotalFiles;
		CurrentProgress.bIsComplete = (ProcessedFiles >= TotalFiles && TotalFiles > 0);
	}

	OnProgress.Broadcast(CurrentProgress);
}

FHotUpdateBasePackageResult UHotUpdateBasePackageBuilder::BuildBasePackageWithPreCollectedAssets(
	const FHotUpdateBasePackageConfig& Config,
	const TArray<FString>& PreCollectedAssetPaths,
	const TMap<FString, FString>& PreCollectedAssetDiskPaths)
{
	UE_LOG(LogHotUpdateEditor, Log, TEXT("BuildBasePackageWithPreCollectedAssets 开始，资源数: %d"), PreCollectedAssetPaths.Num());

	FHotUpdateBasePackageResult Result;

	// 验证配置
	FString ErrorMessage;
	if (!ValidateConfig(Config, ErrorMessage))
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("配置验证失败: %s"), *ErrorMessage);
		Result.bSuccess = false;
		Result.ErrorMessage = ErrorMessage;
		bIsBuilding = false;
		return Result;
	}

	bIsCancelled = false;

	// 编译项目：确保 Cook 使用最新的游戏代码
	if (!Config.bSkipBuild)
	{
		UpdateProgress(TEXT("编译项目"), TEXT(""), 0, 0);
		if (!CompileProject(Config))
		{
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("项目编译失败");
			bIsBuilding = false;
			return Result;
		}
	}
	else
	{
		UE_LOG(LogHotUpdateEditor, Log, TEXT("跳过编译步骤 (bSkipBuild = true)"));
	}

	// Cook 资源：确保使用最新的 cooked 文件
	if (!Config.bSkipCook)
	{
		UpdateProgress(TEXT("Cook 资源"), TEXT(""), 0, 0);
		if (!CookAssets(Config))
		{
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("Cook 资源失败");
			bIsBuilding = false;
			return Result;
		}
	}
	else
	{
		UE_LOG(LogHotUpdateEditor, Log, TEXT("跳过 Cook 步骤 (bSkipCook = true)"));
	}

	if (PreCollectedAssetPaths.Num() == 0)
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("未找到任何资源");
		bIsBuilding = false;
		return Result;
	}

	return BuildBasePackageInternal(Config, PreCollectedAssetPaths, PreCollectedAssetDiskPaths);
}