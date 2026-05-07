// Copyright czm. All Rights Reserved.

#include "HotUpdatePackageHelper.h"
#include "HotUpdateEditor.h"
#include "HotUpdateUtils.h"
#include "HotUpdateAssetFilter.h"
#include "Core/HotUpdateFileUtils.h"
#include "Misc/MonitoredProcess.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "Misc/StringBuilder.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"

// ==================== 编译和 Cook 函数 ====================

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

		UE_LOG(LogHotUpdateEditor, Display, TEXT("增量 Cook: 只 Cook %d 个资源（含硬引用）: %s"), AssetsToCook.Num(), *PackageList);
	}
	else
	{
		Params = FString::Printf(TEXT("\"%s\" -run=cook -targetplatform=%s -NullRHI -unattended -NoSound"),
			*ProjectPath, *CookPlatform);

		UE_LOG(LogHotUpdateEditor, Display, TEXT("全量 Cook"));
	}

	UE_LOG(LogHotUpdateEditor, Display, TEXT("执行 Cook 命令: %s %s"), *ExePath, *Params);

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

TArray<FString> FHotUpdatePackageHelper::CollectDependenciesAndFilterEngine(const TArray<FString>& AssetsToCook)
{
	if (AssetsToCook.Num() == 0)
	{
		return TArray<FString>();
	}

	UE_LOG(LogHotUpdateEditor, Display, TEXT("收集依赖: 需要 Cook %d 个资源"), AssetsToCook.Num());

	TArray<FString> AssetsWithDeps;

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry* AssetRegistry = &AssetRegistryModule.Get();
	if (AssetRegistry)
	{
		// 强制刷新 AssetRegistry 确保依赖信息完整
		AssetRegistry->SearchAllAssets(true);
		while (AssetRegistry->IsLoadingAssets())
		{
			FPlatformProcess::Sleep(0.1f);
		}

		TSet<FString> AssetsSet(AssetsToCook);
		for (const FString& AssetPath : AssetsToCook)
		{
			TSet<FString> Dependencies;
			// 使用 IncludeAll 策略收集所有依赖（包括软引用，如地图中放置的 Actor）
			FHotUpdateAssetFilter::GetDependencies(AssetPath, AssetRegistry, EHotUpdateDependencyStrategy::IncludeAll, Dependencies);

			// 过滤掉引擎资产（引擎资产不需要 Cook，已在引擎 Pak 中）
			for (const FString& Dep : Dependencies)
			{
				if (!UHotUpdateFileUtils::IsEngineAsset(Dep))
				{
					AssetsSet.Add(Dep);
				}
			}
		}
		AssetsWithDeps = AssetsSet.Array();
	}
	else
	{
		AssetsWithDeps = AssetsToCook;
	}

	UE_LOG(LogHotUpdateEditor, Display, TEXT("收集依赖: 共 %d 个资源 (新增依赖 %d 个)"),
		AssetsWithDeps.Num(), AssetsWithDeps.Num() - AssetsToCook.Num());

	return AssetsWithDeps;
}

// ==================== 路径转换函数实现 ====================

// ==================== 私有辅助函数 ====================

FString FHotUpdatePackageHelper::EnsureTrailingSlash(const FString& Path)
{
	if (!Path.EndsWith(TEXT("/")))
	{
		return Path + TEXT("/");
	}
	return Path;
}

FHotUpdatePackageHelper::FNormalizedDirectories FHotUpdatePackageHelper::GetNormalizedDirectories()
{
	FNormalizedDirectories Dirs;

	Dirs.EngineDir = FPaths::EngineDir();
	Dirs.ProjectDir = FPaths::ProjectDir();
	Dirs.EnginePluginsDir = FPaths::EnginePluginsDir();
	Dirs.ProjectPluginsDir = FPaths::ProjectPluginsDir();

	Dirs.EngineDir = EnsureTrailingSlash(Dirs.EngineDir);
	Dirs.ProjectDir = EnsureTrailingSlash(Dirs.ProjectDir);
	Dirs.EnginePluginsDir = EnsureTrailingSlash(Dirs.EnginePluginsDir);
	Dirs.ProjectPluginsDir = EnsureTrailingSlash(Dirs.ProjectPluginsDir);

	return Dirs;
}

FString FHotUpdatePackageHelper::ExtractPluginsRelativePath(const FString& Path)
{
	int32 PluginsIdx = Path.Find(TEXT("Plugins/"));
	if (PluginsIdx == INDEX_NONE)
	{
		return TEXT("");
	}
	return Path.RightChop(PluginsIdx);
}

FString FHotUpdatePackageHelper::FindCookedFileWithFallback(const FString& CookedBaseDir, const FString& RelPath)
{
	// 去除可能残留的扩展名
	FString CleanRelPath = RelPath;
	CleanRelPath.RemoveFromEnd(FPackageName::GetAssetPackageExtension());
	CleanRelPath.RemoveFromEnd(FPackageName::GetMapPackageExtension());
	const FString BasePath = FPaths::Combine(CookedBaseDir, CleanRelPath);

	// 优先 .umap，然后 .uasset
	if (FPaths::FileExists(BasePath + FPackageName::GetMapPackageExtension()))
	{
		return BasePath + FPackageName::GetMapPackageExtension();
	}
	if (FPaths::FileExists(BasePath + FPackageName::GetAssetPackageExtension()))
	{
		return BasePath + FPackageName::GetAssetPackageExtension();
	}
	return TEXT("");
}

FString FHotUpdatePackageHelper::GetPluginCookedSubDir(const FString& PluginPath)
{
	// "Plugins/" 是 8 个字符，去掉后得到插件相对路径（如 "NNE/NNEDenoiser/Content/"）
	static constexpr int32 PluginsPrefixLen = 8;
	const FString PluginRelPath = PluginPath.RightChop(PluginsPrefixLen);

	// 从插件相对路径提取插件名（第一级目录）
	FString PluginName = PluginRelPath;
	int32 SlashIdx = PluginName.Find(TEXT("/"));
	if (SlashIdx != INDEX_NONE)
	{
		PluginName = PluginName.Left(SlashIdx);
	}

	// 通过 IPluginManager 查找插件，比磁盘目录检查更可靠
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(*PluginName);
	if (!Plugin.IsValid())
	{
		UE_LOG(LogHotUpdateEditor, Warning, TEXT("GetPluginCookedSubDir: 找不到插件: %s"), *PluginName);
		return TEXT("");
	}

	const FString EngineDir = FPaths::ConvertRelativePathToFull(FPaths::EngineDir());
	const FString PluginBaseDir = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir());

	if (PluginBaseDir.StartsWith(EngineDir))
	{
		return TEXT("Engine/") + PluginPath;
	}

	return FString(FApp::GetProjectName()) + TEXT("/") + PluginPath;
}

FString FHotUpdatePackageHelper::NormalizeFilePathRootToPakMount(const FString& FilePathRoot, const FString& PackageNameRoot)
{
	FString Result = FilePathRoot;
	FPaths::NormalizeFilename(Result);

	// 如果已经是 Pak 格式（以 ../../../ 开头），直接使用
	if (Result.StartsWith(TEXT("../../../")))
	{
		return EnsureTrailingSlash(Result);
	}

	FNormalizedDirectories Dirs = GetNormalizedDirectories();

	// 如果是绝对路径（包含盘符或以 / 开头的 Unix 路径），转换为相对路径
	if (Result.Contains(TEXT(":/")) || Result.StartsWith(TEXT("/")))
	{
		if (Result.StartsWith(Dirs.EngineDir))
		{
			// 引擎路径: ../../../Engine/...
			FString RelativePart = Result.RightChop(Dirs.EngineDir.Len());
			return TEXT("../../../Engine/") + RelativePart;
		}
		else if (Result.StartsWith(Dirs.ProjectDir))
		{
			// 项目路径: ../../../{ProjectName}/...
			FString RelativePart = Result.RightChop(Dirs.ProjectDir.Len());
			return FString::Printf(TEXT("../../../%s/"), FApp::GetProjectName()) + RelativePart;
		}
		else if (Result.StartsWith(Dirs.EnginePluginsDir))
		{
			// 引擎插件: ../../../Engine/Plugins/...
			FString PluginRelPath = ExtractPluginsRelativePath(Result);
			return TEXT("../../../Engine/") + PluginRelPath;
		}
		else if (Result.StartsWith(Dirs.ProjectPluginsDir))
		{
			// 项目插件: ../../../{ProjectName}/Plugins/...
			FString PluginRelPath = ExtractPluginsRelativePath(Result);
			return FString::Printf(TEXT("../../../%s/"), FApp::GetProjectName()) + PluginRelPath;
		}
		else
		{
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("NormalizeFilePathRootToPakMount: 无法识别的绝对路径: %s"), *FilePathRoot);
			return TEXT("");
		}
	}

	// 其他相对路径格式，尝试根据 PackageNameRoot 推导 Pak 格式
	if (PackageNameRoot == TEXT("/Game/"))
	{
		return FString::Printf(TEXT("../../../%s/Content/"), FApp::GetProjectName());
	}
	else if (PackageNameRoot == TEXT("/Engine/"))
	{
		return TEXT("../../../Engine/Content/");
	}
	else if (PackageNameRoot.Contains(TEXT("/Plugins/")))
	{
		// 插件路径，从 FilePathRoot 提取 Plugins/ 部分并转换
		FString PluginRelPath = ExtractPluginsRelativePath(Result);
		if (PluginRelPath.IsEmpty())
		{
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("NormalizeFilePathRootToPakMount: 无法提取插件路径: %s"), *FilePathRoot);
			return TEXT("");
		}

		// 判断是引擎插件还是项目插件
		if (Result.StartsWith(Dirs.EnginePluginsDir))
		{
			return TEXT("../../../Engine/") + PluginRelPath;
		}
		else if (Result.StartsWith(Dirs.ProjectPluginsDir))
		{
			return FString::Printf(TEXT("../../../%s/"), FApp::GetProjectName()) + PluginRelPath;
		}
		else if (Result.StartsWith(TEXT("../../../")))
		{
			// 已经是 Pak 格式（前面已处理，此处为保险）
			return EnsureTrailingSlash(Result);
		}

		UE_LOG(LogHotUpdateEditor, Warning, TEXT("NormalizeFilePathRootToPakMount: 无法确定插件类型: %s"), *FilePathRoot);
		return TEXT("");
	}

	UE_LOG(LogHotUpdateEditor, Warning, TEXT("NormalizeFilePathRootToPakMount: 未知的 PackageNameRoot: %s"), *PackageNameRoot);
	return TEXT("");
}

FString FHotUpdatePackageHelper::GetCookedAssetPath(const FString& AssetPath, const FString& CookedPlatformDir)
{
	if (IsExternalAsset(AssetPath))
	{
		return TEXT("");
	}

	// 内联 IsUAsset 检查：检查扩展名是否为 UE 资产格式
	const FString Extension = FPaths::GetExtension(AssetPath);
	if (!Extension.IsEmpty() && !IsUAssetExtension(Extension))
	{
		return TEXT("");
	}

	TStringBuilder<256> PackageNameRoot, FilePathRoot, RelPath;
	if (!FPackageName::TryGetMountPointForPath(AssetPath, PackageNameRoot, FilePathRoot, RelPath))
	{
		UE_LOG(LogHotUpdateEditor, Warning, TEXT("GetCookedAssetPath: TryGetMountPointForPath 失败: %s"), *AssetPath);
		return TEXT("");
	}

	const FString RootStr = FString(PackageNameRoot);
	const FString FilePathRootStr = FString(FilePathRoot);
	FString CookedBaseDir;

	// Cooked 目录结构: {CookedPlatformDir}/{MountPoint}/Content/...
	// MountPoint 可以是 ProjectName、Engine 或 Plugin 相对路径
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
		// 插件路径：从 FilePathRoot 提取 Plugins/ 子串，委托 GetPluginCookedSubDir 确定 Cooked 子目录
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
		// 其他挂载点（非标准插件路径）：通过 IPluginManager 按挂载点查找插件
		FString MountPoint = RootStr.StartsWith(TEXT("/")) ? RootStr.LeftChop(1) : RootStr;
		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(*MountPoint);
		if (Plugin.IsValid())
		{
			const FString EngineDir = FPaths::ConvertRelativePathToFull(FPaths::EngineDir());
			const FString PluginBaseDir = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir());
			FString SubDir;
			if (PluginBaseDir.StartsWith(EngineDir))
			{
				SubDir = TEXT("Engine/Plugins/") + Plugin->GetName();
			}
			else
			{
				SubDir = FString(FApp::GetProjectName()) + TEXT("/Plugins/") + Plugin->GetName();
			}
			CookedBaseDir = FPaths::Combine(CookedPlatformDir, SubDir, TEXT("Content"));
		}
		else
		{
			// 最终 fallback：去掉 PackageNameRoot 开头的 / 后直接使用
			FString CleanRoot = RootStr;
			if (CleanRoot.StartsWith(TEXT("/")))
			{
				CleanRoot = CleanRoot.RightChop(1);
			}
			CookedBaseDir = FPaths::Combine(CookedPlatformDir, CleanRoot);
		}
	}

	FString Result = FindCookedFileWithFallback(CookedBaseDir, FString(RelPath));
	if (Result.IsEmpty())
	{
		UE_LOG(LogHotUpdateEditor, Warning, TEXT("GetCookedAssetPath: 文件不存在 (AssetPath: %s, CookedBaseDir: %s, RelPath: %s)"),
			*AssetPath, *CookedBaseDir, *FString(RelPath));
	}
	return Result;
}

FString FHotUpdatePackageHelper::GetAssetSourcePath(const FString& AssetPath)
{
	// 内联 IsUAsset 检查：检查扩展名是否为 UE 资产格式
	FString Extension = FPaths::GetExtension(AssetPath);
	bool bIsUAsset = Extension.IsEmpty() || IsUAssetExtension(Extension);
	if (!bIsUAsset)
	{
		return AssetPath;
	}

	FString NormalizedPath = AssetPath;
	FPaths::NormalizeFilename(NormalizedPath);

	// 情况1：已经是绝对路径（磁盘路径），直接检查文件是否存在
	if (NormalizedPath.Contains(TEXT(":/")) || NormalizedPath.StartsWith(TEXT("/")))
	{
		// 可能是绝对路径（如 E:/...）或虚拟路径（如 /Game/...）
		// 绝对路径特征：包含 :/ 盘符分隔符
		if (NormalizedPath.Contains(TEXT(":/")))
		{
			// 绝对路径：直接使用
			if (FPaths::FileExists(NormalizedPath))
			{
				return NormalizedPath;
			}
			// 尝试去掉扩展名再检查
			FString FallbackResult = FindCookedFileWithFallback(NormalizedPath, FString());
			if (!FallbackResult.IsEmpty())
			{
				return FallbackResult;
			}
			UE_LOG(LogHotUpdateEditor, Display, TEXT("GetAssetSourcePath: 绝对路径文件不存在: %s"), *NormalizedPath);
			return TEXT("");
		}
	}

	// 情况2：虚拟路径（Long Package Name），使用引擎标准 API 查找
	FString Filename;
	if (FPackageName::DoesPackageExist(AssetPath, &Filename))
	{
		return FPaths::ConvertRelativePathToFull(Filename);
	}

	UE_LOG(LogHotUpdateEditor, Display, TEXT("GetAssetSourcePath FAILED: %s"), *AssetPath);
	return TEXT("");
}

FString FHotUpdatePackageHelper::FilePathToLongPackageName(const FString& FileName)
{
	FString Result = FileName;
	FPaths::NormalizeFilename(Result);

	// 方式1：UE 标准 API（适用于已注册的 Mount Point）
	FString LongPackageName;
	if (FPackageName::TryConvertFilenameToLongPackageName(Result, LongPackageName))
	{
		return LongPackageName;
	}

	// 方式2：通过 Mount Point 解析（支持引擎、项目、插件路径）
	TStringBuilder<256> PackageNameRoot, FilePathRoot, RelPath;
	if (FPackageName::TryGetMountPointForPath(Result, PackageNameRoot, FilePathRoot, RelPath))
	{
		FString AssetPath = FString(PackageNameRoot) + FString(RelPath);
		// 移除扩展名，返回 Long Package Name 格式
		AssetPath.RemoveFromEnd(FPackageName::GetAssetPackageExtension());
		AssetPath.RemoveFromEnd(FPackageName::GetMapPackageExtension());
		return AssetPath;
	}

	UE_LOG(LogHotUpdateEditor, Warning, TEXT("FilePathToLongPackageName: 无法解析资产路径: %s"), *Result);
	return Result;
}

FString FHotUpdatePackageHelper::FilePathToContentMountPath(const FString& FileName)
{
	FString Result = FileName;
	FPaths::NormalizeFilename(Result);

	// 检查是否在项目 Content 目录下
	FString ProjectContentDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
	FPaths::NormalizeFilename(ProjectContentDir);
	ProjectContentDir = EnsureTrailingSlash(ProjectContentDir);

	if (Result.StartsWith(ProjectContentDir))
	{
		FString RelativePath = Result.RightChop(ProjectContentDir.Len());
		// 返回虚拟路径格式: /Game/{RelativePath}（不含扩展名）
		FString VirtualPath = TEXT("/Game/") + RelativePath;
		VirtualPath.RemoveFromEnd(FPackageName::GetAssetPackageExtension());
		VirtualPath.RemoveFromEnd(FPackageName::GetMapPackageExtension());
		// 对于非资产文件（如 .txt），保留扩展名，因为后续 IoStoreBuilder 会正确处理
		return VirtualPath;
	}

	UE_LOG(LogHotUpdateEditor, Warning, TEXT("FilePathToContentMountPath: 文件不在项目 Content 目录: %s"), *Result);
	return TEXT("");
}

FString FHotUpdatePackageHelper::GetAssetPakMountPath(const FString& AssetPath)
{
	TStringBuilder<256> PackageNameRoot, FilePathRoot, RelPath;
	if (!FPackageName::TryGetMountPointForPath(AssetPath, PackageNameRoot, FilePathRoot, RelPath))
	{
		UE_LOG(LogHotUpdateEditor, Warning, TEXT("GetAssetPakMountPath: TryGetMountPointForPath 失败: %s"), *AssetPath);
		return TEXT("");
	}

	FString FilePathRootStr = FString(FilePathRoot);
	FString RootStr = FString(PackageNameRoot);

	// 使用统一的规范化函数将 FilePathRoot 转换为 Pak 格式
	FString PakMountRoot = NormalizeFilePathRootToPakMount(FilePathRootStr, RootStr);
	if (PakMountRoot.IsEmpty())
	{
		return TEXT("");
	}

	// 拼接完整 Pak 内部路径
	FString Result = PakMountRoot + FString(RelPath);
	FPaths::NormalizeFilename(Result);

	return Result;
}

// ==================== 辅助判断函数实现 ====================

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

bool FHotUpdatePackageHelper::IsValidPackagePath(const FString& AssetPath)
{
	// 排除外部资产
	if (IsExternalAsset(AssetPath))
	{
		return false;
	}

	FString Extension = FPaths::GetExtension(AssetPath);

	// 虚拟路径（无扩展名）：用引擎标准 API 验证路径格式和挂载点
	if (Extension.IsEmpty())
	{
		return FPackageName::IsValidLongPackageName(AssetPath, false);
	}

	// 磁盘路径：需有 .uasset/.umap 扩展名
	if (!IsUAssetExtension(Extension))
	{
		return false;
	}

	FString LongPackageName;
	return FPackageName::TryConvertFilenameToLongPackageName(AssetPath, LongPackageName);
}

// ==================== 资产类型判断函数实现 ====================

bool FHotUpdatePackageHelper::IsUAssetExtension(const FString& Extension)
{
	return FPackageName::IsPackageExtension(*Extension);
}

bool FHotUpdatePackageHelper::IsUAssetFile(const FString& FilePath)
{
	// 使用引擎标准 API 检查磁盘文件扩展名
	if (FPackageName::IsPackageFilename(FilePath))
	{
		return true;
	}
	// 虚拟路径（无扩展名，以 / 开头）是 UE 资产
	FString Extension = FPaths::GetExtension(FilePath);
	return Extension.IsEmpty() && FilePath.StartsWith(TEXT("/"));
}

// ==================== 新增路径转换函数实现 ====================

FString FHotUpdatePackageHelper::NormalizeAssetPath(const FString& Path)
{
	FString Result = Path;

	// 去除前后空格
	Result.TrimStartAndEndInline();

	if (Result.IsEmpty())
	{
		return Result;
	}

	// 如果不以 / 开头，添加 /Game/ 前缀
	if (!Result.StartsWith(TEXT("/")))
	{
		Result = TEXT("/Game/") + Result;
	}

	return Result;
}

FString FHotUpdatePackageHelper::VirtualPathToDiskPath(const FString& VirtualPath)
{
	FString Result = VirtualPath;
	FPaths::NormalizeFilename(Result);

	// 1. 转换长包名路径（/Game/、/Engine/、/PluginName/ 等）
	FString DiskPath;
	if (FPackageName::TryConvertLongPackageNameToFilename(Result, DiskPath))
	{
		DiskPath = FPaths::ConvertRelativePathToFull(DiskPath);
		FPaths::NormalizeFilename(DiskPath);
		return DiskPath;
	}

	// 2. Pak 挂载路径 ../../../ProjectName/Content/...
	if (Result.StartsWith(TEXT("../../../")))
	{
		FString ProjectName = FApp::GetProjectName();
		FString Prefix = FString::Printf(TEXT("../../../%s/Content/"), *ProjectName);
		if (Result.StartsWith(Prefix))
		{
			FString RelativePath = Result.RightChop(Prefix.Len());
			FString PackagePath = TEXT("/Game/") + RelativePath;
			if (FPackageName::TryConvertLongPackageNameToFilename(PackagePath, DiskPath))
			{
				DiskPath = FPaths::ConvertRelativePathToFull(DiskPath);
				FPaths::NormalizeFilename(DiskPath);
				return DiskPath;
			}
		}

		UE_LOG(LogHotUpdateEditor, Warning, TEXT("VirtualPathToDiskPath: 无法识别的 Pak 挂载路径: %s"), *VirtualPath);
		return TEXT("");
	}

	// 3. 相对路径，相对于项目目录
	if (FPaths::IsRelative(Result))
	{
		Result = FPaths::ProjectDir() + Result;
		Result = FPaths::ConvertRelativePathToFull(Result);
		FPaths::NormalizeFilename(Result);
		return Result;
	}

	// 4. 已是绝对路径
	FPaths::NormalizeFilename(Result);
	return Result;
}

// ==================== 平台目录函数实现 ====================

FString FHotUpdatePackageHelper::GetPlatformDirName(const EHotUpdatePlatform Platform, const EHotUpdateAndroidTextureFormat TextureFormat)
{
	switch (Platform)
	{
	case EHotUpdatePlatform::Windows:
		return TEXT("Windows");
	case EHotUpdatePlatform::Android:
		{
			if (TextureFormat != EHotUpdateAndroidTextureFormat::Multi)
			{
				switch (TextureFormat)
				{
				case EHotUpdateAndroidTextureFormat::ETC2: return TEXT("Android_ETC2");
				case EHotUpdateAndroidTextureFormat::ASTC: return TEXT("Android_ASTC");
				case EHotUpdateAndroidTextureFormat::DXT:  return TEXT("Android_DXT");
				default: break;
				}
			}
			return TEXT("Android");
		}
	case EHotUpdatePlatform::IOS:
		return TEXT("IOS");
	default:
		return TEXT("Windows");
	}
}

FString FHotUpdatePackageHelper::GetCookedPlatformDir(const EHotUpdatePlatform Platform)
{
	return GetCookedPlatformDir(Platform, EHotUpdateAndroidTextureFormat::Multi);
}

FString FHotUpdatePackageHelper::GetCookedPlatformDir(const EHotUpdatePlatform Platform, const EHotUpdateAndroidTextureFormat AndroidTextureFormat)
{
	return FPaths::ProjectSavedDir() / TEXT("Cooked") / GetPlatformDirName(Platform, AndroidTextureFormat);
}