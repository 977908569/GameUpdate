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
#include "Core/HotUpdateSettings.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/App.h"
#include "Misc/SecureHash.h"
#include "Misc/MonitoredProcess.h"
#include "JsonObjectConverter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Settings/ProjectPackagingSettings.h"
#include "Misc/PackageName.h"
#include "Interfaces/IPluginManager.h"

/**
 * 资源解析结果（内部使用，用于在 SaveResourceHashesInGameThread 的各步骤间传递数据）
 * 替代原来的 TMap<FString, FString>，每个资源只调用一次 GetAssetDiskPath，
 * 在构造时直接派生 FileName（等价于 ConvertAssetPathToFileName）和 SourcePath
 */
struct FHotUpdateResolvedAssetInfo
{
	/** 资源路径（如 /Game/Maps/MainMenu） */
	FString AssetPath;
	/** Cooked 磁盘路径（如 D:/Project/Saved/Cooked/Windows/Game/Maps/MainMenu.umap） */
	FString DiskPath;
	/** 文件名（如 Game/Maps/MainMenu.umap），从 AssetPath 去掉前导 / + DiskPath 的扩展名派生 */
	FString FileName;
	/** 源文件路径（Content 目录下的 .uasset/.umap），用于 Hash 计算 */
	FString SourcePath;

	FHotUpdateResolvedAssetInfo() = default;

	FHotUpdateResolvedAssetInfo(const FString& InAssetPath, const FString& InDiskPath)
		: AssetPath(InAssetPath)
		, DiskPath(InDiskPath)
	{
		// 派生 FileName：去掉 AssetPath 前导 /，再附加 DiskPath 的扩展名
		// 等价于 ConvertAssetPathToFileName 的结果，但无需再次调用 GetAssetDiskPath
		FileName = AssetPath;
		if (FileName.StartsWith(TEXT("/")))
		{
			FileName.RightChopInline(1);
		}
		FString Extension = FPaths::GetExtension(DiskPath);
		if (!Extension.IsEmpty())
		{
			FileName += TEXT(".") + Extension;
		}
		else
		{
			FileName += TEXT(".uasset");
		}

		// 派生 SourcePath
		SourcePath = FHotUpdatePackageHelper::GetAssetSourcePath(AssetPath);
	}

	/**
	 * 从 Staged 文件构建（非 UE 资源，如 DirectoriesToAlwaysStageAsUFS/NonUFS 中的文件）
	 * @param InPakPath pak 内路径（如 GameUpdate/Content/Setting/ui.txt），也用作 filemanifest.json 中的 filePath
	 * @param InSourcePath 源文件磁盘路径（如 D:/Project/Content/Setting/ui.txt），用于 Hash 计算
	 */
	static FHotUpdateResolvedAssetInfo FromStagedFile(const FString& InPakPath, const FString& InSourcePath)
	{
		FHotUpdateResolvedAssetInfo Info;
		Info.AssetPath = InPakPath;
		Info.DiskPath = InSourcePath;
		Info.FileName = InPakPath;
		Info.SourcePath = InSourcePath;
		return Info;
	}

	/** 获取用于 Hash 计算的路径（优先 SourcePath，回退 DiskPath） */
	FString GetHashPath() const
	{
		return (!SourcePath.IsEmpty() && FPaths::FileExists(*SourcePath)) ? SourcePath : DiskPath;
	}
};

UHotUpdateBaseVersionBuilder::UHotUpdateBaseVersionBuilder()
	: bIsBuilding(false)
	, bIsCancelled(false)
{
}

FString UHotUpdateBaseVersionBuilder::GetDefaultOutputDirectory()
{
	return FPaths::ProjectSavedDir() / TEXT("BaseVersionBuilds");
}


void UHotUpdateBaseVersionBuilder::BuildBaseVersion(const FHotUpdateBaseVersionBuildConfig& Config)
{
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
	for (const FDirectoryPath& Dir : Config.MinimalPackageConfig.WhitelistDirectories)
	{
		if (Dir.Path.GetCharArray().GetData() == nullptr)
		{
			UE_LOG(LogHotUpdateEditor, Error, TEXT("WhitelistDirectories 包含无效的 FDirectoryPath"));
			FHotUpdateBaseVersionBuildResult Result;
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("白名单目录配置包含无效数据");
			OnBuildComplete.Broadcast(Result);
			return;
		}
	}

	bIsBuilding = true;
	bIsCancelled = false;

	// 在游戏线程上深拷贝配置，确保字符串/数组数据的内存对后台线程可见
	// 直接赋值 CurrentConfig 后在后台线程读取可能导致 FString 内部缓冲区的线程可见性问题
	FHotUpdateBaseVersionBuildConfig SafeConfig = Config;

	if (SafeConfig.bSynchronousMode)
	{
		CurrentConfig = SafeConfig;
		ExecuteBuildInternal(true);
	}
	else
	{
		// 在后台线程执行构建（编辑器模式）
		// 通过 lambda 按值捕获 SafeConfig，确保后台线程使用独立的数据副本
		BuildTask = Async(EAsyncExecution::Thread, [this, SafeConfig]()
		{
			CurrentConfig = SafeConfig;
			ExecuteBuildInternal(false);
		});
	}
}

void UHotUpdateBaseVersionBuilder::CancelBuild()
{
	bIsCancelled = true;
}

void UHotUpdateBaseVersionBuilder::ExecuteBuildInternal(bool bSynchronous)
{
	// 清空上次构建的缓存
	CachedChunkMapping.Empty();
	CachedChunkDefinitions.Empty();

	FHotUpdateBaseVersionBuildResult Result;
	Result.VersionString = CurrentConfig.VersionString;
	Result.Platform = CurrentConfig.Platform;

	UE_LOG(LogHotUpdateEditor, Log, TEXT("开始构建基础版本（%s）: %s, 平台: %s"),
		bSynchronous ? TEXT("同步") : TEXT("异步"),
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
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("构建已取消");
		bIsBuilding = false;
		if (bSynchronous)
		{
			OnBuildComplete.Broadcast(Result);
		}
		else
		{
			AsyncTask(ENamedThreads::GameThread, [this, Result]()
			{
				OnBuildComplete.Broadcast(Result);
			});
		}
		return;
	}

	if (!bPackageSuccess)
	{
		Result.bSuccess = false;
		Result.ErrorMessage = FString::Printf(TEXT("项目打包失败: %s"), *ErrorMsg);
		bIsBuilding = false;
		if (bSynchronous)
		{
			OnBuildComplete.Broadcast(Result);
		}
		else
		{
			AsyncTask(ENamedThreads::GameThread, [this, Result]()
			{
				OnBuildComplete.Broadcast(Result);
			});
		}
		return;
	}

	// 4. 检查输出
	UpdateProgress(TEXT("检查构建输出"), 0.85f, TEXT("正在检查构建输出..."));

	if (!CheckBuildOutput(OutputDir, Result.ExecutablePath))
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("未找到构建输出文件");
		bIsBuilding = false;
		if (bSynchronous)
		{
			OnBuildComplete.Broadcast(Result);
		}
		else
		{
			AsyncTask(ENamedThreads::GameThread, [this, Result]()
			{
				OnBuildComplete.Broadcast(Result);
			});
		}
		return;
	}

	// 5. 保存资源 Hash 清单
	UpdateProgress(TEXT("保存资源清单"), 0.9f, TEXT("正在保存资源Hash清单..."));

	if (bSynchronous)
	{
		if (!SaveResourceHashesInGameThread())
		{
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("保存资源Hash清单失败，但构建成功"));
		}
	}
	else
	{
		TPromise<bool> SaveResultPromise;
		AsyncTask(ENamedThreads::GameThread, [this, &SaveResultPromise]()
		{
			bool bSuccess = SaveResourceHashesInGameThread();
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
		UHotUpdateVersionManager::GetVersionDir(CurrentConfig.VersionString, CurrentConfig.Platform, CurrentConfig.AndroidTextureFormat),
		TEXT("resources_hash.json"));

	bIsBuilding = false;

	UE_LOG(LogHotUpdateEditor, Log, TEXT("基础版本构建成功:%s"), *Result.ExecutablePath);

	if (bSynchronous)
	{
		OnBuildComplete.Broadcast(Result);
	}
	else
	{
		AsyncTask(ENamedThreads::GameThread, [this, Result]()
		{
			OnBuildComplete.Broadcast(Result);
		});
	}
}

FString UHotUpdateBaseVersionBuilder::GenerateUATCommand()
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

	// 完整打包：Cook + Stage + Pak + Archive
	Params += TEXT(" -cook");
	if (!CurrentConfig.bSkipBuild)
	{
		Params += TEXT(" -build");
	}
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
		FString HotUpdatePaksDir = UHotUpdateVersionManager::GetVersionDir(CurrentConfig.VersionString, CurrentConfig.Platform, CurrentConfig.AndroidTextureFormat) / TEXT("Paks");
		FPaths::NormalizeDirectoryName(HotUpdatePaksDir);
		Params += FString::Printf(TEXT(" -HotUpdateOutputDir=\"%s\""), *HotUpdatePaksDir);
		// Write config to temp file for cooking process to read
		// 白名单通过 MinimalPackageConfig.json 传递给 Cook 进程，不需要通过 UAT 命令行传递
		WriteMinimalPackageConfig();

		UE_LOG(LogHotUpdateEditor, Log, TEXT("MinimalPackage mode: ScriptsForProject=%s, HotUpdateOutputDir=%s"), *ProjectPath, *HotUpdatePaksDir);
	}

	if (CurrentConfig.bPackageAll)
	{
		Params += TEXT(" -package");
		}

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
	Command = FString::Printf(
		TEXT("cmd.exe /c \"%s %s\""),
		*UATPath,
		*Params
	);
#else
	Command = FString::Printf(
		TEXT("\"%s\"%s"),
		*UATPath,
		*Params
	);
#endif

	return Command;
}


void UHotUpdateBaseVersionBuilder::WriteMinimalPackageConfig()
{
	FString ConfigFile = FPaths::ProjectIntermediateDir() / TEXT("MinimalPackageConfig.json");

	TSharedPtr<FJsonObject> JsonObj = MakeShareable(new FJsonObject);
	JsonObj->SetBoolField(TEXT("bEnableMinimalPackage"), CurrentConfig.MinimalPackageConfig.bEnableMinimalPackage);

	TArray<TSharedPtr<FJsonValue>> WhitelistArray;
	for (const FDirectoryPath& Dir : CurrentConfig.MinimalPackageConfig.WhitelistDirectories)
	{
		WhitelistArray.Add(MakeShareable(new FJsonValueString(Dir.Path)));
	}
	JsonObj->SetArrayField(TEXT("WhitelistDirectories"), WhitelistArray);

	// 写入分包策略名称
	JsonObj->SetStringField(TEXT("ChunkStrategy"), UEnum::GetValueAsString(CurrentConfig.MinimalPackageConfig.PatchChunkStrategy));

	// 写入预计算的 ChunkMapping（如果已预计算）
	if (CachedChunkMapping.Num() > 0)
	{
		TSharedPtr<FJsonObject> MappingObj = MakeShareable(new FJsonObject);
		for (const TPair<FString, int32>& Pair : CachedChunkMapping)
		{
			MappingObj->SetNumberField(Pair.Key, Pair.Value);
		}
		JsonObj->SetObjectField(TEXT("ChunkMapping"), MappingObj);
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

void UHotUpdateBaseVersionBuilder::PreComputeChunkMapping()
{
	if (!CurrentConfig.MinimalPackageConfig.bEnableMinimalPackage)
		return;

	// 如果策略为 None，保持当前行为（全部 -> Chunk 11）
	if (CurrentConfig.MinimalPackageConfig.PatchChunkStrategy == EHotUpdateChunkStrategy::None)
	{
		CachedChunkMapping.Empty();
		CachedChunkDefinitions.Empty();
		UE_LOG(LogHotUpdateEditor, Log, TEXT("分包策略为 None，保持原有行为（非白名单资源全部分配到 Chunk 11）"));
		return;
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("开始预计算 Chunk 分配，策略: %s"),
		*UEnum::GetValueAsString(CurrentConfig.MinimalPackageConfig.PatchChunkStrategy));

	// 1. 获取 AssetRegistry
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry* AssetRegistry = &AssetRegistryModule.Get();

	// 2. 收集所有资产路径
	TArray<FString> AllAssetPaths = CollectAllAssetPaths(AssetRegistry);

	// 3. 应用最小包过滤（得到白名单和热更资产）
	TArray<FString> PatchAssetPaths;
	ApplyMinimalPackageFilter(AllAssetPaths, PatchAssetPaths, AssetRegistry);

	if (PatchAssetPaths.Num() == 0)
	{
		UE_LOG(LogHotUpdateEditor, Warning, TEXT("没有热更资产，跳过 Chunk 预计算"));
		return;
	}

	// 4. 收集热更资产的磁盘路径映射（用于按大小分包等策略）
	TMap<FString, FString> AssetDiskPaths;
	for (const FString& AssetPath : PatchAssetPaths)
	{
		FString SourcePath = FHotUpdatePackageHelper::GetAssetSourcePath(AssetPath);
		if (!SourcePath.IsEmpty() && FPaths::FileExists(*SourcePath))
		{
			AssetDiskPaths.Add(AssetPath, SourcePath);
		}
	}

	// 5. 配置 ChunkAnalysisConfig
	FHotUpdateChunkAnalysisConfig ChunkConfig = CurrentConfig.MinimalPackageConfig.PatchChunkConfig;
	ChunkConfig.ChunkStrategy = CurrentConfig.MinimalPackageConfig.PatchChunkStrategy;
	ChunkConfig.BaseChunkIdStart = 1;   // 非白名单资源从 Chunk 1 开始
	ChunkConfig.PatchChunkIdStart = 1;
	if (ChunkConfig.DefaultChunkId < 0)
	{
		ChunkConfig.DefaultChunkId = 11;  // 未匹配的资源默认分配到 Chunk 11
	}

	// 6. 使用 UHotUpdateChunkManager 执行分包分析
	UHotUpdateChunkManager* ChunkManager = NewObject<UHotUpdateChunkManager>();
	FHotUpdateChunkAnalysisResult Result = ChunkManager->AnalyzeAndCreateChunks(
		PatchAssetPaths, AssetDiskPaths, ChunkConfig);

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

bool UHotUpdateBaseVersionBuilder::ExecuteUATPackage(const FString& UATCommand, FString& OutError)
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
	Process.OnOutput().BindLambda([this](const FString& Output)
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

bool UHotUpdateBaseVersionBuilder::CheckBuildOutput(const FString& OutputDir, FString& OutExecutablePath)
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

bool UHotUpdateBaseVersionBuilder::SaveResourceHashesInGameThread()
{
	// 1. 解析平台输出目录
	FString PlatformDir = ResolvePlatformOutputDir();
	if (PlatformDir.IsEmpty())
	{
		return false;
	}

	// 2. 收集 IoStore 容器文件信息
	FString VersionDir = UHotUpdateVersionManager::GetVersionDir(CurrentConfig.VersionString, CurrentConfig.Platform, CurrentConfig.AndroidTextureFormat);
	FPaths::NormalizeDirectoryName(VersionDir);
	IPlatformFile::GetPlatformPhysical().CreateDirectoryTree(*VersionDir);

	TArray<FHotUpdateContainerInfo> ContainerInfos = CollectContainerInfos(PlatformDir, VersionDir);

	// 3. 生成并保存 manifest.json（同时输出共享的 VersionObject 和 ChunksArray）
	TSharedPtr<FJsonObject> VersionObject;
	TArray<TSharedPtr<FJsonValue>> ChunksArray;
	if (!BuildManifestJson(VersionDir, ContainerInfos, VersionObject, ChunksArray))
	{
		return false;
	}

	// 4. 从 AssetRegistry 收集资源路径
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry* AssetRegistry = &AssetRegistryModule.Get();
	TArray<FString> AssetPaths = CollectAllAssetPaths(AssetRegistry);

	// 5. 应用最小包过滤（拆分白名单 / 热更资源）
	TArray<FString> PatchAssetPaths;
	ApplyMinimalPackageFilter(AssetPaths, PatchAssetPaths, AssetRegistry);

	// 6. 解析资源磁盘路径（每个资源仅调用一次 GetAssetDiskPath，消除冗余 I/O）
	FString CookedPlatformDir = HotUpdateUtils::GetCookedPlatformDir(CurrentConfig.Platform, CurrentConfig.AndroidTextureFormat);
	TArray<FHotUpdateResolvedAssetInfo> BaseAssets = ResolveAssetInfo(AssetPaths, CookedPlatformDir);
	TArray<FHotUpdateResolvedAssetInfo> PatchAssets = ResolveAssetInfo(PatchAssetPaths, CookedPlatformDir);

	// 6.5. 收集 DirectoriesToAlwaysStageAsUFS/NonUFS 中的 Staged 文件
	TArray<FString> StagedPakPaths = CollectStagedFilePaths();
	for (const FString& PakPath : StagedPakPaths)
	{
		FString RelativePath = PakPath.RightChop(5); // 去掉 "Game/"
		FString SourcePath = FPaths::ProjectContentDir() / RelativePath;

		if (FPaths::FileExists(*SourcePath))
		{
			BaseAssets.Add(FHotUpdateResolvedAssetInfo::FromStagedFile(PakPath, SourcePath));
		}
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("收集了 %d 个基础资源, %d 个热更资源用于差异计算"),
		BaseAssets.Num(), PatchAssets.Num());

	// 7. 生成并保存 filemanifest.json
	if (!BuildFileManifestJson(VersionDir, VersionObject, ChunksArray, BaseAssets, PatchAssets))
	{
		return false;
	}

	// 8. 注册版本信息
	UHotUpdateVersionManager* VersionManager = NewObject<UHotUpdateVersionManager>();
	FHotUpdateEditorVersionInfo VersionInfo;
	VersionInfo.VersionString = CurrentConfig.VersionString;
	VersionInfo.PackageKind = EHotUpdatePackageKind::Base;
	VersionInfo.Platform = CurrentConfig.Platform;
	VersionInfo.CreatedTime = FDateTime::Now();
	VersionInfo.ManifestPath = FPaths::Combine(VersionDir, TEXT("manifest.json"));
	VersionInfo.AssetCount = BaseAssets.Num() + PatchAssets.Num();

	VersionManager->RegisterVersion(VersionInfo);

	UE_LOG(LogHotUpdateEditor, Log, TEXT("版本注册成功: %s"), *CurrentConfig.VersionString);

	return true;
}

FString UHotUpdateBaseVersionBuilder::ResolvePlatformOutputDir() const
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

TArray<FHotUpdateContainerInfo> UHotUpdateBaseVersionBuilder::CollectContainerInfos(
	const FString& PlatformDir, const FString& VersionDir) const
{
	TArray<FHotUpdateContainerInfo> ContainerInfos;
	IPlatformFile& PlatformFile = IPlatformFile::GetPlatformPhysical();

	// 收集指定目录下的容器文件
	// BaseDir: 计算相对路径的基准目录
	auto CollectContainers = [&](const FString& SearchDir, const FString& BaseDir, EHotUpdateContainerType InContainerType)
	{
		if (!PlatformFile.DirectoryExists(*SearchDir))
		{
			return;
		}

		// 从文件名解析 chunkId（pakchunk{n} → n，其他 → -1）
		auto ParseChunkId = [](const FString& Filename) -> int32
		{
			if (Filename.StartsWith(TEXT("pakchunk")))
			{
				FString NumStr;
				for (int32 i = 8; i < Filename.Len() && FChar::IsDigit(Filename[i]); ++i)
				{
					NumStr += Filename[i];
				}
				return NumStr.IsEmpty() ? -1 : FCString::Atoi(*NumStr);
			}
			return -1;
		};

		TArray<FString> UtocFiles;
		PlatformFile.FindFilesRecursively(UtocFiles, *SearchDir, TEXT("utoc"));
		for (const FString& UtocFile : UtocFiles)
		{
			FHotUpdateContainerInfo ContainerInfo;
			ContainerInfo.ContainerName = FPaths::GetBaseFilename(UtocFile);
			ContainerInfo.UtocPath = UtocFile.RightChop(BaseDir.Len() + 1);
			ContainerInfo.UtocSize = IFileManager::Get().FileSize(*UtocFile);
			ContainerInfo.UtocHash = UHotUpdateFileUtils::CalculateFileHash(UtocFile);
			ContainerInfo.ContainerType = InContainerType;
			ContainerInfo.ChunkId = ParseChunkId(ContainerInfo.ContainerName);

			// 查找对应的 ucas 文件
			FString UcasFile = UtocFile.Replace(TEXT(".utoc"), TEXT(".ucas"));
			if (PlatformFile.FileExists(*UcasFile))
			{
				ContainerInfo.UcasPath = UcasFile.RightChop(BaseDir.Len() + 1);
				ContainerInfo.UcasSize = IFileManager::Get().FileSize(*UcasFile);
				ContainerInfo.UcasHash = UHotUpdateFileUtils::CalculateFileHash(UcasFile);
			}

			ContainerInfos.Add(ContainerInfo);
		}
	};

	// 遍历 Content/Paks 目录（首包资源：pakchunk0, global）
	FString PaksDir = FPaths::Combine(PlatformDir, FApp::GetProjectName(), TEXT("Content"), TEXT("Paks"));
	CollectContainers(PaksDir, PlatformDir, EHotUpdateContainerType::Base);

	// 遍历热更资源目录（StripExtraPakChunks 移出的 pakchunk1+）
	FString HotUpdatePaksDir = FPaths::Combine(VersionDir, TEXT("Paks"));
	CollectContainers(HotUpdatePaksDir, VersionDir, EHotUpdateContainerType::Patch);

	UE_LOG(LogHotUpdateEditor, Log, TEXT("收集了 %d 个容器"), ContainerInfos.Num());
	UE_LOG(LogHotUpdateEditor, Log, TEXT("版本目录: %s"), *VersionDir);

	return ContainerInfos;
}

bool UHotUpdateBaseVersionBuilder::BuildManifestJson(
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
		ChunkObject->SetStringField(TEXT("ChunkName"), Container.ContainerName);
		ChunkObject->SetStringField(TEXT("containerType"),
			Container.ContainerType == EHotUpdateContainerType::Base ? TEXT("base") : TEXT("patch"));
		ChunkObject->SetNumberField(TEXT("chunkId"), Container.ChunkId);
		ChunkObject->SetStringField(TEXT("utocPath"), Container.UtocPath);
		ChunkObject->SetNumberField(TEXT("utocSize"), Container.UtocSize);
		ChunkObject->SetStringField(TEXT("utocHash"), Container.UtocHash);
		ChunkObject->SetStringField(TEXT("ucasPath"), Container.UcasPath);
		ChunkObject->SetNumberField(TEXT("ucasSize"), Container.UcasSize);
		ChunkObject->SetStringField(TEXT("ucasHash"), Container.UcasHash);
		OutChunksArray.Add(MakeShareable(new FJsonValueObject(ChunkObject)));
	}
	RootObject->SetArrayField(TEXT("chunks"), OutChunksArray);

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

TArray<FString> UHotUpdateBaseVersionBuilder::CollectAllAssetPaths(IAssetRegistry* AssetRegistry) const
{
	// 强制刷新 AssetRegistry，确保依赖数据完整（Commandlet 模式下缓存可能不包含完整依赖）
	AssetRegistry->SearchAllAssets(true);

	// 等待搜索完成
	while (AssetRegistry->IsLoadingAssets())
	{
		FPlatformProcess::Sleep(0.1f);
	}

	// 从打包设置收集资源
	TArray<FString> AllAssetPaths;

	// 1. 收集 /Game 目录下的所有资源（作为基础）
	FARFilter GameFilter;
	GameFilter.PackagePaths.Add(FName(TEXT("/Game")));
	GameFilter.bRecursivePaths = true;
	TArray<FAssetData> GameAssets;
	AssetRegistry->GetAssets(GameFilter, GameAssets);
	for (const FAssetData& AssetData : GameAssets)
	{
		AllAssetPaths.Add(AssetData.PackageName.ToString());
	}

	// 2. 收集项目打包设置中的资源
	if (const UProjectPackagingSettings* PackagingSettings = GetDefault<UProjectPackagingSettings>())
	{
		// 收集 MapsToCook
		for (const FFilePath& MapPath : PackagingSettings->MapsToCook)
		{
			FString NormalizedPath = MapPath.FilePath;
			if (!NormalizedPath.StartsWith(TEXT("/")))
			{
				NormalizedPath = TEXT("/") + NormalizedPath;
			}
			AllAssetPaths.Add(NormalizedPath);
		}

		// 收集 DirectoriesToAlwaysCook
		for (const FDirectoryPath& Directory : PackagingSettings->DirectoriesToAlwaysCook)
		{
			FString Path = Directory.Path;
			if (!Path.StartsWith(TEXT("/")))
			{
				Path = TEXT("/") + Path;
			}

			FARFilter Filter;
			Filter.PackagePaths.Add(FName(*Path));
			Filter.bRecursivePaths = true;
			TArray<FAssetData> DirAssets;
			AssetRegistry->GetAssets(Filter, DirAssets);
			for (const FAssetData& AssetData : DirAssets)
			{
				AllAssetPaths.Add(AssetData.PackageName.ToString());
			}
		}
	}

	// 3. 收集资源依赖（Hard + Soft，因为 Cooker 会同时打包软引用资源）
	TSet<FString> UniqueAssetPaths(AllAssetPaths);
	for (const FString& AssetPath : AllAssetPaths)
	{
		TArray<FName> Dependencies;
		if (AssetRegistry->GetDependencies(FName(*AssetPath), Dependencies, UE::AssetRegistry::EDependencyCategory::Package))
		{
			for (const FName& Dep : Dependencies)
			{
				FString DepStr = Dep.ToString();
				// 包含 /Game（项目）、/Engine（引擎）、插件路径（如 /NNE/、/Water/）
				// 排除 /Script/ 路径（C++ 类型引用，不是资产）
				if (DepStr.StartsWith(TEXT("/Game/")) || DepStr.StartsWith(TEXT("/Engine/")) ||
					(DepStr.StartsWith(TEXT("/")) && !DepStr.StartsWith(TEXT("/Script/"))))
				{
					UniqueAssetPaths.Add(DepStr);
				}
			}
		}
	}

	return UniqueAssetPaths.Array();
}

TArray<FString> UHotUpdateBaseVersionBuilder::CollectStagedFilePaths() const
{
	// 委托通用方法获取 Staged 文件的 pak 内路径列表
	return FHotUpdatePackagingSettingsHelper::CollectStagedFilePaths();
}

void UHotUpdateBaseVersionBuilder::ApplyMinimalPackageFilter(
	TArray<FString>& InOutAssetPaths,
	TArray<FString>& OutPatchAssets,
	IAssetRegistry* AssetRegistry) const
{
	if (!CurrentConfig.MinimalPackageConfig.bEnableMinimalPackage || InOutAssetPaths.Num() == 0)
	{
		return;
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("启用最小包模式，开始过滤资产，输入资产数: %d"), InOutAssetPaths.Num());

	TArray<FString> WhitelistAssets;
	TArray<FString> ExcludedAssets;

	FHotUpdateAssetFilter::FilterAssets(
		InOutAssetPaths,
		CurrentConfig.MinimalPackageConfig,
		AssetRegistry,
		WhitelistAssets,
		ExcludedAssets);

	UE_LOG(LogHotUpdateEditor, Log,
		TEXT("最小包过滤完成: 白名单资产 %d 个, 排除资产 %d 个"),
		WhitelistAssets.Num(), ExcludedAssets.Num());

	if (WhitelistAssets.Num() == 0)
	{
		UE_LOG(LogHotUpdateEditor, Warning, TEXT("最小包模式过滤后没有剩余资产，请检查必须包含的目录配置"));
	}
	else
	{
		InOutAssetPaths = WhitelistAssets;
	}

	OutPatchAssets = MoveTemp(ExcludedAssets);
}

TArray<FHotUpdateResolvedAssetInfo> UHotUpdateBaseVersionBuilder::ResolveAssetInfo(
	const TArray<FString>& AssetPaths, const FString& CookedPlatformDir) const
{
	TArray<FHotUpdateResolvedAssetInfo> Result;
	Result.Reserve(AssetPaths.Num());

	for (const FString& AssetPath : AssetPaths)
	{
		FString DiskPath = FHotUpdatePackageHelper::GetAssetDiskPath(AssetPath, CookedPlatformDir);

		if (!DiskPath.IsEmpty() && FPaths::FileExists(*DiskPath))
		{
			Result.Emplace(AssetPath, DiskPath);
		}
		else
		{
			// 插件资源路径解析失败时记录警告
			if (!AssetPath.StartsWith(TEXT("/Game/")) && !AssetPath.StartsWith(TEXT("/Engine/")) && !AssetPath.StartsWith(TEXT("/Script/")))
			{
				UE_LOG(LogHotUpdateEditor, Warning, TEXT("插件资源磁盘路径解析失败: AssetPath=%s, DiskPath=%s"),
					*AssetPath, DiskPath.IsEmpty() ? TEXT("(empty)") : *DiskPath);
			}
		}
	}

	return Result;
}

bool UHotUpdateBaseVersionBuilder::BuildFileManifestJson(
	const FString& VersionDir,
	const TSharedPtr<FJsonObject>& VersionObject,
	const TArray<TSharedPtr<FJsonValue>>& ChunksArray,
	const TArray<FHotUpdateResolvedAssetInfo>& BaseAssets,
	const TArray<FHotUpdateResolvedAssetInfo>& PatchAssets) const
{
	TSharedPtr<FJsonObject> FileManifestObj = MakeShareable(new FJsonObject);
	FileManifestObj->SetObjectField(TEXT("version"), VersionObject);
	FileManifestObj->SetArrayField(TEXT("chunks"), ChunksArray);

	// 生成文件条目的通用 lambda（消除基础/热更资源的重复代码）
	TArray<TSharedPtr<FJsonValue>> FilesArray;
	auto AddFileEntries = [&FilesArray](const TArray<FHotUpdateResolvedAssetInfo>& Assets, int32 ChunkId, const FString& Source)
	{
		for (const FHotUpdateResolvedAssetInfo& Info : Assets)
		{
			TSharedPtr<FJsonObject> FileObj = MakeShareable(new FJsonObject);
			FileObj->SetStringField(TEXT("filePath"), Info.FileName);

			FString HashPath = Info.GetHashPath();
			FileObj->SetNumberField(TEXT("fileSize"), IFileManager::Get().FileSize(*HashPath));
			FileObj->SetStringField(TEXT("fileHash"), UHotUpdateFileUtils::CalculateFileHash(HashPath));

			FileObj->SetNumberField(TEXT("chunkId"), ChunkId);
			FileObj->SetNumberField(TEXT("priority"), 0);
			FileObj->SetBoolField(TEXT("isCompressed"), true);
			FileObj->SetStringField(TEXT("source"), Source);

			FilesArray.Add(MakeShareable(new FJsonValueObject(FileObj)));
		}
	};

	// 基础资源 -> chunk0 base
	AddFileEntries(BaseAssets, 0, TEXT("base"));
	// 热更资源 -> 按 CachedChunkMapping 分配实际 ChunkId
	for (const FHotUpdateResolvedAssetInfo& Info : PatchAssets)
	{
		int32 ChunkId = 11;
		if (const int32* Found = CachedChunkMapping.Find(Info.AssetPath))
		{
			ChunkId = *Found;
		}

		TSharedPtr<FJsonObject> FileObj = MakeShareable(new FJsonObject);
		FileObj->SetStringField(TEXT("filePath"), Info.FileName);

		FString HashPath = Info.GetHashPath();
		FileObj->SetNumberField(TEXT("fileSize"), IFileManager::Get().FileSize(*HashPath));
		FileObj->SetStringField(TEXT("fileHash"), UHotUpdateFileUtils::CalculateFileHash(HashPath));

		FileObj->SetNumberField(TEXT("chunkId"), ChunkId);
		FileObj->SetNumberField(TEXT("priority"), 0);
		FileObj->SetBoolField(TEXT("isCompressed"), true);
		FileObj->SetStringField(TEXT("source"), TEXT("patch"));

		FilesArray.Add(MakeShareable(new FJsonValueObject(FileObj)));
	}

	FileManifestObj->SetArrayField(TEXT("files"), FilesArray);

	// 序列化 filemanifest
	FString FileManifestString;
	TSharedRef<TJsonWriter<>> FileWriter = TJsonWriterFactory<>::Create(&FileManifestString);
	FJsonSerializer::Serialize(FileManifestObj.ToSharedRef(), FileWriter);

	FString FileManifestPath = FPaths::Combine(VersionDir, TEXT("filemanifest.json"));
	if (!FFileHelper::SaveStringToFile(FileManifestString, *FileManifestPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("保存 FileManifest 失败: %s"), *FileManifestPath);
		return false;
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("FileManifest 保存成功: %s，包含 %d 个资源（%d 基础 + %d 热更）"),
		*FileManifestPath, BaseAssets.Num() + PatchAssets.Num(), BaseAssets.Num(), PatchAssets.Num());

	return true;
}

void UHotUpdateBaseVersionBuilder::UpdateProgress(const FString& Stage, float Percent, const FString& Message)
{
	FHotUpdateBaseVersionBuildProgress Progress;
	Progress.CurrentStage = Stage;
	Progress.ProgressPercent = Percent;
	Progress.StatusMessage = Message;

	// 在游戏线程广播
	AsyncTask(ENamedThreads::GameThread, [this, Progress]()
	{
		OnBuildProgress.Broadcast(Progress);
	});
}


