// Copyright czm. All Rights Reserved.

#include "HotUpdatePackagingSettingsHelper.h"
#include "HotUpdateEditor.h"
#include "Settings/ProjectPackagingSettings.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

UProjectPackagingSettings* FHotUpdatePackagingSettingsHelper::GetPackagingSettings()
{
	return GetMutableDefault<UProjectPackagingSettings>();
}

FHotUpdatePackagingSettingsResult FHotUpdatePackagingSettingsHelper::ParsePackagingSettings(bool bIncludeDependencies)
{
	FHotUpdatePackagingSettingsResult Result;

	UProjectPackagingSettings* Settings = GetPackagingSettings();
	if (!Settings){
		Result.Errors.Add(TEXT("无法获取项目打包设置"));
		return Result;
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry* AssetRegistry = &AssetRegistryModule.Get();

	// 强制刷新 AssetRegistry
	AssetRegistry->SearchAllAssets(true);
	while (AssetRegistry->IsLoadingAssets())
	{
		FPlatformProcess::Sleep(0.1f);
	}

	// 读取基本配置
	Result.bSkipEditorContent = Settings->bSkipEditorContent;

	// 1. 收集 MapsToCook 中的地图
	TArray<FString> MapPaths = CollectMapsToCook(Settings);
	Result.MapPaths = MapPaths;
	Result.AssetPaths.Append(MapPaths);

	// 2. 收集 DirectoriesToAlwaysCook 中的资源（bCookMapsOnly 时跳过）
	TArray<FString> AlwaysCookAssets = CollectAlwaysCookAssets(Settings);
	Result.AssetPaths.Append(AlwaysCookAssets);

	// 3. 过滤 NeverCook 目录
	FilterNeverCookAssets(Result.AssetPaths, Settings);

	// 4. 过滤编辑器内容
	if (Result.bSkipEditorContent){
		FilterEditorContent(Result.AssetPaths);
	}

	// 5. 去重
	TSet<FString> UniquePaths(Result.AssetPaths);
	Result.AssetPaths = UniquePaths.Array();

	// 6. 解析资源依赖（Hard + Soft，因为 Cooker 会同时打包软引用资源）
	if (bIncludeDependencies)
	{
		TSet<FString> AllPaths(Result.AssetPaths);
		for (const FString& AssetPath : Result.AssetPaths)
		{
			TArray<FName> Dependencies;
			if (AssetRegistry->GetDependencies(FName(*AssetPath), Dependencies, UE::AssetRegistry::EDependencyCategory::Package))
			{
				for (const FName& Dep : Dependencies)
				{
					FString DepStr = Dep.ToString();
					AllPaths.Add(DepStr);
				}
			}
		}
		Result.AssetPaths = AllPaths.Array();
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("解析项目打包配置完成: %d 个资源, %d 个地图"), Result.AssetPaths.Num(), Result.MapPaths.Num());

	return Result;
}

TArray<FString> FHotUpdatePackagingSettingsHelper::CollectMapsToCook(UProjectPackagingSettings* Settings)
{
	TArray<FString> Result;

	if (!Settings)
	{
		return Result;
	}

	for (const FFilePath& MapPath : Settings->MapsToCook)
	{
		FString NormalizedPath = NormalizeAssetPath(MapPath.FilePath);
		if (!NormalizedPath.IsEmpty())
		{
			Result.Add(NormalizedPath);
			UE_LOG(LogHotUpdateEditor, Verbose, TEXT("添加地图: %s"), *NormalizedPath);
		}
	}

	return Result;
}

TArray<FString> FHotUpdatePackagingSettingsHelper::CollectAlwaysCookAssets(UProjectPackagingSettings* Settings)
{
	TArray<FString> Result;

	if (!Settings)
	{
		return Result;
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry* AssetRegistry = &AssetRegistryModule.Get();

	for (const FDirectoryPath& Directory : Settings->DirectoriesToAlwaysCook)
	{
		FString Path = Directory.Path;
		if (Path.IsEmpty())
		{
			continue;
		}

		// 确保路径以 / 开头
		FString NormalizedPath = NormalizeAssetPath(Path);
		if (!NormalizedPath.StartsWith(TEXT("/")))
		{
			NormalizedPath = TEXT("/") + NormalizedPath;
		}

		// 使用 AssetRegistry 收集目录下所有资源
		FARFilter Filter;
		Filter.PackagePaths.Add(FName(*NormalizedPath));
		Filter.bRecursivePaths = true;

		TArray<FAssetData> AssetDataList;
		AssetRegistry->GetAssets(Filter, AssetDataList);

		for (const FAssetData& AssetData : AssetDataList)
		{
			FString PackageName = AssetData.PackageName.ToString();
			Result.Add(PackageName);
		}

		UE_LOG(LogHotUpdateEditor, Verbose, TEXT("从目录 %s 收集到 %d 个资源"), *NormalizedPath, AssetDataList.Num());
	}

	return Result;
}

bool FHotUpdatePackagingSettingsHelper::ShouldExcludeAsset(const FString& AssetPath, UProjectPackagingSettings* Settings)
{
	if (!Settings)
	{
		return false;
	}

	for (const FDirectoryPath& NeverDir : Settings->DirectoriesToNeverCook)
	{
		FString NeverPath = NeverDir.Path;
		if (NeverPath.IsEmpty())
		{
			continue;
		}

		if (!NeverPath.StartsWith(TEXT("/")))
		{
			NeverPath = TEXT("/") + NeverPath;
		}

		if (AssetPath.StartsWith(NeverPath))
		{
			return true;
		}
	}

	return false;
}

void FHotUpdatePackagingSettingsHelper::FilterNeverCookAssets(TArray<FString>& AssetPaths, UProjectPackagingSettings* Settings)
{
	if (!Settings)
	{
		return;
	}

	int32 RemovedCount = 0;
	AssetPaths.RemoveAll([&Settings, &RemovedCount](const FString& Path)
	{
		if (ShouldExcludeAsset(Path, Settings))
		{
			RemovedCount++;
			return true;
		}
		return false;
	});

	if (RemovedCount > 0)
	{
		UE_LOG(LogHotUpdateEditor, Verbose, TEXT("过滤掉 %d 个 NeverCook 目录中的资源"), RemovedCount);
	}
}

void FHotUpdatePackagingSettingsHelper::FilterEditorContent(TArray<FString>& AssetPaths)
{
	int32 RemovedCount = 0;
	AssetPaths.RemoveAll([&RemovedCount](const FString& Path)
	{
		if (IsEditorContent(Path))
		{
			RemovedCount++;
			return true;
		}
		return false;
	});

	if (RemovedCount > 0)
	{
		UE_LOG(LogHotUpdateEditor, Verbose, TEXT("过滤掉 %d 个编辑器内容资源"), RemovedCount);
	}
}

FString FHotUpdatePackagingSettingsHelper::NormalizeAssetPath(const FString& Path)
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

TArray<FString> FHotUpdatePackagingSettingsHelper::CollectStagedFilePaths()
{
	TArray<FString> StagedFilePaths;

	if (const UProjectPackagingSettings* PackagingSettings = GetDefault<UProjectPackagingSettings>())
	{
		IPlatformFile& PlatformFile = IPlatformFile::GetPlatformPhysical();
		FString ProjectName(FApp::GetProjectName());
		FString ContentDir = FPaths::ProjectContentDir();

		// 文件访问器，递归枚举目录中的所有文件
		struct FStagedFileVisitor : public IPlatformFile::FDirectoryVisitor
		{
			TArray<FString>& OutFiles;
			FStagedFileVisitor(TArray<FString>& InOutFiles) : OutFiles(InOutFiles) {}
			virtual bool Visit(const TCHAR* FilenameOrDirectory, bool bIsDirectory) override
			{
				if (!bIsDirectory)
				{
					OutFiles.Add(FilenameOrDirectory);
				}
				return true;
			}
		};

		auto CollectStagedDir = [&](const TArray<FDirectoryPath>& Directories)
		{
			for (const FDirectoryPath& DirPath : Directories)
			{
				if (DirPath.Path.IsEmpty()) continue;

				FString FullDir = FPaths::Combine(ContentDir, DirPath.Path);
				FPaths::NormalizeDirectoryName(FullDir);

				if (!PlatformFile.DirectoryExists(*FullDir))
				{
					UE_LOG(LogHotUpdateEditor, Warning,
						TEXT("Staged 目录不存在: %s"), *FullDir);
					continue;
				}

				TArray<FString> FoundFiles;
				FStagedFileVisitor Visitor(FoundFiles);
				PlatformFile.IterateDirectoryRecursively(*FullDir, Visitor);

				for (const FString& File : FoundFiles)
				{
					// 计算短路径: Content/Setting/ui.txt -> Game/Setting/ui.txt
					FString RelativePath = File;
					FPaths::MakePathRelativeTo(RelativePath, *ContentDir);
					FString PakPath = TEXT("Game") / RelativePath;

					StagedFilePaths.Add(PakPath);
				}
			}
		};

		// 收集 UFS Staged 文件（打包到 pak 内部）
		CollectStagedDir(PackagingSettings->DirectoriesToAlwaysStageAsUFS);
		// 收集 NonUFS Staged 文件（打包到 pak 外部，但仍需追踪）
		CollectStagedDir(PackagingSettings->DirectoriesToAlwaysStageAsNonUFS);
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("收集了 %d 个 Staged 文件（UFS/NonUFS）"), StagedFilePaths.Num());

	return StagedFilePaths;
}

bool FHotUpdatePackagingSettingsHelper::IsEditorContent(const FString& AssetPath)
{
	// 检查是否是编辑器相关路径
	if (AssetPath.Contains(TEXT("/Editor/")) ||
		AssetPath.Contains(TEXT("/EditorWidgets/")) ||
		AssetPath.Contains(TEXT("/EditorStyle/")))
	{
		return true;
	}

	return false;
}