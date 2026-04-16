// Copyright czm. All Rights Reserved.

#include "HotUpdateBaseVersionBuilder.h"
#include "HotUpdatePatchPackageBuilder.h"
#include "Core/HotUpdateFileUtils.h"
#include "HotUpdateEditor.h"
#include "HotUpdateVersionManager.h"
#include "HotUpdateUtils.h"
#include "HotUpdateAssetFilter.h"
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
		WriteMinimalPackageConfig();
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
		UHotUpdateVersionManager::GetVersionDir(CurrentConfig.VersionString, CurrentConfig.Platform),
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
		FString HotUpdatePaksDir = UHotUpdateVersionManager::GetVersionDir(CurrentConfig.VersionString, CurrentConfig.Platform) / TEXT("Paks");
		FPaths::NormalizeDirectoryName(HotUpdatePaksDir);
		Params += FString::Printf(TEXT(" -HotUpdateOutputDir=\"%s\""), *HotUpdatePaksDir);
		// Build whitelist dirs parameter (semicolon-separated)
		FString WhitelistStr;
		for (const FDirectoryPath& Dir : CurrentConfig.MinimalPackageConfig.WhitelistDirectories)
		{
			if (!WhitelistStr.IsEmpty())
			{
				WhitelistStr += TEXT(";");
			}
			WhitelistStr += Dir.Path;
		}
		Params += FString::Printf(TEXT(" WhitelistDirs=%s"), *WhitelistStr);

		// Write config to temp file for cooking process to read
		WriteMinimalPackageConfig();

		UE_LOG(LogHotUpdateEditor, Log, TEXT("MinimalPackage mode: WhitelistDirs=%s"), *WhitelistStr);
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
	// 从打包输出目录获取容器文件列表
	FString OutputDir = CurrentConfig.OutputDirectory;
	if (OutputDir.IsEmpty())
	{
		OutputDir = GetDefaultOutputDirectory();
	}
	OutputDir = FPaths::Combine(OutputDir, CurrentConfig.VersionString);
	FPaths::NormalizeDirectoryName(OutputDir);

	// 根据平台获取 UAT 输出子目录名
	// 注意：Android + cookflavor 时 UAT 输出目录为 Android_ASTC / Android_ETC2 等
	FString PlatformDir;
	if (CurrentConfig.Platform == EHotUpdatePlatform::Android && CurrentConfig.AndroidTextureFormat != EHotUpdateAndroidTextureFormat::Multi)
	{
		FString TextureFormat;
		switch (CurrentConfig.AndroidTextureFormat)
		{
		case EHotUpdateAndroidTextureFormat::ETC2: TextureFormat = TEXT("ETC2"); break;
		case EHotUpdateAndroidTextureFormat::ASTC: TextureFormat = TEXT("ASTC"); break;
		case EHotUpdateAndroidTextureFormat::DXT:  TextureFormat = TEXT("DXT");  break;
		default: break;
		}
		if (!TextureFormat.IsEmpty())
		{
			PlatformDir = FPaths::Combine(OutputDir, FString::Printf(TEXT("Android_%s"), *TextureFormat));
		}
		else
		{
			PlatformDir = FPaths::Combine(OutputDir, HotUpdateUtils::GetPlatformString(CurrentConfig.Platform));
		}
	}
	else
	{
		PlatformDir = FPaths::Combine(OutputDir, HotUpdateUtils::GetPlatformString(CurrentConfig.Platform));
	}
	if (!IPlatformFile::GetPlatformPhysical().DirectoryExists(*PlatformDir))
	{
		UE_LOG(LogHotUpdateEditor, Warning, TEXT("打包输出目录不存在: %s"), *PlatformDir);
		return false;
	}

	// 收集打包后的容器文件信息
	TArray<FHotUpdateContainerInfo> ContainerInfos;
	IPlatformFile& PlatformFile = IPlatformFile::GetPlatformPhysical();

	// 版本目录（manifest 所在目录，StripExtraPakChunks 也会将 pakchunk1+ 移到此目录）
	FString VersionDir = UHotUpdateVersionManager::GetVersionDir(CurrentConfig.VersionString, CurrentConfig.Platform);
	FPaths::NormalizeDirectoryName(VersionDir);
	IPlatformFile::GetPlatformPhysical().CreateDirectoryTree(*VersionDir);

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

	// ========== 生成 Manifest（容器信息） ==========
	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);

	// 版本信息
	TSharedPtr<FJsonObject> VersionObject = MakeShareable(new FJsonObject);
	VersionObject->SetStringField(TEXT("version"), CurrentConfig.VersionString);
	VersionObject->SetStringField(TEXT("platform"), HotUpdateUtils::GetPlatformDirectoryName(CurrentConfig.Platform));
	VersionObject->SetNumberField(TEXT("timestamp"), FDateTime::Now().ToUnixTimestamp());

	RootObject->SetObjectField(TEXT("version"), VersionObject);

	// chunks 数组
	TArray<TSharedPtr<FJsonValue>> ChunksArray;
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
		ChunksArray.Add(MakeShareable(new FJsonValueObject(ChunkObject)));
	}
	RootObject->SetArrayField(TEXT("chunks"), ChunksArray);

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

	// ========== 收集资源文件列表（用于差异计算） ==========
	// 从 AssetRegistry 收集所有被打包的资源
	TArray<FString> AssetPaths;
	TMap<FString, FString> AssetDiskPaths;

	// 收集项目打包设置中配置的资源
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry* AssetRegistry = &AssetRegistryModule.Get();

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

	AssetPaths = UniqueAssetPaths.Array();


	// ========== 应用最小包过滤 ==========
	TArray<FString> PatchAssets; // 非白名单资源（热更包）
	if (CurrentConfig.MinimalPackageConfig.bEnableMinimalPackage && AssetPaths.Num() > 0)
	{
		UE_LOG(LogHotUpdateEditor, Log, TEXT("启用最小包模式，开始过滤资产，输入资产数: %d"), AssetPaths.Num());

		TArray<FString> WhitelistAssets;
		TArray<FString> ExcludedAssets;

		FHotUpdateAssetFilter::FilterAssets(
			AssetPaths,
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
			// 不返回失败，因为打包本身已经成功，只是资源清单为空
		}
		else
		{
			AssetPaths = WhitelistAssets;
		}

		PatchAssets = MoveTemp(ExcludedAssets);
	}
	// ========== 最小包过滤结束 ==========

	// 获取白名单资源磁盘路径
	for (const FString& AssetPath : AssetPaths)
	{
		FString DiskPath = UHotUpdatePatchPackageBuilder::GetAssetDiskPath(AssetPath, HotUpdateUtils::GetCookedPlatformDir(CurrentConfig.Platform));

		if (!DiskPath.IsEmpty() && FPaths::FileExists(*DiskPath))
		{
			AssetDiskPaths.Add(AssetPath, DiskPath);
		}
		else
		{
			if (!AssetPath.StartsWith(TEXT("/Game/")) && !AssetPath.StartsWith(TEXT("/Engine/")) && !AssetPath.StartsWith(TEXT("/Script/")))
			{
				UE_LOG(LogHotUpdateEditor, Warning, TEXT("插件资源磁盘路径解析失败: AssetPath=%s, DiskPath=%s"),
					*AssetPath, DiskPath.IsEmpty() ? TEXT("(empty)") : *DiskPath);
			}
		}
	}

	// 获取热更资源磁盘路径
	TMap<FString, FString> PatchDiskPaths;
	for (const FString& AssetPath : PatchAssets)
	{
		FString DiskPath = UHotUpdatePatchPackageBuilder::GetAssetDiskPath(AssetPath, HotUpdateUtils::GetCookedPlatformDir(CurrentConfig.Platform));

		if (!DiskPath.IsEmpty() && FPaths::FileExists(*DiskPath))
		{
			PatchDiskPaths.Add(AssetPath, DiskPath);
		}
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("收集了 %d 个基础资源, %d 个热更资源用于差异计算"),
		AssetDiskPaths.Num(), PatchDiskPaths.Num());

	// ========== 生成 FileManifest（资源文件列表，用于差异计算） ==========
	TSharedPtr<FJsonObject> FileManifestObj = MakeShareable(new FJsonObject);
	FileManifestObj->SetObjectField(TEXT("version"), VersionObject);

	// chunks 数组也加入 filemanifest
	FileManifestObj->SetArrayField(TEXT("chunks"), ChunksArray);

	// 文件列表（白名单资源 -> chunk0 base）
	TArray<TSharedPtr<FJsonValue>> FilesArray;
	for (const auto& Pair : AssetDiskPaths)
	{
		const FString& AssetPath = Pair.Key;
		const FString& DiskPath = Pair.Value;

		TSharedPtr<FJsonObject> FileObj = MakeShareable(new FJsonObject);
		FileObj->SetStringField(TEXT("filePath"), UHotUpdatePatchPackageBuilder::ConvertAssetPathToFileName(AssetPath, HotUpdateUtils::GetCookedPlatformDir(CurrentConfig.Platform)));
		FileObj->SetNumberField(TEXT("fileSize"), IFileManager::Get().FileSize(*DiskPath));
		FileObj->SetStringField(TEXT("fileHash"), UHotUpdateFileUtils::CalculateFileHash(DiskPath));
		FileObj->SetNumberField(TEXT("chunkId"), 0);
		FileObj->SetNumberField(TEXT("priority"), 0);
		FileObj->SetBoolField(TEXT("isCompressed"), true);
		FileObj->SetStringField(TEXT("source"), TEXT("base"));

		FilesArray.Add(MakeShareable(new FJsonValueObject(FileObj)));
	}

	// 文件列表（热更资源 -> chunk11 patch）
	for (const auto& Pair : PatchDiskPaths)
	{
		const FString& AssetPath = Pair.Key;
		const FString& DiskPath = Pair.Value;

		TSharedPtr<FJsonObject> FileObj = MakeShareable(new FJsonObject);
		FileObj->SetStringField(TEXT("filePath"), UHotUpdatePatchPackageBuilder::ConvertAssetPathToFileName(AssetPath, HotUpdateUtils::GetCookedPlatformDir(CurrentConfig.Platform)));
		FileObj->SetNumberField(TEXT("fileSize"), IFileManager::Get().FileSize(*DiskPath));
		FileObj->SetStringField(TEXT("fileHash"), UHotUpdateFileUtils::CalculateFileHash(DiskPath));
		FileObj->SetNumberField(TEXT("chunkId"), 11);
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
		*FileManifestPath, AssetDiskPaths.Num() + PatchDiskPaths.Num(), AssetDiskPaths.Num(), PatchDiskPaths.Num());

	// ========== 注册版本信息 ==========
	UHotUpdateVersionManager* VersionManager = NewObject<UHotUpdateVersionManager>();
	FHotUpdateEditorVersionInfo VersionInfo;
	VersionInfo.VersionString = CurrentConfig.VersionString;
	VersionInfo.PackageKind = EHotUpdatePackageKind::Base;
	VersionInfo.Platform = CurrentConfig.Platform;
	VersionInfo.CreatedTime = FDateTime::Now();
	VersionInfo.ManifestPath = ManifestFilePath;
	VersionInfo.AssetCount = AssetDiskPaths.Num() + PatchDiskPaths.Num();

	VersionManager->RegisterVersion(VersionInfo);

	UE_LOG(LogHotUpdateEditor, Log, TEXT("版本注册成功: %s"), *CurrentConfig.VersionString);

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


