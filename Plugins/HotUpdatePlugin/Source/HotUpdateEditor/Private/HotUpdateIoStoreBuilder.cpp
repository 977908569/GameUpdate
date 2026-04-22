// Copyright czm. All Rights Reserved.

#include "HotUpdateIoStoreBuilder.h"
#include "HotUpdateEditor.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "Misc/SecureHash.h"
#include "Misc/PackageName.h"
#include "GenericPlatform/GenericPlatformProcess.h"

FHotUpdateIoStoreBuilder::FHotUpdateIoStoreBuilder()
	: bIsBuilding(false)
	, bIsCancelled(false)
{
}

FHotUpdateIoStoreResult FHotUpdateIoStoreBuilder::BuildIoStoreContainer(
	const TMap<FString, FString>& AssetPathToDiskPath,
	const FString& OutputPath,
	const FHotUpdateIoStoreConfig& Config)
{
	UE_LOG(LogHotUpdateEditor, Log, TEXT("BuildIoStoreContainer (同步) 开始调用"));
	UE_LOG(LogHotUpdateEditor, Log, TEXT("  输出路径: %s"), *OutputPath);
	UE_LOG(LogHotUpdateEditor, Log, TEXT("  资源文件数: %d"), AssetPathToDiskPath.Num());

	FHotUpdateIoStoreResult Result;

	// 验证配置
	FString ErrorMessage;
	if (!ValidateConfig(Config, ErrorMessage))
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("配置验证失败: %s"), *ErrorMessage);
		Result.bSuccess = false;
		Result.ErrorMessage = ErrorMessage;
		return Result;
	}

	bIsCancelled = false;

	// 执行构建
	bool bSuccess = CreateIoStoreWithUnrealPak(AssetPathToDiskPath, OutputPath, Config, Result);

	Result.bSuccess = bSuccess;
	Result.FileCount = AssetPathToDiskPath.Num();

	bIsBuilding = false;

	return Result;
}

void FHotUpdateIoStoreBuilder::BuildIoStoreContainerAsync(
	const TMap<FString, FString>& AssetPathToDiskPath,
	const FString& OutputPath,
	const FHotUpdateIoStoreConfig& Config)
{
	UE_LOG(LogHotUpdateEditor, Log, TEXT("BuildIoStoreContainerAsync 开始调用"));
	UE_LOG(LogHotUpdateEditor, Log, TEXT("  bIsBuilding: %s"), bIsBuilding ? TEXT("true") : TEXT("false"));
	UE_LOG(LogHotUpdateEditor, Log, TEXT("  BuildTask.IsValid(): %s"), BuildTask.IsValid() ? TEXT("true") : TEXT("false"));
	UE_LOG(LogHotUpdateEditor, Log, TEXT("  BuildTask.IsReady(): %s"), BuildTask.IsReady() ? TEXT("true") : TEXT("false"));

	if (bIsBuilding)
	{
		if (BuildTask.IsValid() && !BuildTask.IsReady())
		{
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("已有构建任务正在运行，拒绝新的构建请求"));
			FHotUpdateIoStoreResult Result;
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

	UE_LOG(LogHotUpdateEditor, Log, TEXT("开始新的构建任务，输出路径: %s"), *OutputPath);
	UE_LOG(LogHotUpdateEditor, Log, TEXT("资源文件数: %d"), AssetPathToDiskPath.Num());

	bIsBuilding = true;
	bIsCancelled = false;

	BuildTask = Async(EAsyncExecution::Thread, [WeakBuilder = TWeakPtr<FHotUpdateIoStoreBuilder>(AsShared()), AssetPathToDiskPath, OutputPath, Config]()
	{
		TSharedPtr<FHotUpdateIoStoreBuilder> Self = WeakBuilder.Pin();
		if (!Self.IsValid()) return;
		FHotUpdateIoStoreResult Result;
		bool bSuccess = Self->CreateIoStoreWithUnrealPak(AssetPathToDiskPath, OutputPath, Config, Result);

		Result.bSuccess = bSuccess;
		Result.FileCount = AssetPathToDiskPath.Num();

		Self->bIsBuilding = false;

		AsyncTask(ENamedThreads::GameThread, [WeakBuilder, Result]()
		{
			TSharedPtr<FHotUpdateIoStoreBuilder> PinnedBuilder = WeakBuilder.Pin();
			if (!PinnedBuilder.IsValid()) return;
			PinnedBuilder->OnComplete.Broadcast(Result);
		});
	});
}

void FHotUpdateIoStoreBuilder::CancelBuild()
{
	bIsCancelled = true;
}

FHotUpdateIoStoreProgress FHotUpdateIoStoreBuilder::GetCurrentProgress() const
{
	FScopeLock Lock(&ProgressCriticalSection);
	return CurrentProgress;
}

bool FHotUpdateIoStoreBuilder::ValidateConfig(const FHotUpdateIoStoreConfig& Config, FString& OutErrorMessage)
{
	if (Config.ContainerName.IsEmpty())
	{
		OutErrorMessage = TEXT("容器名称不能为空");
		return false;
	}

	if (Config.CompressionFormat != TEXT("Oodle") &&
		Config.CompressionFormat != TEXT("Zlib") &&
		Config.CompressionFormat != TEXT("GZip") &&
		Config.CompressionFormat != TEXT("None"))
	{
		OutErrorMessage = FString::Printf(TEXT("不支持的压缩格式: %s"), *Config.CompressionFormat);
		return false;
	}

	return true;
}

bool FHotUpdateIoStoreBuilder::CreateIoStoreWithUnrealPak(
	const TMap<FString, FString>& AssetPathToDiskPath,
	const FString& OutputPath,
	const FHotUpdateIoStoreConfig& Config,
	FHotUpdateIoStoreResult& OutResult)
{
	UE_LOG(LogHotUpdateEditor, Log, TEXT("开始创建 IoStore/Pak 容器: %s"), *OutputPath);
	UE_LOG(LogHotUpdateEditor, Log, TEXT("  使用 IoStore 格式: %s"), Config.bUseIoStore ? TEXT("true") : TEXT("false"));

	// 1. 查找 UnrealPak 工具
	FString UnrealPakPath = FindUnrealPakPath(OutResult.ErrorMessage);
	if (UnrealPakPath.IsEmpty())
	{
		return false;
	}

	// 2. 准备临时目录
	FString TempDir = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("IoStoreTemp"), Config.ContainerName);
	if (!PrepareTempDirectory(TempDir, OutResult.ErrorMessage))
	{
		return false;
	}

	// 3. 生成响应文件
	FString ResponseFilePath = FPaths::Combine(TempDir, TEXT("IoStoreCommands.txt"));
	int32 ValidFileCount = 0;
	int64 TotalSize = 0;

	if (!GenerateResponseFile(AssetPathToDiskPath, ResponseFilePath,
		Config.CompressionFormat, ValidFileCount, TotalSize, OutResult.ErrorMessage))
	{
		CleanupTempDirectory(TempDir);
		return false;
	}

	// 4. 生成加密密钥文件
	FString CryptoKeyPath;
	if (!Config.EncryptionKey.IsEmpty())
	{
		CryptoKeyPath = GenerateCryptoKeyFile(TempDir, Config);
	}

	// 5. 准备输出目录
	if (!PrepareOutputDirectory(OutputPath, OutResult.ErrorMessage))
	{
		CleanupTempDirectory(TempDir);
		return false;
	}

	// 6. 构建命令行
	FString CmdLine = BuildUnrealPakCommandLine(OutputPath, ResponseFilePath, CryptoKeyPath, Config);

	// 7. 执行 UnrealPak
	UpdateProgress(TEXT("创建容器"), TEXT(""), 0, 1, 0, TotalSize);
	UE_LOG(LogHotUpdateEditor, Log, TEXT("执行 UnrealPak: %s %s"), *UnrealPakPath, *CmdLine);

	if (!ExecuteUnrealPak(UnrealPakPath, CmdLine, OutputPath, OutResult))
	{
		CleanupTempDirectory(TempDir);
		return false;
	}

	// 8. 不清理临时目录，保留响应文件等供调试查看
	// 下次构建时 PrepareTempDirectory 会先删除重建

	UE_LOG(LogHotUpdateEditor, Log, TEXT("容器创建完成，总大小: %lld 字节"), OutResult.ContainerSize);
	UpdateProgress(TEXT("完成"), TEXT(""), 1, 1, TotalSize, TotalSize);

	return true;
}

FString FHotUpdateIoStoreBuilder::FindUnrealPakPath(FString& OutErrorMessage)
{
	FString EngineDir = FPaths::EngineDir();
	#if PLATFORM_WINDOWS
	FString UnrealPakPath = FPaths::Combine(EngineDir, TEXT("Binaries/Win64/UnrealPak.exe"));
#elif PLATFORM_MAC
	FString UnrealPakPath = FPaths::Combine(EngineDir, TEXT("Binaries/Mac/UnrealPak"));
#else
	FString UnrealPakPath = FPaths::Combine(EngineDir, TEXT("Binaries/Win64/UnrealPak.exe"));
#endif

	if (!FPaths::FileExists(UnrealPakPath))
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("找不到 UnrealPak 工具: %s"), *UnrealPakPath);
		OutErrorMessage = TEXT("找不到 UnrealPak 工具");
		return TEXT("");
	}

	return UnrealPakPath;
}

bool FHotUpdateIoStoreBuilder::PrepareTempDirectory(
	const FString& TempDir,
	FString& OutErrorMessage)
{
	IPlatformFile& PlatformFile = IPlatformFile::GetPlatformPhysical();

	// 清空整个 IoStoreTemp 目录，只保留当前构建
	FString IoStoreTempDir = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("IoStoreTemp"));
	if (PlatformFile.DirectoryExists(*IoStoreTempDir))
	{
		PlatformFile.DeleteDirectoryRecursively(*IoStoreTempDir);
	}

	if (!PlatformFile.CreateDirectoryTree(*TempDir))
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("无法创建临时目录: %s"), *TempDir);
		OutErrorMessage = TEXT("无法创建临时目录");
		return false;
	}

	return true;
}

FString FHotUpdateIoStoreBuilder::DetermineAssetExtension(FString& InOutPakPath, const FString& DiskPath)
{
	// 情况1：路径已有 UE 资源扩展名，剥离后返回
	if (InOutPakPath.EndsWith(TEXT(".uasset")) || InOutPakPath.EndsWith(TEXT(".umap")) ||
		InOutPakPath.EndsWith(TEXT(".uexp")) || InOutPakPath.EndsWith(TEXT(".ubulk")) ||
		InOutPakPath.EndsWith(TEXT(".ubulk2")))
	{
		int32 DotIndex;
		if (InOutPakPath.FindChar('.', DotIndex))
		{
			FString Extension = InOutPakPath.Mid(DotIndex + 1);
			InOutPakPath = InOutPakPath.Left(DotIndex);
			return Extension;
		}
	}

	// 情况2：路径有非 UE 扩展名（如 .txt/.ini 等 Staged 文件），保留原始路径，不追加扩展名
	FString PathExt = FPaths::GetExtension(InOutPakPath);
	if (!PathExt.IsEmpty() && PathExt != TEXT("uasset") && PathExt != TEXT("umap"))
	{
		return TEXT("");
	}

	// 情况3：无扩展名，从磁盘路径获取
	if (!DiskPath.IsEmpty())
	{
		FString Extension = FPaths::GetExtension(DiskPath);
		if (!Extension.IsEmpty())
		{
			return Extension;
		}
	}

	// 情况4：回退启发式，地图用 umap，其他用 uasset
	if (InOutPakPath.Contains(TEXT("/Maps/")) || InOutPakPath.Contains(TEXT("/Map/")))
	{
		return TEXT("umap");
	}
	return TEXT("uasset");
}

FString FHotUpdateIoStoreBuilder::MapPluginPathToPakMountPath(const FString& PluginPakPath, const FString& ProjectName)
{
	// 尝试通过 FPackageName 解析实际文件路径
	FString ResolvedPath;
	if (!FPackageName::TryConvertLongPackageNameToFilename(PluginPakPath, ResolvedPath, TEXT("")))
	{
		UE_LOG(LogHotUpdateEditor, Warning, TEXT("GetPakInternalPath: FPackageName 无法解析 %s，默认按引擎插件处理"), *PluginPakPath);
		return FString::Printf(TEXT("../../../Engine/Plugins/%s"), *PluginPakPath.Mid(1));
	}

	// 规范化路径用于前缀比较
	FString NormalizedResolved = ResolvedPath;
	FPaths::NormalizeDirectoryName(NormalizedResolved);

	FString NormalizedEngineDir = FPaths::EngineDir();
	FPaths::NormalizeDirectoryName(NormalizedEngineDir);

	FString NormalizedProjectDir = FPaths::ProjectDir();
	FPaths::NormalizeDirectoryName(NormalizedProjectDir);

	// 引擎插件：解析路径以引擎目录开头
	if (NormalizedResolved.StartsWith(NormalizedEngineDir))
	{
		FString RelativePath = NormalizedResolved.RightChop(NormalizedEngineDir.Len());
		return FString::Printf(TEXT("../../../Engine/%s"), *RelativePath);
	}

	// 项目插件：解析路径以项目目录开头
	if (NormalizedResolved.StartsWith(NormalizedProjectDir))
	{
		FString RelativePath = NormalizedResolved.RightChop(NormalizedProjectDir.Len());
		return FString::Printf(TEXT("../../../%s/%s"), *ProjectName, *RelativePath);
	}

	// 无法分类，按引擎插件处理
	UE_LOG(LogHotUpdateEditor, Warning, TEXT("GetPakInternalPath: 无法分类插件路径 %s，默认按引擎插件处理"), *PluginPakPath);
	return FString::Printf(TEXT("../../../Engine/Plugins/%s"), *PluginPakPath.Mid(1));
}

FString FHotUpdateIoStoreBuilder::MapToPakMountPath(const FString& PakPath)
{
	FString ProjectName = FApp::GetProjectName();

	// /Game/ 映射到项目 Content 目录
	if (PakPath.StartsWith(TEXT("/Game/")))
	{
		return FString::Printf(TEXT("../../../%s/Content/%s"), *ProjectName, *PakPath.Mid(6));
	}

	// /Engine/ 映射到引擎 Content 目录
	if (PakPath.StartsWith(TEXT("/Engine/")))
	{
		return FString::Printf(TEXT("../../../Engine/Content/%s"), *PakPath.Mid(9));
	}

	// 其他 / 开头的路径视为插件路径
	return MapPluginPathToPakMountPath(PakPath, ProjectName);
}

FString FHotUpdateIoStoreBuilder::GetPakInternalPath(const FString& AssetPath, const FString& DiskPath)
{
	// 从 AssetPath（如 "/Game/Maps/Start"）转换为 Pak 内部路径（Dest 路径）
	// UE5 标准 pak 的 Dest 路径格式: ../../../{ProjectName}/Content/...
	// 这样 GetCommonRootPath 会计算出 mount point 为 "../../../"
	// 与标准基础 pak 的 mount point 一致，运行时才能正确匹配文件路径

	FString PakPath = AssetPath;

	// 确保路径以 / 开头（标准 UE 长包名格式）
	if (!PakPath.StartsWith(TEXT("/")))
	{
		PakPath = TEXT("/") + PakPath;
	}

	// 步骤1：确定文件扩展名（可能剥离路径中已有的 UE 扩展名）
	FString Extension = DetermineAssetExtension(PakPath, DiskPath);

	// 步骤2：挂载点映射
	PakPath = MapToPakMountPath(PakPath);

	// 步骤3：追加扩展名
	if (!Extension.IsEmpty())
	{
		PakPath += TEXT(".") + Extension;
	}

	// 确保使用正斜杠
	PakPath.ReplaceCharInline('\\', '/');

	UE_LOG(LogHotUpdateEditor, Log, TEXT("AssetPath -> PakPath: %s -> %s"), *AssetPath, *PakPath);

	return PakPath;
}

bool FHotUpdateIoStoreBuilder::GenerateResponseFile(
	const TMap<FString, FString>& AssetPathToDiskPath,
	const FString& ResponseFilePath,
	const FString& CompressionFormat,
	int32& OutValidFileCount,
	int64& OutTotalSize,
	FString& OutErrorMessage)
{
	IPlatformFile& PlatformFile = IPlatformFile::GetPlatformPhysical();
	FString ResponseContent;

	OutValidFileCount = 0;
	OutTotalSize = 0;

	int32 TotalAssets = AssetPathToDiskPath.Num();
	UpdateProgress(TEXT("准备资源"), TEXT(""), 0, TotalAssets, 0, 0);

	int32 Index = 0;
	for (const TPair<FString, FString>& Pair : AssetPathToDiskPath)
	{
		if (bIsCancelled)
		{
			OutErrorMessage = TEXT("构建已取消");
			return false;
		}

		const FString& AssetPath = Pair.Key;
		const FString& DiskPath = Pair.Value;

		if (!PlatformFile.FileExists(*DiskPath))
		{
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("源文件不存在: %s (Asset: %s)"), *DiskPath, *AssetPath);
			continue;
		}

		// 使用 AssetPath 和 DiskPath 计算 Pak 内部路径
		FString PakInternalPath = GetPakInternalPath(AssetPath, DiskPath);

		// 源路径使用正斜杠
		FString UnixDiskPath = DiskPath;
		UnixDiskPath.ReplaceCharInline('\\', '/');

		ResponseContent += (CompressionFormat != TEXT("None"))
				? FString::Printf(TEXT("\"%s\" \"%s\" -compress\n"), *UnixDiskPath, *PakInternalPath)
				: FString::Printf(TEXT("\"%s\" \"%s\"\n"), *UnixDiskPath, *PakInternalPath);

		int64 FileSize = IFileManager::Get().FileSize(*DiskPath);
		OutTotalSize += FileSize;
		OutValidFileCount++;

		// 收集配套文件 (.uexp, .ubulk, .ubulk2)
		// UE5 的 uasset/umap 通常有对应的 .uexp（导出数据）和 .ubulk（批量数据）
		// 基础包中这些文件同时存在，Patch 也必须包含，否则运行时读取越界崩溃
		FString DiskDir = FPaths::GetPath(DiskPath);
		FString BaseFilename = FPaths::GetBaseFilename(DiskPath);
		FString PakInternalDir = FPaths::GetPath(PakInternalPath);
		FString PakInternalBaseFilename = FPaths::GetBaseFilename(PakInternalPath);

		static const TArray<FString> CompanionExtensions = { TEXT("uexp"), TEXT("ubulk"), TEXT("ubulk2") };
		for (const FString& CompanionExt : CompanionExtensions)
		{
			FString CompanionDiskPath = FPaths::Combine(DiskDir, BaseFilename + TEXT(".") + CompanionExt);
			if (PlatformFile.FileExists(*CompanionDiskPath))
			{
				FString CompanionPakPath = PakInternalDir / (PakInternalBaseFilename + TEXT(".") + CompanionExt);
				CompanionPakPath.ReplaceCharInline('\\', '/');

				FString UnixCompanionDiskPath = CompanionDiskPath;
				UnixCompanionDiskPath.ReplaceCharInline('\\', '/');

				ResponseContent += (CompressionFormat != TEXT("None"))
						? FString::Printf(TEXT("\"%s\" \"%s\" -compress\n"), *UnixCompanionDiskPath, *CompanionPakPath)
						: FString::Printf(TEXT("\"%s\" \"%s\"\n"), *UnixCompanionDiskPath, *CompanionPakPath);

				int64 CompanionSize = IFileManager::Get().FileSize(*CompanionDiskPath);
				OutTotalSize += CompanionSize;
				OutValidFileCount++;
			}
		}

		UpdateProgress(TEXT("准备资源"), DiskPath, ++Index, TotalAssets, 0, OutTotalSize);
	}

	if (OutValidFileCount == 0)
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("没有有效的资源文件可打包"));
		OutErrorMessage = TEXT("没有有效的资源文件可打包");
		return false;
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("已准备 %d 个资源文件，总大小: %lld 字节"), OutValidFileCount, OutTotalSize);

	// 添加占位条目确保 PAK mount point 为 ../../../
	// GetCommonRootPath 从所有 Dest 路径计算最长公共前缀作为 mount point
	// 如果所有资源都在同一个子目录下，会导致 mount point 过窄
	// 与基础包 ../../../ 不匹配，运行时无法覆盖基础包内容
	// 使用 Engine/Content 前缀与 GameUpdate/Content 前缀不同，强制公共前缀缩短到 ../../../
	{
		FString ProjectName = FApp::GetProjectName();
		FString PlaceholderSource = FPaths::ProjectDir() / (ProjectName + TEXT(".uproject"));
		if (FPaths::FileExists(*PlaceholderSource))
		{
			FString PlaceholderSourceUnix = PlaceholderSource;
			PlaceholderSourceUnix.ReplaceCharInline('\\', '/');
			FString PlaceholderDest = FString::Printf(TEXT("../../../Engine/Content/__MountPointPlaceholder__/%s.uproject"), *ProjectName);
			ResponseContent += FString::Printf(TEXT("\"%s\" \"%s\"\n"), *PlaceholderSourceUnix, *PlaceholderDest);
			UE_LOG(LogHotUpdateEditor, Verbose, TEXT("添加 mount point 占位条目: %s -> %s"), *PlaceholderSourceUnix, *PlaceholderDest);
		}
	}

	if (!FFileHelper::SaveStringToFile(ResponseContent, *ResponseFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("无法创建响应文件: %s"), *ResponseFilePath);
		OutErrorMessage = TEXT("无法创建响应文件");
		return false;
	}

	return true;
}

bool FHotUpdateIoStoreBuilder::PrepareOutputDirectory(
	const FString& OutputPath,
	FString& OutErrorMessage)
{
	IPlatformFile& PlatformFile = IPlatformFile::GetPlatformPhysical();

	FString OutputDir = FPaths::GetPath(OutputPath);
	if (!PlatformFile.CreateDirectoryTree(*OutputDir))
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("无法创建输出目录: %s"), *OutputDir);
		OutErrorMessage = TEXT("无法创建输出目录");
		return false;
	}

	// UE5 IoStore 标准格式：清理 .utoc, .ucas, .pak 文件
	TArray<FString> Extensions = { TEXT(".utoc"), TEXT(".ucas"), TEXT(".pak") };
	for (const FString& Ext : Extensions)
	{
		FString ExistingPath = OutputPath + Ext;
		if (PlatformFile.FileExists(*ExistingPath))
		{
			PlatformFile.DeleteFile(*ExistingPath);
			UE_LOG(LogHotUpdateEditor, Log, TEXT("删除已存在的文件: %s"), *ExistingPath);
		}
	}

	return true;
}

FString FHotUpdateIoStoreBuilder::BuildUnrealPakCommandLine(
	const FString& OutputPath,
	const FString& ResponseFilePath,
	const FString& CryptoKeyPath,
	const FHotUpdateIoStoreConfig& Config)
{
	FString ProjectDir = FPaths::ProjectDir();
	ProjectDir.ReplaceCharInline('\\', '/');

	FString CmdLine;

	// UE5 IoStore 打包方式：
	// 使用 -Create 参数配合 -iostore 标志来创建 IoStore 容器 (.utoc + .ucas + .pak)
	// 输出路径不带扩展名，UnrealPak 会自动添加 .utoc 和 .ucas
	if (Config.bUseIoStore)
	{
		// IoStore 方式：使用 -Create 配合 -iostore 标志
		// 响应文件格式与传统 Pak 相同，但添加 -iostore 标志
		CmdLine = FString::Printf(
			TEXT("\"%s.utoc\" -Create=\"%s\" -iostore -projectdir=\"%s\""),
			*OutputPath,
			*ResponseFilePath,
			*ProjectDir);
	}
	else
	{
		// 传统 Pak 方式：只生成 .pak
		CmdLine = FString::Printf(
			TEXT("\"%s.pak\" -Create=\"%s\" -projectdir=\"%s\""),
			*OutputPath,
			*ResponseFilePath,
			*ProjectDir);
	}

	// MountPoint 由 GetCommonRootPath 从响应文件 Dest 路径自动计算
	// UE5 的 UnrealPak 在 -Create 模式下不支持 -mountpoint 参数覆盖
	// 需要在响应文件中添加占位条目确保公共前缀为 ../../../
	// 否则当所有资源在同一子目录时 mount point 过窄，运行时无法覆盖基础包

	if (Config.CompressionFormat != TEXT("None"))
	{
		CmdLine += FString::Printf(TEXT(" -compressionformats=%s"), *Config.CompressionFormat);

		if (Config.CompressionLevel > 0)
		{
			CmdLine += FString::Printf(TEXT(" -compressionlevel=%d"), Config.CompressionLevel);
		}
	}

	if (!Config.EncryptionKey.IsEmpty() && !CryptoKeyPath.IsEmpty())
	{
		CmdLine += FString::Printf(TEXT(" -encrypt -cryptokeys=\"%s\""), *CryptoKeyPath);

		if (Config.bEncryptIndex)
		{
			CmdLine += TEXT(" -encryptindex");
		}
	}

	return CmdLine;
}

bool FHotUpdateIoStoreBuilder::ExecuteUnrealPak(
	const FString& UnrealPakPath,
	const FString& CmdLine,
	const FString& OutputPath,
	FHotUpdateIoStoreResult& OutResult)
{
	int32 ReturnCode = 0;
	FString StdOut;
	FString StdErr;

	UE_LOG(LogHotUpdateEditor, Log, TEXT("执行 UnrealPak: %s %s"), *UnrealPakPath, *CmdLine);
	FPlatformProcess::ExecProcess(*UnrealPakPath, *CmdLine, &ReturnCode, &StdOut, &StdErr);

	if (!StdOut.IsEmpty())
	{
		UE_LOG(LogHotUpdateEditor, Log, TEXT("UnrealPak 输出: %s"), *StdOut);
	}

	IPlatformFile& PlatformFile = IPlatformFile::GetPlatformPhysical();

	// 检查 IoStore 格式输出（.utoc + .ucas）
	FString UtocPath = OutputPath + TEXT(".utoc");
	FString UcasPath = OutputPath + TEXT(".ucas");
	FString PakPath = OutputPath + TEXT(".pak");

	bool bUtocExists = PlatformFile.FileExists(*UtocPath);
	bool bUcasExists = PlatformFile.FileExists(*UcasPath);
	bool bPakExists = PlatformFile.FileExists(*PakPath);

	int64 UtocSize = bUtocExists ? IFileManager::Get().FileSize(*UtocPath) : 0;
	int64 UcasSize = bUcasExists ? IFileManager::Get().FileSize(*UcasPath) : 0;
	int64 PakSize = bPakExists ? IFileManager::Get().FileSize(*PakPath) : 0;

	// UE5 IoStore 标准格式：utoc + ucas
	if (bUtocExists && bUcasExists && UtocSize > 0 && UcasSize > 0)
	{
		OutResult.UtocPath = UtocPath;
		OutResult.UcasPath = UcasPath;
		OutResult.ContainerSize = UtocSize + UcasSize + PakSize;
		UE_LOG(LogHotUpdateEditor, Log, TEXT("IoStore 容器创建成功 (utoc + ucas):"));
		UE_LOG(LogHotUpdateEditor, Log, TEXT("  .utoc: %s (%lld 字节)"), *UtocPath, UtocSize);
		UE_LOG(LogHotUpdateEditor, Log, TEXT("  .ucas: %s (%lld 字节)"), *UcasPath, UcasSize);
		UE_LOG(LogHotUpdateEditor, Log, TEXT("  .pak: %s (%lld 字节)"), *PakPath, PakSize);
		UE_LOG(LogHotUpdateEditor, Log, TEXT("  总大小: %lld 字节"), OutResult.ContainerSize);
		return true;
	}

	// UE5 IoStore 单文件格式（仅 .utoc，数据嵌入在 utoc 中）
	// 某些情况下 UnrealPak 只生成 .utoc 文件，包含所有数据
	if (bUtocExists && UtocSize > 0)
	{
		OutResult.UtocPath = UtocPath;
		OutResult.UcasPath = TEXT("");
		OutResult.ContainerSize = UtocSize;
		UE_LOG(LogHotUpdateEditor, Log, TEXT("IoStore 容器创建成功 (仅 utoc):"));
		UE_LOG(LogHotUpdateEditor, Log, TEXT("  .utoc: %s (%lld 字节)"), *UtocPath, UtocSize);
		UE_LOG(LogHotUpdateEditor, Log, TEXT("  总大小: %lld 字节"), OutResult.ContainerSize);
		return true;
	}

	// 传统 Pak 格式（仅 .pak 文件）
	if (bPakExists && PakSize > 0)
	{
		OutResult.UtocPath = PakPath;  // 用 UtocPath 存储 pak 路径（兼容旧逻辑）
		OutResult.UcasPath = TEXT("");
		OutResult.ContainerSize = PakSize;
		UE_LOG(LogHotUpdateEditor, Log, TEXT("Pak 文件已创建: %s, 大小: %lld 字节"), *PakPath, PakSize);
		return true;
	}

	UE_LOG(LogHotUpdateEditor, Error, TEXT("UnrealPak 执行失败，返回码: %d"), ReturnCode);
	UE_LOG(LogHotUpdateEditor, Error, TEXT("  .utoc 存在: %s, 大小: %lld"), bUtocExists ? TEXT("true") : TEXT("false"), UtocSize);
	UE_LOG(LogHotUpdateEditor, Error, TEXT("  .ucas 存在: %s, 大小: %lld"), bUcasExists ? TEXT("true") : TEXT("false"), UcasSize);
	UE_LOG(LogHotUpdateEditor, Error, TEXT("  .pak 存在: %s, 大小: %lld"), bPakExists ? TEXT("true") : TEXT("false"), PakSize);

	if (!StdErr.IsEmpty())
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("错误信息: %s"), *StdErr);
	}
	OutResult.ErrorMessage = FString::Printf(TEXT("UnrealPak 执行失败: %s"), *StdErr);
	return false;
}

void FHotUpdateIoStoreBuilder::CleanupTempDirectory(const FString& TempDir)
{
	IPlatformFile::GetPlatformPhysical().DeleteDirectoryRecursively(*TempDir);
}

FString FHotUpdateIoStoreBuilder::GenerateCryptoKeyFile(
	const FString& TempDir,
	const FHotUpdateIoStoreConfig& Config)
{
	if (Config.EncryptionKey.IsEmpty())
	{
		return TEXT("");
	}

	FString CryptoPath = FPaths::Combine(TempDir, TEXT("Crypto.json"));

	FString CryptoContent = FString::Printf(TEXT(
		"{\n"
		"  \"EncryptionKey\": {\n"
		"    \"Name\": \"%s\",\n"
		"    \"Guid\": \"%s\",\n"
		"    \"Key\": \"%s\"\n"
		"  },\n"
		"  \"bEnablePakSigning\": false\n"
		"}"),
		*Config.ContainerName,
		*FGuid::NewGuid().ToString(),
		*Config.EncryptionKey
	);

	if (FFileHelper::SaveStringToFile(CryptoContent, *CryptoPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		return CryptoPath;
	}

	return TEXT("");
}

void FHotUpdateIoStoreBuilder::UpdateProgress(
	const FString& Stage,
	const FString& CurrentFile,
	int32 ProcessedFiles,
	int32 TotalFiles,
	int64 ProcessedBytes,
	int64 TotalBytes)
{
	{
		FScopeLock Lock(&ProgressCriticalSection);
		CurrentProgress.CurrentStage = Stage;
		CurrentProgress.CurrentFile = CurrentFile;
		CurrentProgress.ProcessedFiles = ProcessedFiles;
		CurrentProgress.TotalFiles = TotalFiles;
		CurrentProgress.ProcessedBytes = ProcessedBytes;
		CurrentProgress.TotalBytes = TotalBytes;
		CurrentProgress.bIsComplete = (ProcessedFiles >= TotalFiles && TotalFiles > 0);
	}

	OnProgress.Broadcast(CurrentProgress);
}