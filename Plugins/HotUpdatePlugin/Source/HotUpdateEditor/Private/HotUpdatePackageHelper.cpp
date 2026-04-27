// Copyright czm. All Rights Reserved.

#include "HotUpdatePackageHelper.h"
#include "HotUpdateEditor.h"
#include "HotUpdateUtils.h"
#include "Misc/MonitoredProcess.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "Misc/StringBuilder.h"

FString FHotUpdatePackageHelper::ConvertAbsolutePathToPakMount(const FString& AbsolutePath, const FString& EngineDir, const FString& ProjectDir)
{
	FString NormalizedPath = AbsolutePath;
	FPaths::NormalizeDirectoryName(NormalizedPath);

	FString RelativePath;
	if (FPaths::MakePathRelativeTo(NormalizedPath, *EngineDir))
	{
		return FString::Printf(TEXT("../../../Engine/%s"), *RelativePath);
	}
	else if (FPaths::MakePathRelativeTo(NormalizedPath, *ProjectDir))
	{
		return FString::Printf(TEXT("../../../%s/%s"), FApp::GetProjectName(), *RelativePath);
	}

	return TEXT("");
}

bool FHotUpdatePackageHelper::CompileProject(EHotUpdatePlatform Platform)
{
	UE_LOG(LogHotUpdateEditor, Log, TEXT("开始编译项目..."));

	FString EngineDir = FPaths::EngineDir();
	const FString UBTPath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(EngineDir, TEXT("Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll")));

	const FString ProjectPath = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
	const FString PlatformName = HotUpdateUtils::GetPlatformDirectoryName(Platform);
	const FString BuildConfig = TEXT("Development");

	const FString Params = FString::Printf(
		TEXT("\"%s\" GameUpdate %s %s -project=\"%s\""),
		*UBTPath, *PlatformName, *BuildConfig, *ProjectPath);

	UE_LOG(LogHotUpdateEditor, Log, TEXT("执行编译: dotnet %s"), *Params);

	const FString CommandLine = FString::Printf(TEXT("/c dotnet %s"), *Params);

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

	while (Process.Update())
	{
		FPlatformProcess::Sleep(0.1f);
	}

	int32 ReturnCode = Process.GetReturnCode();

	if (ReturnCode != 0)
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("编译失败，返回码: %d"), ReturnCode);
		return false;
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("编译完成"));
	return true;
}

bool FHotUpdatePackageHelper::CookAssets(EHotUpdatePlatform Platform)
{
	return CookAssets(Platform, TArray<FString>());
}

bool FHotUpdatePackageHelper::CookAssets(EHotUpdatePlatform Platform, const TArray<FString>& AssetsToCook)
{
	UE_LOG(LogHotUpdateEditor, Log, TEXT("开始 Cook 资源..."));

	FString EngineDir = FPaths::EngineDir();
#if PLATFORM_WINDOWS
	FString ExePath = FPaths::ConvertRelativePathToFull(EngineDir / TEXT("Binaries/Win64/UnrealEditor-Cmd.exe"));
#elif PLATFORM_MAC
	FString ExePath = FPaths::ConvertRelativePathToFull(EngineDir / TEXT("Binaries/Mac/UnrealEditor-Cmd"));
#else
	FString ExePath = FPaths::ConvertRelativePathToFull(EngineDir / TEXT("Binaries/Win64/UnrealEditor-Cmd.exe"));
#endif
	const FString ProjectPath = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
	const FString CookPlatform = HotUpdateUtils::GetPlatformString(Platform);

	FString Params;
	if (AssetsToCook.Num() > 0)
	{
		FString PackageList;
		for (int32 i = 0; i < AssetsToCook.Num(); i++)
		{
			if (i > 0) PackageList += TEXT("+");
			PackageList += AssetsToCook[i];
		}

		Params = FString::Printf(TEXT("\"%s\" -run=cook -targetplatform=%s -PACKAGE=%s -cooksinglepackage -NullRHI -unattended -NoSound"),
			*ProjectPath, *CookPlatform, *PackageList);

		UE_LOG(LogHotUpdateEditor, Log, TEXT("增量 Cook: 只 Cook %d 个资源"), AssetsToCook.Num());
	}
	else
	{
		Params = FString::Printf(TEXT("\"%s\" -run=cook -targetplatform=%s -NullRHI -unattended -NoSound"),
			*ProjectPath, *CookPlatform);

		UE_LOG(LogHotUpdateEditor, Log, TEXT("全量 Cook"));
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("执行 Cook: %s %s"), *ExePath, *Params);

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

	while (Process.Update())
	{
		FPlatformProcess::Sleep(0.1f);
	}

	int32 ReturnCode = Process.GetReturnCode();

	if (ReturnCode != 0)
	{
		bool bIsIncremental = AssetsToCook.Num() > 0;
		if (bIsIncremental && ReturnCode == 1)
		{
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("增量 Cook 返回警告码 1，检查 Cook 输出..."));
			FString CookedPlatformDir = HotUpdateUtils::GetCookedPlatformDir(Platform);
			int32 FoundCount = 0;
			for (const FString& AssetPath : AssetsToCook)
			{
				FString DiskPath = GetCookedAssetPath(AssetPath, CookedPlatformDir);
				if (!DiskPath.IsEmpty() && FPaths::FileExists(*DiskPath))
				{
					FoundCount++;
				}
			}
			if (FoundCount > 0)
			{
				UE_LOG(LogHotUpdateEditor, Log, TEXT("增量 Cook: %d/%d 个目标文件已生成，视为成功"), FoundCount, AssetsToCook.Num());
				return true;
			}
		}
		UE_LOG(LogHotUpdateEditor, Error, TEXT("Cook 失败，返回码: %d"), ReturnCode);
		return false;
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("Cook 完成"));
	return true;
}

FString FHotUpdatePackageHelper::GetPluginCookedSubDir(const FString& PluginPath)
{
	const FString PluginRelPath = PluginPath.RightChop(7);

	FString EnginePluginDir = FPaths::EnginePluginsDir() / PluginRelPath;
	FString ProjectPluginDir = FPaths::ProjectPluginsDir() / PluginRelPath;

	FPaths::NormalizeDirectoryName(EnginePluginDir);
	FPaths::NormalizeDirectoryName(ProjectPluginDir);

	if (FPaths::DirectoryExists(EnginePluginDir))
	{
		return TEXT("Engine/") + PluginPath;
	}
	else if (FPaths::DirectoryExists(ProjectPluginDir))
	{
		return FString(FApp::GetProjectName()) + TEXT("/") + PluginPath;
	}

	return TEXT("");
}

FString FHotUpdatePackageHelper::GetCookedAssetPath(const FString& AssetPath, const FString& CookedPlatformDir)
{
	if (IsExternalAsset(AssetPath))
	{
		return TEXT("");
	}

	if (!IsUAsset(AssetPath))
	{
		return TEXT("");
	}

	TStringBuilder<256> PackageNameRoot, FilePathRoot, RelPath;
	if (!FPackageName::TryGetMountPointForPath(AssetPath, PackageNameRoot, FilePathRoot, RelPath))
	{
		UE_LOG(LogHotUpdateEditor, Warning, TEXT("GetCookedAssetPath: TryGetMountPointForPath 失败: %s"), *AssetPath);
		return TEXT("");
	}

	FString RootStr = FString(PackageNameRoot);
	FString FilePathRootStr = FString(FilePathRoot);
	FString CookedBaseDir;

	// 根据 PackageNameRoot 确定 Cooked 目录基础路径
	// Cooked 目录结构: {CookedPlatformDir}/{ProjectName}/Content/... 或 {CookedPlatformDir}/Engine/Content/...
	if (RootStr == TEXT("/Game/"))
	{
		// /Game/ 映射到 {ProjectName}/Content/
		CookedBaseDir = FPaths::Combine(CookedPlatformDir, FString(FApp::GetProjectName()), TEXT("Content"));
	}
	else if (RootStr == TEXT("/Engine/"))
	{
		// /Engine/ 映射到 Engine/Content/
		CookedBaseDir = FPaths::Combine(CookedPlatformDir, TEXT("Engine"), TEXT("Content"));
	}
	else if (FilePathRootStr.Contains(TEXT("Plugins/")))
	{
		// 插件路径：提取 "Plugins/..." 部分
		const int32 PluginsIdx = FilePathRootStr.Find(TEXT("Plugins/"));
		FString PluginPath = FilePathRootStr.Mid(PluginsIdx);
		FString SubDir = GetPluginCookedSubDir(PluginPath);
		if (SubDir.IsEmpty())
		{
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("GetCookedAssetPath: 插件目录不存在: %s"), *PluginPath);
			return TEXT("");
		}
		CookedBaseDir = FPaths::Combine(CookedPlatformDir, SubDir, TEXT("Content"));
	}
	else
	{
		// 其他路径：直接使用 PackageNameRoot（去掉开头的 /）
		FString CleanRoot = RootStr;
		if (CleanRoot.StartsWith(TEXT("/")))
		{
			CleanRoot = CleanRoot.RightChop(1);
		}
		CookedBaseDir = FPaths::Combine(CookedPlatformDir, CleanRoot);
	}

	// 拼接完整 Cooked 路径
	const FString CookedPath = FPaths::Combine(CookedBaseDir, RelPath);

	FString UmapPath = CookedPath + TEXT(".umap");
	if (FPaths::FileExists(UmapPath))
	{
		return UmapPath;
	}
	FString UassetPath = CookedPath + TEXT(".uasset");
	if (FPaths::FileExists(UassetPath))
	{
		return UassetPath;
	}

	UE_LOG(LogHotUpdateEditor, Warning, TEXT("GetCookedAssetPath: 文件不存在: %s (AssetPath: %s, CookedBaseDir: %s, RelPath: %s)"),
		*CookedPath, *AssetPath, *CookedBaseDir, *FString(RelPath));
	return TEXT("");
}

FString FHotUpdatePackageHelper::GetAssetSourcePath(const FString& AssetPath)
{
	if (!IsUAsset(AssetPath))
	{
		return AssetPath;
	}
	FString Filename;
	if (!FPackageName::TryConvertLongPackageNameToFilename(AssetPath, Filename))
	{
		UE_LOG(LogHotUpdateEditor, Display, TEXT("GetAssetSourcePath FAILED TryConvertLongPackageNameToFilename : %s"), *AssetPath);
		return TEXT("");
	}

	const FString AbsolutePath = FPaths::ConvertRelativePathToFull(Filename);

	if (FPaths::FileExists(AbsolutePath + TEXT(".umap")))
		return AbsolutePath + TEXT(".umap");
	if (FPaths::FileExists(AbsolutePath + TEXT(".uasset")))
		return AbsolutePath + TEXT(".uasset");

	UE_LOG(LogHotUpdateEditor, Display, TEXT("GetAssetSourcePath FAILED: %s"), *AssetPath);
	return TEXT("");
}

FString FHotUpdatePackageHelper::FileNameToAssetPath(const FString& FileName)
{
	FString Result = FileName;
	FPaths::NormalizeFilename(Result);

	// Staged 文件（非 .uasset/.umap）：使用 Pak 路径格式 /{ProjectName}/Content/xxx
	if (!Result.EndsWith(TEXT(".uasset")) && !Result.EndsWith(TEXT(".umap")))
	{
		const FString ProjectContentDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
		if (Result.StartsWith(ProjectContentDir))
		{
			FString RelativePath = Result.RightChop(ProjectContentDir.Len());
			return TEXT("/") + FString(FApp::GetProjectName()) + TEXT("/Content/") + RelativePath;
		}
		return Result;
	}

	// UE 资产：使用引擎标准 API 转换为 Long Package Name
	FString LongPackageName;
	if (FPackageName::TryConvertFilenameToLongPackageName(Result, LongPackageName))
	{
		UE_LOG(LogHotUpdateEditor, Display, TEXT("FileNameToAssetPath: %s -> %s"), *FileName, *LongPackageName);
		return LongPackageName;
	}

	UE_LOG(LogHotUpdateEditor, Display, TEXT("TryConvertFilenameToLongPackageName FAILED: %s"), *Result);
	return Result;
}

FString FHotUpdatePackageHelper::GetAssetPakMountPath(const FString& AssetPath)
{
	TStringBuilder<256> PackageNameRoot, FilePathRoot, RelPath;
	if (!FPackageName::TryGetMountPointForPath(AssetPath, PackageNameRoot, FilePathRoot, RelPath))
	{
		return TEXT("");
	}

	FString FilePathRootStr = FString(FilePathRoot);
	FString RootStr = FString(PackageNameRoot);

	// 检查是否为绝对路径
	if (FilePathRootStr.Contains(TEXT(":/")) || FilePathRootStr.Contains(TEXT(":\\")))
	{
		// 绝对路径：使用辅助函数转换为 Pak 挂载格式
		FString EngineDir = FPaths::EngineDir();
		FString ProjectDir = FPaths::ProjectDir();
		FPaths::NormalizeDirectoryName(EngineDir);
		FPaths::NormalizeDirectoryName(ProjectDir);

		FilePathRootStr = ConvertAbsolutePathToPakMount(FilePathRootStr, EngineDir, ProjectDir);
		if (FilePathRootStr.IsEmpty())
		{
			return TEXT("");
		}
	}
	else if (!FilePathRootStr.StartsWith(TEXT("../../../")))
	{
		// 非标准相对路径格式：根据 PackageNameRoot 推导标准 Pak 格式
		if (RootStr == TEXT("/Game/"))
		{
			FilePathRootStr = FString::Printf(TEXT("../../../%s/Content/"), FApp::GetProjectName());
		}
		else if (RootStr == TEXT("/Engine/"))
		{
			FilePathRootStr = TEXT("../../../Engine/Content/");
		}
	}

	FString Result = FilePathRootStr + FString(RelPath);
	Result.ReplaceCharInline('\\', '/');

	// 防御性检查：确保 /Game/ 路径包含 /Content/
	// 如果 FilePathRootStr 不包含 /Content/，可能导致 Pak 内部路径缺少该段
	if (RootStr == TEXT("/Game/") && !Result.Contains(TEXT("/Content/")))
	{
		// 手动修复：例如 ../../../GameUpdate/Maps/Start -> ../../../GameUpdate/Content/Maps/Start
		// 找到项目名后的第一个 / 位置，插入 /Content
		FString Prefix = FString::Printf(TEXT("../../../%s"), FApp::GetProjectName());
		int32 PrefixLen = Prefix.Len();
		if (Result.StartsWith(Prefix) && Result.Len() > PrefixLen)
		{
			// 在项目名后插入 /Content
			Result = Prefix + TEXT("/Content") + Result.Mid(PrefixLen);
			UE_LOG(LogHotUpdateEditor, Log, TEXT("GetAssetPakMountPath: 修复缺少 /Content/ 的路径: %s"), *Result);
		}
	}

	return Result;
}

bool FHotUpdatePackageHelper::IsUAsset(const FString& AssetPath)
{
	FString Extension = TEXT("");
	if (AssetPath.Contains(TEXT(".")))
	{
		FPaths::GetExtension(AssetPath);
	}else{
		FString Filename;
		if (!FPackageName::TryConvertLongPackageNameToFilename(AssetPath, Filename))
		{
			return false;
		}
		const FString AbsolutePath = FPaths::ConvertRelativePathToFull(Filename);
		if (FPaths::FileExists(AbsolutePath + TEXT(".umap")))
		{
			Extension = TEXT("umap");
		}else if (FPaths::FileExists(AbsolutePath + TEXT(".uasset")))
		{
			Extension = TEXT("umap");
		}
	}

	if (Extension == TEXT("umap") || Extension == TEXT("uasset"))
	{
		return true;
	}
	return false;
}

bool FHotUpdatePackageHelper::IsExternalAsset(const FString& AssetPath)
{
	if (FPackageName::IsScriptPackage(AssetPath) || FPackageName::IsMemoryPackage(AssetPath))
	{
		return true;
	}
	if (AssetPath.Contains(FPackagePath::GetExternalActorsFolderName()) || AssetPath.Contains(FPackagePath::GetExternalObjectsFolderName()))
	{
		return true;
	}
	return false;
}