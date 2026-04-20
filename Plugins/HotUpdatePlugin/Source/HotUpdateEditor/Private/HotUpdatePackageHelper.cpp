// Copyright czm. All Rights Reserved.

#include "HotUpdatePackageHelper.h"
#include "HotUpdateEditor.h"
#include "HotUpdateUtils.h"
#include "Core/HotUpdateFileUtils.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/MonitoredProcess.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "Misc/SecureHash.h"
#include "JsonObjectConverter.h"
#include "Interfaces/IPluginManager.h"

bool FHotUpdatePackageHelper::CompileProject(EHotUpdatePlatform Platform)
{
	UE_LOG(LogHotUpdateEditor, Log, TEXT("开始编译项目..."));

	FString EngineDir = FPaths::EngineDir();
	FString UBTPath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(EngineDir, TEXT("Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll")));

	FString ProjectPath = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
	FString PlatformName = HotUpdateUtils::GetPlatformDirectoryName(Platform);
	FString BuildConfig = TEXT("Development");

	FString Params = FString::Printf(
		TEXT("\"%s\" GameUpdate %s %s -project=\"%s\""),
		*UBTPath, *PlatformName, *BuildConfig, *ProjectPath);

	UE_LOG(LogHotUpdateEditor, Log, TEXT("执行编译: dotnet %s"), *Params);

	FString CommandLine = FString::Printf(TEXT("/c dotnet %s"), *Params);

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
	FString ProjectPath = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
	FString CookPlatform = HotUpdateUtils::GetPlatformString(Platform);

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
				FString DiskPath = GetAssetDiskPath(AssetPath, CookedPlatformDir);
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

FString FHotUpdatePackageHelper::GetAssetDiskPath(const FString& AssetPath, const FString& CookedPlatformDir)
{
	if (CookedPlatformDir.IsEmpty())
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("GetAssetDiskPath: CookedPlatformDir 不能为空"));
		return TEXT("");
	}

	FString ResolvedPath;
	if (!FPackageName::TryConvertLongPackageNameToFilename(AssetPath, ResolvedPath, TEXT("")))
	{
		return TEXT("");
	}

	FString RelativePath;

	if (ResolvedPath.StartsWith(TEXT("../../../")))
	{
		RelativePath = ResolvedPath.Mid(9);
	}
	else if (ResolvedPath.StartsWith(TEXT("../../")))
	{
		FString Suffix = ResolvedPath.Mid(6);

		FString PluginName;
		{
			FString Rest = AssetPath.Mid(1);
			int32 SlashIdx;
			if (Rest.FindChar(TEXT('/'), SlashIdx))
				PluginName = Rest.Left(SlashIdx);
			else
				PluginName = Rest;
		}

		bool bIsProjectPlugin = false;
		if (!PluginName.IsEmpty())
		{
			TArray<TSharedRef<IPlugin>> Plugins = IPluginManager::Get().GetEnabledPlugins();
			for (const TSharedRef<IPlugin>& P : Plugins)
			{
				if (P.Get().GetName() == PluginName)
				{
					EPluginType Type = P.Get().GetType();
					bIsProjectPlugin = (Type == EPluginType::Project || Type == EPluginType::Mod);
					break;
				}
			}
		}

		if (bIsProjectPlugin)
			RelativePath = FString(FApp::GetProjectName()) / Suffix;
		else
			RelativePath = TEXT("Engine") / Suffix;

		if (PluginName.IsEmpty())
		{
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("GetAssetDiskPath: 无法确定插件类型，默认引擎路径: %s"), *AssetPath);
		}
	}
	else
	{
		auto NormalizeDir = [](FString Dir) { FPaths::NormalizeDirectoryName(Dir); return Dir; };

		if (AssetPath.StartsWith(TEXT("/Game/")))
		{
			FString ProjectContentDir = NormalizeDir(FPaths::ProjectContentDir());
			if (ResolvedPath.StartsWith(ProjectContentDir))
				RelativePath = FString(FApp::GetProjectName()) / TEXT("Content") + ResolvedPath.RightChop(ProjectContentDir.Len());
			else
				RelativePath = FString(FApp::GetProjectName()) / TEXT("Content") + AssetPath.Mid(5);
		}
		else if (AssetPath.StartsWith(TEXT("/Engine/")))
		{
			FString EngineContentDir = NormalizeDir(FPaths::EngineContentDir());
			if (ResolvedPath.StartsWith(EngineContentDir))
				RelativePath = TEXT("Engine/Content") + ResolvedPath.RightChop(EngineContentDir.Len());
			else
				RelativePath = TEXT("Engine/Content") / AssetPath.Mid(8);
		}
		else
		{
			FString ProjectDir = NormalizeDir(FPaths::ProjectDir());
			if (ResolvedPath.StartsWith(ProjectDir))
			{
				FString PathUnderProject = ResolvedPath.RightChop(ProjectDir.Len());

				FString PluginName;
				{
					FString Rest = AssetPath.Mid(1);
					int32 SlashIdx;
					if (Rest.FindChar(TEXT('/'), SlashIdx))
						PluginName = Rest.Left(SlashIdx);
					else
						PluginName = Rest;
				}

				bool bIsProjectPlugin = false;
				if (!PluginName.IsEmpty())
				{
					TArray<TSharedRef<IPlugin>> Plugins = IPluginManager::Get().GetEnabledPlugins();
					for (const TSharedRef<IPlugin>& P : Plugins)
					{
						if (P.Get().GetName() == PluginName)
						{
							EPluginType Type = P.Get().GetType();
							bIsProjectPlugin = (Type == EPluginType::Project || Type == EPluginType::Mod);
							break;
						}
					}
				}

				if (bIsProjectPlugin)
					RelativePath = FString(FApp::GetProjectName()) / PathUnderProject;
				else
					RelativePath = TEXT("Engine") / PathUnderProject;
			}
			else
			{
				FString EngineDir = NormalizeDir(FPaths::EngineDir());
				if (ResolvedPath.StartsWith(EngineDir))
					RelativePath = ResolvedPath.RightChop(EngineDir.Len());
				else
				{
					UE_LOG(LogHotUpdateEditor, Warning, TEXT("GetAssetDiskPath: 无法解析路径 %s (Resolved=%s)"), *AssetPath, *ResolvedPath);
					return TEXT("");
				}
			}
		}
	}

	FString CookedMapPath = FPaths::Combine(CookedPlatformDir, RelativePath + TEXT(".umap"));
	if (FPaths::FileExists(*CookedMapPath))
		return CookedMapPath;

	FString CookedAssetPath = FPaths::Combine(CookedPlatformDir, RelativePath + TEXT(".uasset"));
	if (FPaths::FileExists(*CookedAssetPath))
		return CookedAssetPath;

	return TEXT("");
}

FString FHotUpdatePackageHelper::GetAssetSourcePath(const FString& AssetPath)
{
	FString ResolvedPath;
	if (!FPackageName::TryConvertLongPackageNameToFilename(AssetPath, ResolvedPath, TEXT("")))
	{
		return TEXT("");
	}

	FString AbsolutePath = FPaths::ConvertRelativePathToFull(ResolvedPath);

	if (FPaths::FileExists(AbsolutePath + TEXT(".umap")))
		return AbsolutePath + TEXT(".umap");
	if (FPaths::FileExists(AbsolutePath + TEXT(".uasset")))
		return AbsolutePath + TEXT(".uasset");

	return TEXT("");
}

FString FHotUpdatePackageHelper::ConvertAssetPathToFileName(const FString& AssetPath, const FString& CookedPlatformDir)
{
	FString FileName = AssetPath;

	if (FileName.StartsWith(TEXT("/")))
	{
		FileName.RightChopInline(1);
	}

	FString CurrentExtension = FPaths::GetExtension(FileName);
	if (!CurrentExtension.IsEmpty() && CurrentExtension != TEXT("uasset") && CurrentExtension != TEXT("umap"))
	{
		return FileName;
	}

	FString DiskPath = GetAssetDiskPath(AssetPath, CookedPlatformDir);
	if (!DiskPath.IsEmpty() && FPaths::FileExists(*DiskPath))
	{
		FString Extension = FPaths::GetExtension(DiskPath);
		if (!Extension.IsEmpty())
		{
			FileName += TEXT(".") + Extension;
			return FileName;
		}
	}

	FileName += TEXT(".uasset");
	return FileName;
}

FString FHotUpdatePackageHelper::FileNameToAssetPath(const FString& FileName)
{
	FString AssetPath = FileName;

	if (!AssetPath.StartsWith(TEXT("/")))
	{
		AssetPath = TEXT("/") + AssetPath;
	}

	if (AssetPath.EndsWith(TEXT(".uasset")))
	{
		AssetPath.LeftChopInline(7);
	}
	else if (AssetPath.EndsWith(TEXT(".umap")))
	{
		AssetPath.LeftChopInline(5);
	}

	return AssetPath;
}