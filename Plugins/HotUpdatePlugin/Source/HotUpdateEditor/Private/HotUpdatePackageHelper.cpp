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

			// 过滤掉引擎资产
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

FString FHotUpdatePackageHelper::GetCookedAssetPath(const FString& AssetPath, const FString& CookedPlatformDir)
{
	// 通过引擎挂载点系统获取源文件路径（自动处理 /Game/、/Engine/、插件等所有挂载点）
	FString SourcePath;
	if (!FPackageName::TryConvertLongPackageNameToFilename(AssetPath, SourcePath))
	{
		UE_LOG(LogHotUpdateEditor, Warning, TEXT("GetCookedAssetPath: TryConvertLongPackageNameToFilename 失败: %s"), *AssetPath);
		return TEXT("");
	}

	// 沙盒路径转换：将项目/引擎目录替换为 Cooked 输出目录（保留项目名，与引擎行为一致）
	const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	FString CookedPath;
	if (SourcePath.StartsWith(ProjectDir))
	{
		// 项目插件/资产：Saved/Cooked/{Platform}/{ProjectName}/Plugins/...
		CookedPath = FPaths::Combine(CookedPlatformDir, FString(FApp::GetProjectName()), SourcePath.RightChop(ProjectDir.Len()));
	}
	else
	{
		const FString EngineDir = FPaths::ConvertRelativePathToFull(FPaths::EngineDir());
		if (SourcePath.StartsWith(EngineDir))
		{
			// 引擎插件/资产：Saved/Cooked/{Platform}/Engine/...
			CookedPath = CookedPlatformDir / SourcePath.RightChop(EngineDir.Len());
		}
		else
		{
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("GetCookedAssetPath: 无法确定路径归属: %s"), *SourcePath);
			return TEXT("");
		}
	}

	// 直接检查文件是否存在
	if (FPaths::FileExists(CookedPath))
	{
		return CookedPath;
	}

	// 无扩展名时尝试 .umap / .uasset
	FString BasePath = FPaths::GetPath(CookedPath) / FPaths::GetBaseFilename(CookedPath);
	if (FPaths::FileExists(BasePath + FPackageName::GetMapPackageExtension()))
	{
		return BasePath + FPackageName::GetMapPackageExtension();
	}
	if (FPaths::FileExists(BasePath + FPackageName::GetAssetPackageExtension()))
	{
		return BasePath + FPackageName::GetAssetPackageExtension();
	}

	UE_LOG(LogHotUpdateEditor, Warning, TEXT("GetCookedAssetPath: 文件不存在: %s"), *CookedPath);
	return TEXT("");
}

FString FHotUpdatePackageHelper::GetAssetSourcePath(const FString& AssetPath)
{
	FString Extension = FPaths::GetExtension(AssetPath);

	// 非 UE 资产文件（如 .txt/.json）：直接返回路径，不做存在性检查
	if (!Extension.IsEmpty() && !IsUAssetExtension(Extension))
	{
		return AssetPath;
	}

	// 绝对磁盘路径（含盘符）：直接检查文件存在性
	if (AssetPath.Contains(TEXT(":/")))
	{
		if (FPaths::FileExists(AssetPath))
		{
			return AssetPath;
		}
		UE_LOG(LogHotUpdateEditor, Display, TEXT("GetAssetSourcePath: 绝对路径文件不存在: %s"), *AssetPath);
		return TEXT("");
	}

	// 虚拟路径（Long Package Name）：使用引擎标准 API 查找
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
	if (!ProjectContentDir.EndsWith(TEXT("/")))
	{
		ProjectContentDir += TEXT("/");
	}

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
	// 通过引擎挂载点系统获取源文件路径
	FString SourcePath;
	if (!FPackageName::TryConvertLongPackageNameToFilename(AssetPath, SourcePath))
	{
		UE_LOG(LogHotUpdateEditor, Warning, TEXT("GetAssetPakMountPath: TryConvertLongPackageNameToFilename 失败: %s"), *AssetPath);
		return TEXT("");
	}

	// 将源文件路径转换为 Pak 内部路径 ../../../ 格式
	const FString EngineDir = FPaths::ConvertRelativePathToFull(FPaths::EngineDir());
	const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());

	FString Result;
	if (SourcePath.StartsWith(EngineDir))
	{
		Result = TEXT("../../../Engine/") + SourcePath.RightChop(EngineDir.Len());
	}
	else if (SourcePath.StartsWith(ProjectDir))
	{
		Result = FString::Printf(TEXT("../../../%s/"), FApp::GetProjectName()) + SourcePath.RightChop(ProjectDir.Len());
	}
	else
	{
		UE_LOG(LogHotUpdateEditor, Warning, TEXT("GetAssetPakMountPath: 无法确定路径归属: %s"), *SourcePath);
		return TEXT("");
	}

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
	Result.TrimStartAndEndInline();

	if (Result.IsEmpty())
	{
		return Result;
	}

	// 已是虚拟路径（以 / 开头）
	if (Result.StartsWith(TEXT("/")))
	{
		return Result;
	}

	// 绝对磁盘路径（含盘符）：尝试转换为 Long Package Name
	if (Result.Contains(TEXT(":/")))
	{
		return FilePathToLongPackageName(Result);
	}

	// 相对路径：添加 /Game/ 前缀
	return TEXT("/Game/") + Result;
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

	// 2. Pak 挂载路径 ../../../Engine/... 或 ../../../ProjectName/...
	if (Result.StartsWith(TEXT("../../../")))
	{
		FString PakRelative = Result.RightChop(9); // 去掉 "../../../"

		// 引擎路径: ../../../Engine/Content/... → /Engine/...
		if (PakRelative.StartsWith(TEXT("Engine/Content/")))
		{
			FString PackagePath = TEXT("/Engine/") + PakRelative.RightChop(15); // 去掉 "Engine/Content/"
			if (FPackageName::TryConvertLongPackageNameToFilename(PackagePath, DiskPath))
			{
				return FPaths::ConvertRelativePathToFull(DiskPath);
			}
		}

		// 项目路径: ../../../ProjectName/Content/... → /Game/...
		FString ProjectPrefix = FString::Printf(TEXT("%s/Content/"), FApp::GetProjectName());
		if (PakRelative.StartsWith(ProjectPrefix))
		{
			FString PackagePath = TEXT("/Game/") + PakRelative.RightChop(ProjectPrefix.Len());
			if (FPackageName::TryConvertLongPackageNameToFilename(PackagePath, DiskPath))
			{
				return FPaths::ConvertRelativePathToFull(DiskPath);
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