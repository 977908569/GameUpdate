// Copyright czm. All Rights Reserved.

#include "HotUpdateBaseVersionBuilder.h"
#include "HotUpdatePackageHelper.h"
#include "Core/HotUpdateFileUtils.h"
#include "HotUpdateEditor.h"
#include "HotUpdateVersionManager.h"
#include "HotUpdatePackagingSettingsHelper.h"
#include "HotUpdateUtils.h"
#include "HotUpdateAssetFilter.h"
#include "HotUpdateChunkManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/MonitoredProcess.h"
#include "JsonObjectConverter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/PackageName.h"

/**
 * 资源解析结果
 */
struct FHotUpdateResolvedAssetInfo
{
	/** 相对路径（如 Game/Maps/MainMenu.umap 或 Engine/Plugins/.../xxx.uasset），用于 manifest filePath */
	FString AssetPath;
	/** 文件 Hash */
	FString FileHash;
	/** 文件大小 */
	int64 FileSize;

	FHotUpdateResolvedAssetInfo()
		: FileSize(0)
	{}

	FHotUpdateResolvedAssetInfo(const FString& InAssetPath, const FString& InFileHash, int64 InFileSize)
		: AssetPath(InAssetPath)
		, FileHash(InFileHash)
		, FileSize(InFileSize)
	{}
	
};

FHotUpdateBaseVersionBuilder::FHotUpdateBaseVersionBuilder()
	: bIsBuilding(false)
	, bIsCancelled(false)
{
}

FString FHotUpdateBaseVersionBuilder::GetDefaultOutputDirectory()
{
	return FPaths::ProjectSavedDir() / TEXT("BaseVersionBuilds");
}


void FHotUpdateBaseVersionBuilder::BuildBaseVersion(const FHotUpdateBaseVersionBuildConfig& Config)
{
	CurrentConfig = Config;
	
	// 检查是否有正在运行的构建任务
	// 如果 bIsBuilding 为 true 但 BuildTask 已经完成，说明之前的构建异常终止，需要重置状态
	if (bIsBuilding)
	{
		if (BuildTask.IsValid() && !BuildTask.IsReady())
		{
			// 任务仍在运行
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("已有构建任务正在运行"));
			return;
		}
		else
		{
			// 之前的构建异常终止，重置状态
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("检测到之前的构建异常终止，正在重置构建状态"));
			bIsBuilding = false;
			bIsCancelled = false;
		}
	}

	if (Config.VersionString.IsEmpty())
	{
		FHotUpdateBaseVersionBuildResult Result;
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("版本号不能为空");
		OnBuildComplete.Broadcast(Result);
		return;
	}

	// 验证配置中所有字符串成员的有效性，防止赋值时崩溃
	// 检查 MinimalPackageConfig 中的目录路径是否有效
	if (CurrentConfig.MinimalPackageConfig.bEnableMinimalPackage)
	{
		for (const FDirectoryPath& Dir : Config.MinimalPackageConfig.WhitelistDirectories)
		{
			if (Dir.Path.IsEmpty())
			{
				UE_LOG(LogHotUpdateEditor, Error, TEXT("WhitelistDirectories 包含无效的 FDirectoryPath"));
				FHotUpdateBaseVersionBuildResult Result;
				Result.bSuccess = false;
				Result.ErrorMessage = TEXT("白名单目录配置包含无效数据");
				OnBuildComplete.Broadcast(Result);
				return;
			}
		}
	}

	bIsBuilding = true;
	bIsCancelled = false;
	
	// 在游戏线程预收集资源数据
	if (CurrentConfig.MinimalPackageConfig.bEnableMinimalPackage)
	{
		UE_LOG(LogHotUpdateEditor, Log, TEXT("在游戏线程预收集最小包资源数据..."));

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry* AssetRegistry = &AssetRegistryModule.Get();

		// 等待 AssetRegistry 加载完成
		if (AssetRegistry->IsLoadingAssets())
		{
			AssetRegistry->SearchAllAssets(true);
			while (AssetRegistry->IsLoadingAssets())
			{
				FPlatformProcess::Sleep(0.1f);
			}
		}

		// 1. 收集白名单目录下的资产及其引用者
		TSet<FString> WhitelistSet;
		for (const FDirectoryPath& Dir : CurrentConfig.MinimalPackageConfig.WhitelistDirectories)
		{
			FString ContentDir = FPackageName::LongPackageNameToFilename(Dir.Path);
			if (!ContentDir.EndsWith(TEXT("/")))
			{
				ContentDir += TEXT("/");
			}

			TArray<FString> PackageFiles;
			FPackageName::FindPackagesInDirectory(PackageFiles, ContentDir);

			for (const FString& PackageFile : PackageFiles)
			{
				FString PackageName = FPackageName::FilenameToLongPackageName(PackageFile);
				// 收集白名单资源及其依赖（依赖应进入首包）
				TSet<FString> CollectedPackages;
				FHotUpdateAssetFilter::GetDependencies(PackageName, AssetRegistry, EHotUpdateDependencyStrategy::IncludeAll, CollectedPackages);
				WhitelistSet.Append(CollectedPackages);
			}
		}

		UE_LOG(LogHotUpdateEditor, Log, TEXT("白名单资源及其依赖收集完成: %d 个资产"), WhitelistSet.Num());

		// 2. 收集所有资源（包含依赖）
		FHotUpdatePackagingSettingsResult PackagingResult = FHotUpdatePackagingSettingsHelper::ParsePackagingSettings(true);
		TArray<FString> AllAssetPaths = PackagingResult.AssetPaths;

		// 3. 拆分首包资源（白名单+引擎）和热更资源
		TArray<FString> PatchAssetPaths;
		TArray<FString> WhitelistAssetPaths;

		// 白名单资源直接添加到首包
		for (const FString& WhitelistAsset : WhitelistSet)
		{
			WhitelistAssetPaths.AddUnique(WhitelistAsset);
		}

		for (const FString& AssetPath : AllAssetPaths)
		{
			// 白名单资源已添加，跳过
			if (WhitelistSet.Contains(AssetPath))
			{
				continue;
			}

			// 引擎资源 -> 首包
			if (UHotUpdateFileUtils::IsEngineAsset(AssetPath))
			{
				WhitelistAssetPaths.AddUnique(AssetPath);
				continue;
			}

			// 其余 -> 热更资源
			PatchAssetPaths.AddUnique(AssetPath);
		}

		CurrentConfig.PreCollectedPatchAssetPaths = PatchAssetPaths;
		CachedWhitelistAssetPaths = WhitelistAssetPaths;
		CurrentConfig.PreCollectedNonAssetFiles = PackagingResult.NonAssetPaths;

		UE_LOG(LogHotUpdateEditor, Log, TEXT("预收集完成，首包资源: %d 个，热更资源: %d 个，Staged 文件: %d 个"),
			WhitelistAssetPaths.Num(), PatchAssetPaths.Num(), PackagingResult.NonAssetPaths.Num());
	}

	if (CurrentConfig.bSynchronousMode)
	{
		ExecuteBuildInternal();
	}
	else
	{
		// 在后台线程执行构建（编辑器模式）
		BuildTask = Async(EAsyncExecution::Thread, [WeakThis = TWeakPtr<FHotUpdateBaseVersionBuilder>(AsShared())]()
		{
			TSharedPtr<FHotUpdateBaseVersionBuilder> StrongThis = WeakThis.Pin();
			if (!StrongThis.IsValid()) return;
			StrongThis->ExecuteBuildInternal();
		});
	}
}

void FHotUpdateBaseVersionBuilder::CancelBuild()
{
	bIsCancelled = true;
}

void FHotUpdateBaseVersionBuilder::ExecuteBuildInternal()
{
	// 清空上次构建的缓存
	CachedChunkMapping.Empty();
	CachedChunkDefinitions.Empty();

	FHotUpdateBaseVersionBuildResult Result;
	Result.VersionString = CurrentConfig.VersionString;
	Result.Platform = CurrentConfig.Platform;

	UE_LOG(LogHotUpdateEditor, Log, TEXT("开始构建基础版本（%s）: %s, 平台: %s"), CurrentConfig.bSynchronousMode ? TEXT("同步") : TEXT("异步"),
		*CurrentConfig.VersionString, *HotUpdateUtils::GetPlatformDirectoryName(CurrentConfig.Platform));

	// 1. 确定输出目录
	FString OutputDir = CurrentConfig.OutputDirectory;
	if (OutputDir.IsEmpty())
	{
		OutputDir = GetDefaultOutputDirectory();
	}
	OutputDir = FPaths::Combine(OutputDir, CurrentConfig.VersionString);
	FPaths::NormalizeDirectoryName(OutputDir);

	Result.OutputDirectory = OutputDir;

	UpdateProgress(TEXT("准备构建环境"), 0.05f, TEXT("正在准备构建环境..."));

	// 2. 写入最小包配置
	if (CurrentConfig.MinimalPackageConfig.bEnableMinimalPackage)
	{
		// 预计算非白名单资源的 Chunk 分配
		PreComputeChunkMapping();
		// 写入完整配置（包含 ChunkMapping）
		WriteMinimalPackageConfig();
	}
	else
	{
		// 整包模式：删除残留的最小包配置文件，防止 Cook 子进程读到旧配置导致 chunk 分配错误
		FString ConfigFile = FPaths::ProjectIntermediateDir() / TEXT("MinimalPackageConfig.json");
		if (FPaths::FileExists(ConfigFile))
		{
			IFileManager::Get().Delete(*ConfigFile);
			UE_LOG(LogHotUpdateEditor, Log, TEXT("已清理残留的最小包配置文件: %s"), *ConfigFile);
		}
	}

	// 3. 执行打包
	FString ErrorMsg;
	FString UATCommand = GenerateUATCommand();
	UE_LOG(LogHotUpdateEditor, Log, TEXT("UAT 命令: %s"), *UATCommand);
	bool bPackageSuccess = ExecuteUATPackage(UATCommand, ErrorMsg);

	if (bIsCancelled)
	{
		bIsBuilding = false;
		BroadcastBuildResult(MakeErrorResult(TEXT("构建已取消")));
		return;
	}

	if (!bPackageSuccess)
	{
		bIsBuilding = false;
		BroadcastBuildResult(MakeErrorResult(FString::Printf(TEXT("项目打包失败: %s"), *ErrorMsg)));
		return;
	}

	// 4. 检查输出
	UpdateProgress(TEXT("检查构建输出"), 0.85f, TEXT("正在检查构建输出..."));

	if (!CheckBuildOutput(OutputDir, Result.ExecutablePath))
	{
		bIsBuilding = false;
		BroadcastBuildResult(MakeErrorResult(TEXT("未找到构建输出文件")));
		return;
	}

	// 5. 保存资源 Hash 清单
	UpdateProgress(TEXT("保存资源清单"), 0.9f, TEXT("正在保存资源Hash清单..."));

	if (CurrentConfig.bSynchronousMode)
	{
		if (!SaveResourceHashesInGameThread())
		{
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("保存资源Hash清单失败，但构建成功"));
		}
	}
	else
	{
		TPromise<bool> SaveResultPromise;
		AsyncTask(ENamedThreads::GameThread, [WeakThis = TWeakPtr<FHotUpdateBaseVersionBuilder>(AsShared()), &SaveResultPromise]()
		{
			TSharedPtr<FHotUpdateBaseVersionBuilder> StrongThis = WeakThis.Pin();
			if (!StrongThis.IsValid())
			{
				SaveResultPromise.SetValue(false);
				return;
			}
			bool bSuccess = StrongThis->SaveResourceHashesInGameThread();
			SaveResultPromise.SetValue(bSuccess);
		});
		TFuture<bool> SaveFuture = SaveResultPromise.GetFuture();
		SaveFuture.WaitFor(FTimespan::FromSeconds(60));
		if (!SaveFuture.IsReady() || !SaveFuture.Get())
		{
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("保存资源Hash清单失败或超时，但构建成功"));
		}
	}

	// 6. 完成
	UpdateProgress(TEXT("构建完成"), 1.0f, TEXT("基础版本构建完成!"));

	Result.bSuccess = true;
	Result.ResourceHashPath = FPaths::Combine(
		FHotUpdateVersionManager::GetVersionDir(CurrentConfig.VersionString, CurrentConfig.Platform, CurrentConfig.AndroidTextureFormat),
		TEXT("manifest.json"));

	bIsBuilding = false;

	UE_LOG(LogHotUpdateEditor, Log, TEXT("基础版本构建成功:%s"), *Result.ExecutablePath);

	BroadcastBuildResult(Result);
}

FString FHotUpdateBaseVersionBuilder::GenerateUATCommand()
{
	// 获取引擎目录
	FString EngineDir = FPaths::EngineDir();
	FString BatchFilesDir = FPaths::Combine(EngineDir, TEXT("Build"), TEXT("BatchFiles"));

	// UE5 使用 RunUAT.bat 调用 AutomationTool.dll
	FString UATPath;
#if PLATFORM_WINDOWS
	UATPath = FPaths::Combine(BatchFilesDir, TEXT("RunUAT.bat"));
#else
	UATPath = FPaths::Combine(BatchFilesDir, TEXT("RunUAT.sh"));
#endif

	// 转换为绝对路径
	UATPath = FPaths::ConvertRelativePathToFull(UATPath);

	// 项目文件
	FString ProjectPath = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());

	// 输出目录
	FString OutputDir = CurrentConfig.OutputDirectory;
	if (OutputDir.IsEmpty())
	{
		OutputDir = GetDefaultOutputDirectory();
	}
	OutputDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(OutputDir, CurrentConfig.VersionString));

	// 构建配置
	FString BuildConfig;
	switch (CurrentConfig.BuildConfiguration)
	{
	case EHotUpdateBuildConfiguration::DebugGame:
		BuildConfig = TEXT("DebugGame");
		break;
	case EHotUpdateBuildConfiguration::Development:
		BuildConfig = TEXT("Development");
		break;
	case EHotUpdateBuildConfiguration::Shipping:
		BuildConfig = TEXT("Shipping");
		break;
	default:
		BuildConfig = TEXT("Development");
		break;
	}

	// 平台
	FString PlatformName = HotUpdateUtils::GetPlatformDirectoryName(CurrentConfig.Platform);

	// 构建参数部分
	FString Params;
	Params += FString::Printf(TEXT(" BuildCookRun"));
	Params += FString::Printf(TEXT(" -project=\"%s\""), *ProjectPath);
	Params += FString::Printf(TEXT(" -targetplatform=%s"), *PlatformName);
	Params += FString::Printf(TEXT(" -clientconfig=%s"), *BuildConfig);
	Params += FString::Printf(TEXT(" -archivedirectory=\"%s\""), *OutputDir);
	Params += TEXT(" -noP4");

	// 完整打包：Cook + Build + Stage + Pak + Archive（基础包必须编译，Stage 需要 receipt 文件）
	Params += TEXT(" -cook");
	Params += TEXT(" -build");
	Params += TEXT(" -stage");
	Params += TEXT(" -archive");
	Params += TEXT(" -pak");

	// 最小包模式：启用 Chunk 分离，由 StripExtraPakChunks.Automation.cs 后处理
	if (CurrentConfig.MinimalPackageConfig.bEnableMinimalPackage &&
		CurrentConfig.MinimalPackageConfig.WhitelistDirectories.Num() > 0)
	{
		Params += TEXT(" -generateChunks");
		Params += FString::Printf(TEXT(" -ScriptsForProject=\"%s\""), *ProjectPath);  // 加载项目的自动化脚本（StripExtraPakChunks）
		Params += TEXT(" -MinimalPackage");
		// 热更资源输出目录（pakchunk1+ 移到此目录，与 manifest 同级）
		// 传版本根目录（不含平台），StripExtraPakChunks 会将 staging 子目录结构保留
		FString HotUpdatePaksDir = FHotUpdateVersionManager::GetVersionDir(CurrentConfig.VersionString, CurrentConfig.Platform, CurrentConfig.AndroidTextureFormat) / TEXT("Paks");
		FPaths::NormalizeDirectoryName(HotUpdatePaksDir);
		Params += FString::Printf(TEXT(" -HotUpdateOutputDir=\"%s\""), *HotUpdatePaksDir);
			// 配置已在 ExecuteBuildInternal 中写入 MinimalPackageConfig.json
			// 白名单和 ChunkMapping 通过该配置文件传递给 Cook 进程，不需要通过 UAT 命令行传递

		UE_LOG(LogHotUpdateEditor, Log, TEXT("MinimalPackage mode: ScriptsForProject=%s, HotUpdateOutputDir=%s"), *ProjectPath, *HotUpdatePaksDir);
	}

	Params += TEXT(" -package");

	// Android 特定配置
	if (CurrentConfig.Platform == EHotUpdatePlatform::Android)
	{
		// 根据纹理格式配置 cookflavor
		FString TextureFormat;
		switch (CurrentConfig.AndroidTextureFormat)
		{
		case EHotUpdateAndroidTextureFormat::ETC2:
			TextureFormat = TEXT("ETC2");
			break;
		case EHotUpdateAndroidTextureFormat::ASTC:
			TextureFormat = TEXT("ASTC");
			break;
		case EHotUpdateAndroidTextureFormat::DXT:
			TextureFormat = TEXT("DXT");
			break;
		case EHotUpdateAndroidTextureFormat::Multi:
			// Multi 模式不传 cookflavor，UAT 根据项目设置自动处理多格式
				break;
		default:
			TextureFormat = TEXT("ETC2");
			break;
		}

		if (!TextureFormat.IsEmpty())
		{
			Params += FString::Printf(TEXT(" -cookflavor=%s"), *TextureFormat);
		}

		// 添加包名配置
		if (!CurrentConfig.AndroidPackageName.IsEmpty())
		{
			Params += FString::Printf(TEXT(" -packagename=%s"), *CurrentConfig.AndroidPackageName);
		}
	}

	// DebugGame 模式下生成调试信息
	if (CurrentConfig.BuildConfiguration == EHotUpdateBuildConfiguration::DebugGame)
	{
		Params += TEXT(" -Debug");
	}

	// Windows 下使用 cmd.exe 执行 RunUAT.bat
	// 格式: cmd.exe /c "path\to\RunUAT.bat BuildCookRun ..."
	FString Command;
#if PLATFORM_WINDOWS
	Command = FString::Printf(TEXT("cmd.exe /c \"%s %s\""), *UATPath, *Params);
#else
	Command = FString::Printf(TEXT("\"%s\"%s"), *UATPath, *Params);
#endif

	return Command;
}

void FHotUpdateBaseVersionBuilder::WriteMinimalPackageConfig()
{
	const FString ConfigFile = FPaths::ProjectIntermediateDir() / TEXT("MinimalPackageConfig.json");

	const TSharedPtr<FJsonObject> JsonObj = MakeShareable(new FJsonObject);
	JsonObj->SetBoolField(TEXT("bEnableMinimalPackage"), CurrentConfig.MinimalPackageConfig.bEnableMinimalPackage);

	// 构建 ChunkMapping：白名单资源 -> Chunk 0，热更资源 -> Chunk 1+
	const TSharedPtr<FJsonObject> MappingObj = MakeShareable(new FJsonObject);

	// 添加白名单资源（首包 Chunk 0）
	for (const FString& WhitelistAsset : CachedWhitelistAssetPaths)
	{
		MappingObj->SetNumberField(WhitelistAsset, 0);
	}

	// 添加热更资源（Chunk 1+）
	for (const TPair<FString, int32>& Pair : CachedChunkMapping)
	{
		MappingObj->SetNumberField(Pair.Key, Pair.Value);
	}

	if (MappingObj->Values.Num() > 0)
	{
		JsonObj->SetObjectField(TEXT("ChunkMapping"), MappingObj);
		UE_LOG(LogHotUpdateEditor, Log, TEXT("ChunkMapping: 白名单 %d 个(Chunk 0), 热更 %d 个(Chunk 1+)"),
			CachedWhitelistAssetPaths.Num(), CachedChunkMapping.Num());
	}

	FString JsonStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonStr);
	FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);

	if (FFileHelper::SaveStringToFile(JsonStr, *ConfigFile))
	{
		UE_LOG(LogHotUpdateEditor, Log, TEXT("Written minimal package config to: %s"), *ConfigFile);
		UE_LOG(LogHotUpdateEditor, Log, TEXT("  Content: %s"), *JsonStr);
	}
	else
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("Failed to write minimal package config to: %s"), *ConfigFile);
	}
}

void FHotUpdateBaseVersionBuilder::PreComputeChunkMapping()
{
	if (!CurrentConfig.MinimalPackageConfig.bEnableMinimalPackage)
		return;

	// 清空缓存
	CachedChunkMapping.Empty();
	CachedChunkDefinitions.Empty();

	// 直接使用预收集的热更资源列表（已在 BuildBaseVersion 主线程中完成白名单过滤）
	TArray<FString> PatchAssetPaths = CurrentConfig.PreCollectedPatchAssetPaths;

	UE_LOG(LogHotUpdateEditor, Log, TEXT("预计算 Chunk 分配，热更资源数: %d"), PatchAssetPaths.Num());

	// 策略为 None：所有热更资源分配到同一个 Chunk（ChunkIdStart，默认 1）
	if (CurrentConfig.MinimalPackageConfig.PatchChunkStrategy == EHotUpdateChunkStrategy::None)
	{
		const int32 DefaultChunkId = FMath::Max(1, CurrentConfig.MinimalPackageConfig.PatchChunkConfig.SizeBasedConfig.ChunkIdStart);
		for (const FString& AssetPath : PatchAssetPaths)
		{
			CachedChunkMapping.Add(AssetPath, DefaultChunkId);
		}
		UE_LOG(LogHotUpdateEditor, Log, TEXT("分包策略为 None，%d 个热更资源全部分配到 Chunk %d"), PatchAssetPaths.Num(), DefaultChunkId);
		return;
	}

	if (PatchAssetPaths.Num() == 0)
	{
		UE_LOG(LogHotUpdateEditor, Warning, TEXT("没有热更资产，跳过 Chunk 预计算"));
		return;
	}

	// 收集热更资产的磁盘路径映射（用于按大小分包等策略）
	TMap<FString, FString> AssetDiskPaths;
	for (const FString& AssetPath : PatchAssetPaths)
	{
		FString SourcePath = FHotUpdatePackageHelper::GetAssetSourcePath(AssetPath);
		if (!SourcePath.IsEmpty() && FPaths::FileExists(*SourcePath))
		{
			AssetDiskPaths.Add(AssetPath, SourcePath);
		}
	}

	// 配置 ChunkAnalysisConfig
	FHotUpdateChunkAnalysisConfig ChunkConfig = CurrentConfig.MinimalPackageConfig.PatchChunkConfig;
	ChunkConfig.ChunkStrategy = CurrentConfig.MinimalPackageConfig.PatchChunkStrategy;

	if (ChunkConfig.SizeBasedConfig.ChunkIdStart < 1)
	{
		ChunkConfig.SizeBasedConfig.ChunkIdStart = 1;
	}
	if (ChunkConfig.DefaultChunkId < 0)
	{
		ChunkConfig.DefaultChunkId = 1;  // 未匹配的资源默认分配到 Chunk 1
	}

	FHotUpdateChunkAnalysisResult Result = FHotUpdateChunkManager::AnalyzeAndCreateChunks(PatchAssetPaths, AssetDiskPaths, ChunkConfig);

	if (Result.bSuccess)
	{
		CachedChunkMapping = MoveTemp(Result.AssetToChunkMap);
		CachedChunkDefinitions = MoveTemp(Result.Chunks);

		UE_LOG(LogHotUpdateEditor, Log,
			TEXT("Chunk 分包预计算完成: 策略=%s, %d 个资产分为 %d 个 Chunk"),
			*UEnum::GetValueAsString(CurrentConfig.MinimalPackageConfig.PatchChunkStrategy),
			PatchAssetPaths.Num(), CachedChunkDefinitions.Num());

		for (const FHotUpdateChunkDefinition& Chunk : CachedChunkDefinitions)
		{
			UE_LOG(LogHotUpdateEditor, Log, TEXT("  Chunk %d (%s): %d 个资产"),
				Chunk.ChunkId, *Chunk.ChunkName, Chunk.AssetPaths.Num());
		}
	}
	else
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("Chunk 分包预计算失败: %s"), *Result.ErrorMessage);
		CachedChunkMapping.Empty();
		CachedChunkDefinitions.Empty();
	}
}

bool FHotUpdateBaseVersionBuilder::ExecuteUATPackage(const FString& UATCommand, FString& OutError) const
{
	// 解析命令行
	FString ExecutablePath;
	FString CommandLine;

	// Windows 下命令格式: cmd.exe /c "RunUAT.bat ..."
	// 其他平台: "RunUAT.sh" ...
#if PLATFORM_WINDOWS
	// 查找 cmd.exe
	int32 CmdExeEnd = UATCommand.Find(TEXT(" "), ESearchCase::CaseSensitive);
	if (CmdExeEnd != INDEX_NONE)
	{
		ExecutablePath = UATCommand.Left(CmdExeEnd);
		CommandLine = UATCommand.Mid(CmdExeEnd + 1);
	}
#else
	// 提取可执行文件路径和参数
	int32 FirstQuoteIndex = INDEX_NONE;
	if (UATCommand.FindChar('"', FirstQuoteIndex))
	{
		// 从第一个引号后开始查找第二个引号
		FString RemainingStr = UATCommand.Mid(FirstQuoteIndex + 1);
		int32 SecondQuoteRelative = INDEX_NONE;
		if (RemainingStr.FindChar('"', SecondQuoteRelative))
		{
			int32 SecondQuoteIndex = FirstQuoteIndex + 1 + SecondQuoteRelative;
			ExecutablePath = UATCommand.Mid(FirstQuoteIndex + 1, SecondQuoteIndex - FirstQuoteIndex - 1);
			CommandLine = UATCommand.RightChop(SecondQuoteIndex + 1);
		}
	}
#endif

	if (ExecutablePath.IsEmpty())
	{
		OutError = TEXT("无法解析 UAT 命令");
		return false;
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("执行: %s %s"), *ExecutablePath, *CommandLine);

	// 使用 FMonitoredProcess 执行命令，避免管道继承问题
	FMonitoredProcess Process(ExecutablePath, CommandLine, true);  // true = 隐藏窗口

	// 设置输出回调
	Process.OnOutput().BindLambda([](const FString& Output)
	{
		UE_LOG(LogHotUpdateEditor, Log, TEXT("%s"), *Output);
	});

	// 启动进程
	if (!Process.Launch())
	{
		OutError = TEXT("无法启动 UAT 进程");
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

	// 获取返回代码
	int32 ReturnCode = Process.GetReturnCode();

	if (bIsCancelled)
	{
		OutError = TEXT("构建已取消");
		return false;
	}

	if (ReturnCode != 0)
	{
		OutError = FString::Printf(TEXT("UAT 返回错误代码: %d"), ReturnCode);
		return false;
	}

	return true;
}

bool FHotUpdateBaseVersionBuilder::CheckBuildOutput(const FString& OutputDir, FString& OutExecutablePath) const
{
	IPlatformFile& PlatformFile = IPlatformFile::GetPlatformPhysical();

	if (!PlatformFile.DirectoryExists(*OutputDir))
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("输出目录不存在: %s"), *OutputDir);
		return false;
	}

	switch (CurrentConfig.Platform)
	{
	case EHotUpdatePlatform::Windows:
		{
			// Windows 输出在 OutputDir\Windows\ 目录下
			FString PlatformSubDir = FPaths::Combine(OutputDir, HotUpdateUtils::GetPlatformString(CurrentConfig.Platform));
			TArray<FString> FoundFiles;
			PlatformFile.FindFilesRecursively(FoundFiles, *PlatformSubDir, TEXT("exe"));

			if (FoundFiles.Num() > 0)
			{
				OutExecutablePath = FoundFiles[0];
				return true;
			}

			// 也检查直接在 OutputDir 下
			PlatformFile.FindFilesRecursively(FoundFiles, *OutputDir, TEXT("exe"));
			if (FoundFiles.Num() > 0)
			{
				OutExecutablePath = FoundFiles[0];
				return true;
			}
		}
		break;

	case EHotUpdatePlatform::Android:
		{
			// Android 输出 apk 文件
			TArray<FString> FoundFiles;
			PlatformFile.FindFilesRecursively(FoundFiles, *OutputDir, TEXT("apk"));

			if (FoundFiles.Num() > 0)
			{
				OutExecutablePath = FoundFiles[0];
				return true;
			}
		}
		break;

	default:
		break;
	}

	return false;
}

bool FHotUpdateBaseVersionBuilder::SaveResourceHashesInGameThread()
{
	// 1. 解析平台输出目录
	FString PlatformDir = ResolvePlatformOutputDir();
	if (PlatformDir.IsEmpty())
	{
		return false;
	}

	// 2. 收集 IoStore 容器文件信息
	FString VersionDir = FHotUpdateVersionManager::GetVersionDir(CurrentConfig.VersionString, CurrentConfig.Platform, CurrentConfig.AndroidTextureFormat);
	FPaths::NormalizeDirectoryName(VersionDir);
	IPlatformFile::GetPlatformPhysical().CreateDirectoryTree(*VersionDir);

	TArray<FHotUpdateContainerInfo> ContainerInfos;
	FString HotUpdatePaksDir = FPaths::Combine(VersionDir, TEXT("Paks"));
	CollectContainerFiles(HotUpdatePaksDir, VersionDir, EHotUpdateContainerType::Patch, ContainerInfos);

	UE_LOG(LogHotUpdateEditor, Log, TEXT("收集了 %d 个容器"), ContainerInfos.Num());
	UE_LOG(LogHotUpdateEditor, Log, TEXT("版本目录: %s"), *VersionDir);

	// 3. 生成并保存 manifest.json（同时输出共享的 VersionObject 和 ChunksArray）
	TSharedPtr<FJsonObject> VersionObject;
	TArray<TSharedPtr<FJsonValue>> ChunksArray;
	if (!BuildManifestJson(VersionDir, ContainerInfos, VersionObject, ChunksArray))
	{
		return false;
	}

	// 4. 使用缓存数据解析资源磁盘路径（避免重复收集）
	FString CookedPlatformDir = HotUpdateUtils::GetCookedPlatformDir(CurrentConfig.Platform, CurrentConfig.AndroidTextureFormat);
	TArray<FHotUpdateResolvedAssetInfo> BaseAssets;
	TArray<FHotUpdateResolvedAssetInfo> PatchAssets;

	// 最小包模式下已预收集数据，无需重复调用；整包模式在下方收集
	if (CurrentConfig.MinimalPackageConfig.bEnableMinimalPackage)
	{
		// 最小包模式：使用预收集的缓存数据
		BaseAssets = ResolveAssetInfo(CachedWhitelistAssetPaths, CookedPlatformDir);
		PatchAssets = ResolveAssetInfo(CurrentConfig.PreCollectedPatchAssetPaths, CookedPlatformDir);

		// 处理预收集的 Staged 文件
		for (const FString& StagedFile : CurrentConfig.PreCollectedNonAssetFiles)
		{
			if (FPaths::FileExists(*StagedFile))
			{
				int64 FileSize = IFileManager::Get().FileSize(*StagedFile);
				FString FileHash = UHotUpdateFileUtils::CalculateFileHash(StagedFile);
				// 使用虚拟路径（/Game/...）而非绝对路径，确保跨机器一致
				FString VirtualPath = FHotUpdatePackageHelper::FilePathToContentMountPath(StagedFile);
				if (VirtualPath.IsEmpty())
				{
					VirtualPath = FPaths::ConvertRelativePathToFull(StagedFile);
				}
				BaseAssets.Add(FHotUpdateResolvedAssetInfo(VirtualPath, FileHash, FileSize));
			}
			else
			{
				UE_LOG(LogHotUpdateEditor, Warning, TEXT("Staged 文件不存在: %s"), *StagedFile);
			}
		}
	}
	else
	{
		// 整包模式：需要收集所有资源
		FHotUpdatePackagingSettingsResult PackagingResult = FHotUpdatePackagingSettingsHelper::ParsePackagingSettings(true);
		BaseAssets = ResolveAssetInfo(PackagingResult.AssetPaths, CookedPlatformDir);
		// 处理 Staged 文件（非 UE 资产）
		for (const FString& StagedFile : PackagingResult.NonAssetPaths)
		{
			if (FPaths::FileExists(*StagedFile))
			{
				int64 FileSize = IFileManager::Get().FileSize(*StagedFile);
				FString FileHash = UHotUpdateFileUtils::CalculateFileHash(StagedFile);
				// 使用虚拟路径（/Game/...）而非绝对路径，确保跨机器一致
				FString VirtualPath = FHotUpdatePackageHelper::FilePathToContentMountPath(StagedFile);
				if (VirtualPath.IsEmpty())
				{
					VirtualPath = FPaths::ConvertRelativePathToFull(StagedFile);
				}
				BaseAssets.Add(FHotUpdateResolvedAssetInfo(VirtualPath, FileHash, FileSize));
			}
			else
			{
				UE_LOG(LogHotUpdateEditor, Warning, TEXT("Staged 文件不存在: %s"), *StagedFile);
			}
		}
	}
	
	UE_LOG(LogHotUpdateEditor, Log, TEXT("收集了 %d 个基础资源, %d 个热更资源用于差异计算"), BaseAssets.Num(), PatchAssets.Num());

	// 5. 生成并保存 filemanifest.json
	if (!BuildFileManifestJson(VersionDir, VersionObject, ChunksArray, BaseAssets, PatchAssets))
	{
		return false;
	}

	// 6. 注册版本信息
	FHotUpdateVersionManager VersionManager;
	FHotUpdateEditorVersionInfo VersionInfo;
	VersionInfo.VersionString = CurrentConfig.VersionString;
	VersionInfo.PackageKind = EHotUpdatePackageKind::Base;
	VersionInfo.Platform = CurrentConfig.Platform;
	VersionInfo.CreatedTime = FDateTime::Now();
	VersionInfo.FileManifestPath = FPaths::Combine(VersionDir, TEXT("filemanifest.json"));
	VersionInfo.AssetCount = BaseAssets.Num() + PatchAssets.Num();

	VersionManager.RegisterVersion(VersionInfo);

	UE_LOG(LogHotUpdateEditor, Log, TEXT("版本注册成功: %s"), *CurrentConfig.VersionString);

	return true;
}

FString FHotUpdateBaseVersionBuilder::ResolvePlatformOutputDir() const
{
	// 从打包输出目录获取容器文件列表
	FString OutputDir = CurrentConfig.OutputDirectory;
	if (OutputDir.IsEmpty())
	{
		OutputDir = GetDefaultOutputDirectory();
	}
	OutputDir = FPaths::Combine(OutputDir, CurrentConfig.VersionString);
	FPaths::NormalizeDirectoryName(OutputDir);

	// 根据平台获取 UAT 输出子目录名
	FString PlatformDir = FPaths::Combine(OutputDir, HotUpdateUtils::GetPlatformDirName(CurrentConfig.Platform, CurrentConfig.AndroidTextureFormat));

	if (!IPlatformFile::GetPlatformPhysical().DirectoryExists(*PlatformDir))
	{
		UE_LOG(LogHotUpdateEditor, Warning, TEXT("打包输出目录不存在: %s"), *PlatformDir);
		return FString();
	}

	return PlatformDir;
}

void FHotUpdateBaseVersionBuilder::CollectContainerFiles(
	const FString& SearchDir,
	const FString& BaseDir,
	EHotUpdateContainerType ContainerType,
	TArray<FHotUpdateContainerInfo>& OutContainerInfos)
{
	IPlatformFile& PlatformFile = IPlatformFile::GetPlatformPhysical();

	if (!PlatformFile.DirectoryExists(*SearchDir))
	{
		return;
	}

	TArray<FString> UtocFiles;
	PlatformFile.FindFilesRecursively(UtocFiles, *SearchDir, TEXT("utoc"));
	for (const FString& UtocFile : UtocFiles)
	{
		FHotUpdateContainerInfo ContainerInfo;
		ContainerInfo.ContainerName = FPaths::GetBaseFilename(UtocFile);
		ContainerInfo.UtocFile.Path = UtocFile.RightChop(BaseDir.Len() + 1);
		ContainerInfo.UtocFile.Size = IFileManager::Get().FileSize(*UtocFile);
		ContainerInfo.UtocFile.Hash = UHotUpdateFileUtils::CalculateFileHash(UtocFile);
		ContainerInfo.ContainerType = ContainerType;

		FString UcasFile = UtocFile.Replace(TEXT(".utoc"), TEXT(".ucas"));
		if (PlatformFile.FileExists(*UcasFile))
		{
			ContainerInfo.UcasFile.Path = UcasFile.RightChop(BaseDir.Len() + 1);
			ContainerInfo.UcasFile.Size = IFileManager::Get().FileSize(*UcasFile);
			ContainerInfo.UcasFile.Hash = UHotUpdateFileUtils::CalculateFileHash(UcasFile);
		}

		OutContainerInfos.Add(ContainerInfo);
	}

	TArray<FString> PakFiles;
	PlatformFile.FindFilesRecursively(PakFiles, *SearchDir, TEXT("pak"));
	for (const FString& PakFile : PakFiles)
	{
		bool bAlreadyCollected = false;
		for (const FString& UtocFile : UtocFiles)
		{
			if (FPaths::GetBaseFilename(UtocFile) == FPaths::GetBaseFilename(PakFile))
			{
				bAlreadyCollected = true;
				break;
			}
		}
		if (bAlreadyCollected)
		{
			continue;
		}

		FHotUpdateContainerInfo ContainerInfo;
		ContainerInfo.ContainerName = FPaths::GetBaseFilename(PakFile);
		ContainerInfo.PakFile.Path = PakFile.RightChop(BaseDir.Len() + 1);
		ContainerInfo.PakFile.Size = IFileManager::Get().FileSize(*PakFile);
		ContainerInfo.PakFile.Hash = UHotUpdateFileUtils::CalculateFileHash(PakFile);
		ContainerInfo.ContainerType = ContainerType;

		OutContainerInfos.Add(ContainerInfo);
	}
}

bool FHotUpdateBaseVersionBuilder::BuildManifestJson(
	const FString& VersionDir,
	const TArray<FHotUpdateContainerInfo>& ContainerInfos,
	TSharedPtr<FJsonObject>& OutVersionObject,
	TArray<TSharedPtr<FJsonValue>>& OutChunksArray) const
{
	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);

	// 版本信息
	OutVersionObject = MakeShareable(new FJsonObject);
	OutVersionObject->SetStringField(TEXT("version"), CurrentConfig.VersionString);
	OutVersionObject->SetStringField(TEXT("platform"), HotUpdateUtils::GetPlatformString(CurrentConfig.Platform));
	OutVersionObject->SetNumberField(TEXT("timestamp"), FDateTime::Now().ToUnixTimestamp());

	RootObject->SetObjectField(TEXT("version"), OutVersionObject);

	// chunks 数组
	for (const FHotUpdateContainerInfo& Container : ContainerInfos)
	{
		TSharedPtr<FJsonObject> ChunkObject = MakeShareable(new FJsonObject);
		ChunkObject->SetStringField(TEXT("containerName"), Container.ContainerName);
		ChunkObject->SetStringField(TEXT("containerType"),
			Container.ContainerType == EHotUpdateContainerType::Base ? TEXT("base") : TEXT("patch"));
		ChunkObject->SetStringField(TEXT("version"), CurrentConfig.VersionString);

		// IoStore 格式字段
		if (!Container.UtocFile.Path.IsEmpty())
		{
			ChunkObject->SetStringField(TEXT("utocPath"), Container.UtocFile.Path);
			ChunkObject->SetNumberField(TEXT("utocSize"), Container.UtocFile.Size);
			ChunkObject->SetStringField(TEXT("utocHash"), Container.UtocFile.Hash);
		}
		if (!Container.UcasFile.Path.IsEmpty())
		{
			ChunkObject->SetStringField(TEXT("ucasPath"), Container.UcasFile.Path);
			ChunkObject->SetNumberField(TEXT("ucasSize"), Container.UcasFile.Size);
			ChunkObject->SetStringField(TEXT("ucasHash"), Container.UcasFile.Hash);
		}

		// 传统 Pak 格式字段
		if (!Container.PakFile.Path.IsEmpty())
		{
			ChunkObject->SetStringField(TEXT("pakPath"), Container.PakFile.Path);
			ChunkObject->SetNumberField(TEXT("pakSize"), Container.PakFile.Size);
			ChunkObject->SetStringField(TEXT("pakHash"), Container.PakFile.Hash);
		}

		OutChunksArray.Add(MakeShareable(new FJsonValueObject(ChunkObject)));
	}
	RootObject->SetArrayField(TEXT("containers"), OutChunksArray);

	// 序列化 manifest
	FString ManifestOutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ManifestOutputString);
	FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

	FString ManifestFilePath = FPaths::Combine(VersionDir, TEXT("manifest.json"));
	if (!FFileHelper::SaveStringToFile(ManifestOutputString, *ManifestFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("保存 Manifest 失败: %s"), *ManifestFilePath);
		return false;
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("Manifest 保存成功: %s"), *ManifestFilePath);
	return true;
}

TArray<FHotUpdateResolvedAssetInfo> FHotUpdateBaseVersionBuilder::ResolveAssetInfo(const TArray<FString>& AssetPaths, const FString& CookedPlatformDir)
{
	TArray<FHotUpdateResolvedAssetInfo> Result;
	Result.Reserve(AssetPaths.Num());

	for (const FString& OriginalAssetPath : AssetPaths)
	{
		// 获取源文件路径
		FString SourcePath = FHotUpdatePackageHelper::GetAssetSourcePath(OriginalAssetPath);

		if (SourcePath.IsEmpty() || !FPaths::FileExists(*SourcePath))
		{
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("源文件不存在，跳过: %s"), *OriginalAssetPath);
			continue;
		}

		// filePath 使用原始资产路径（/Game/... 或 /Engine/...），确保跨机器一致
		int64 FileSize = IFileManager::Get().FileSize(*SourcePath);
		FString FileHash = UHotUpdateFileUtils::CalculateFileHash(SourcePath);

		Result.Emplace(OriginalAssetPath, FileHash, FileSize);
	}

	return Result;
}

bool FHotUpdateBaseVersionBuilder::BuildFileManifestJson(
	const FString& VersionDir,
	const TSharedPtr<FJsonObject>& VersionObject,
	const TArray<TSharedPtr<FJsonValue>>& ChunksArray,
	const TArray<FHotUpdateResolvedAssetInfo>& BaseAssets,
	const TArray<FHotUpdateResolvedAssetInfo>& PatchAssets)
{
	const TSharedPtr<FJsonObject> FileManifestObj = MakeShareable(new FJsonObject);
	FileManifestObj->SetObjectField(TEXT("version"), VersionObject);
	FileManifestObj->SetArrayField(TEXT("containers"), ChunksArray);

	// 生成文件条目的通用 lambda（消除基础/热更资源的重复代码）
	TArray<TSharedPtr<FJsonValue>> FilesArray;
	auto AddFileEntries = [&FilesArray](const TArray<FHotUpdateResolvedAssetInfo>& Assets, const FString& Source)
	{
		for (const FHotUpdateResolvedAssetInfo& Info : Assets)
		{
			TSharedPtr<FJsonObject> FileObj = MakeShareable(new FJsonObject);
			FileObj->SetStringField(TEXT("filePath"), Info.AssetPath);

			FileObj->SetNumberField(TEXT("fileSize"), Info.FileSize);
			FileObj->SetStringField(TEXT("fileHash"), Info.FileHash);

			FileObj->SetStringField(TEXT("source"), Source);

			FilesArray.Add(MakeShareable(new FJsonValueObject(FileObj)));
		}
	};

	// 基础资源 -> chunk0 base
	AddFileEntries(BaseAssets, TEXT("base"));
	// 热更资源
	AddFileEntries(PatchAssets, TEXT("patch"));

	FileManifestObj->SetArrayField(TEXT("files"), FilesArray);

	// 序列化 filemanifest
	FString FileManifestString;
	TSharedRef<TJsonWriter<>> FileWriter = TJsonWriterFactory<>::Create(&FileManifestString);
	FJsonSerializer::Serialize(FileManifestObj.ToSharedRef(), FileWriter);

	const FString FileManifestPath = FPaths::Combine(VersionDir, TEXT("filemanifest.json"));
	if (!FFileHelper::SaveStringToFile(FileManifestString, *FileManifestPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("保存 FileManifest 失败: %s"), *FileManifestPath);
		return false;
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("FileManifest 保存成功: %s，包含 %d 个资源（%d 基础 + %d 热更）"),
		*FileManifestPath, BaseAssets.Num() + PatchAssets.Num(), BaseAssets.Num(), PatchAssets.Num());

	return true;
}

void FHotUpdateBaseVersionBuilder::UpdateProgress(const FString& Stage, float Percent, const FString& Message)
{
	FHotUpdateBaseVersionBuildProgress Progress;
	Progress.CurrentStage = Stage;
	Progress.ProgressPercent = Percent;
	Progress.StatusMessage = Message;

	if (CurrentConfig.bSynchronousMode)
	{
		OnBuildProgress.Broadcast(Progress);
	}
	else
	{
		HotUpdateProgressHelper::BroadcastBaseBuildProgressAsync(Progress, OnBuildProgress);
	}
}

void FHotUpdateBaseVersionBuilder::BroadcastBuildResult(const FHotUpdateBaseVersionBuildResult& Result)
{
	if (CurrentConfig.bSynchronousMode)
	{
		OnBuildComplete.Broadcast(Result);
	}
	else
	{
		AsyncTask(ENamedThreads::GameThread, [WeakThis = TWeakPtr<FHotUpdateBaseVersionBuilder>(AsShared()), Result]()
		{
			TSharedPtr<FHotUpdateBaseVersionBuilder> StrongThis = WeakThis.Pin();
			if (!StrongThis.IsValid()) return;
			StrongThis->OnBuildComplete.Broadcast(Result);
		});
	}
}

FHotUpdateBaseVersionBuildResult FHotUpdateBaseVersionBuilder::MakeErrorResult(const FString& ErrorMessage)
{
	FHotUpdateBaseVersionBuildResult Result;
	Result.bSuccess = false;
	Result.ErrorMessage = ErrorMessage;
	return Result;
}


