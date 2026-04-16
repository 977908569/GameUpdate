// Copyright czm. All Rights Reserved.

#include "HotUpdatePackagingSettingsHelper.h"
#include "HotUpdateEditor.h"
#include "Settings/ProjectPackagingSettings.h"
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
	if (!Settings)
	{
		Result.Errors.Add(TEXT("无法获取项目打包设置"));
		return Result;
	}

	// 读取基本配置
	Result.bCookAll = Settings->bCookAll;
	Result.bCookMapsOnly = Settings->bCookMapsOnly;
	Result.bSkipEditorContent = Settings->bSkipEditorContent;

	// 1. 收集 MapsToCook 中的地图
	TArray<FString> MapPaths = CollectMapsToCook(Settings);
	Result.MapPaths = MapPaths;
	Result.AssetPaths.Append(MapPaths);

	// 2. 收集 DirectoriesToAlwaysCook 中的资源
	TArray<FString> AlwaysCookAssets = CollectAlwaysCookAssets(Settings);
	Result.AssetPaths.Append(AlwaysCookAssets);

	// 3. 如果 bCookAll 为 true，给出警告
	if (Result.bCookAll)
	{
		Result.Warnings.Add(TEXT("项目配置了 Cook All，这可能导致大量资源被打包"));
	}

	// 4. 过滤 NeverCook 目录
	FilterNeverCookAssets(Result.AssetPaths, Settings);

	// 5. 过滤编辑器内容
	if (Result.bSkipEditorContent)
	{
		FilterEditorContent(Result.AssetPaths);
	}

	// 6. 去重
	TSet<FString> UniquePaths(Result.AssetPaths);
	Result.AssetPaths = UniquePaths.Array();


	UE_LOG(LogHotUpdateEditor, Log, TEXT("解析项目打包配置完成: %d 个资源, %d 个地图"),
		Result.AssetPaths.Num(), Result.MapPaths.Num());

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

FText FHotUpdatePackagingSettingsHelper::GetSettingsSummary(UProjectPackagingSettings* Settings)
{
	if (!Settings)
	{
		return FText::FromString(TEXT("无法获取项目打包设置"));
	}

	FString Summary;

	// MapsToCook
	if (Settings->MapsToCook.Num() > 0)
	{
		Summary += FString::Printf(TEXT("地图: %d 个\n"), Settings->MapsToCook.Num());
	}

	// DirectoriesToAlwaysCook
	if (Settings->DirectoriesToAlwaysCook.Num() > 0)
	{
		Summary += FString::Printf(TEXT("AlwaysCook目录: %d 个\n"), Settings->DirectoriesToAlwaysCook.Num());
	}

	// DirectoriesToNeverCook
	if (Settings->DirectoriesToNeverCook.Num() > 0)
	{
		Summary += FString::Printf(TEXT("NeverCook目录: %d 个\n"), Settings->DirectoriesToNeverCook.Num());
	}

	// 其他配置
	if (Settings->bCookAll)
	{
		Summary += TEXT("CookAll: 是\n");
	}
	if (Settings->bSkipEditorContent)
	{
		Summary += TEXT("跳过编辑器内容: 是\n");
	}

	if (Summary.IsEmpty())
	{
		Summary = TEXT("未配置任何打包选项");
	}

	return FText::FromString(Summary);
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