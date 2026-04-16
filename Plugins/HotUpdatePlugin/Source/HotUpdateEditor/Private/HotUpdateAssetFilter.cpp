// Copyright czm. All Rights Reserved.

#include "HotUpdateAssetFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"

DEFINE_LOG_CATEGORY_STATIC(LogHotUpdateAssetFilter, Log, All);

void FHotUpdateAssetFilter::FilterAssets(
	const TArray<FString>& InAssetPaths,
	const FHotUpdateMinimalPackageConfig& Config,
	IAssetRegistry* AssetRegistry,
	TArray<FString>& OutWhitelistAssets,
	TArray<FString>& OutExcludedAssets)
{
	TSet<FString> WhitelistSet;

	UE_LOG(LogHotUpdateAssetFilter, Log, TEXT("开始过滤资产，输入资产数: %d"), InAssetPaths.Num());

	// 1. 收集白名单目录中的资产
	if (Config.WhitelistDirectories.Num() > 0)
	{
		TArray<FString> WhitelistDirAssets = CollectAssetsFromDirectories(Config.WhitelistDirectories, AssetRegistry);
		for (const FString& Asset : WhitelistDirAssets)
		{
			WhitelistSet.Add(Asset);
		}
		UE_LOG(LogHotUpdateAssetFilter, Log, TEXT("必须包含的目录收集资产: %d"), WhitelistDirAssets.Num());
	}

	// 2. 应用白名单规则
	for (const FHotUpdateAssetFilterRule& Rule : Config.WhitelistAssets)
	{
		if (!Rule.IsValid())
		{
			continue;
		}

		for (const FString& AssetPath : InAssetPaths)
		{
			if (MatchesFilterRule(AssetPath, Rule, AssetRegistry))
			{
				WhitelistSet.Add(AssetPath);
			}
		}
	}
	UE_LOG(LogHotUpdateAssetFilter, Log, TEXT("白名单规则应用后，白名单资产数: %d"), WhitelistSet.Num());

	// 3. 根据依赖策略收集白名单资产的依赖
	TSet<FString> FinalWhitelist = WhitelistSet;
	if (Config.DependencyStrategy != EHotUpdateDependencyStrategy::None && WhitelistSet.Num() > 0)
	{
		for (const FString& Asset : WhitelistSet)
		{
			TSet<FString> Dependencies;
			GetDependencies(Asset, AssetRegistry, Config.DependencyStrategy, Config.MaxDependencyDepth, Dependencies);
			FinalWhitelist.Append(Dependencies);
		}
		UE_LOG(LogHotUpdateAssetFilter, Log, TEXT("依赖收集后，白名单资产数: %d (添加依赖数: %d)"), FinalWhitelist.Num(), FinalWhitelist.Num() - WhitelistSet.Num());
	}

	// 4. 过滤非资产路径（保留 /Game/、/Engine/、插件路径，排除 /Script/）
	TSet<FString> FilteredWhitelist;
	for (const FString& Asset : FinalWhitelist)
	{
		if (Asset.StartsWith(TEXT("/Game/")) || Asset.StartsWith(TEXT("/Engine/")) ||
			(Asset.StartsWith(TEXT("/")) && !Asset.StartsWith(TEXT("/Script/"))))
		{
			FilteredWhitelist.Add(Asset);
		}
	}

	// 5. 输出结果
	OutWhitelistAssets = FilteredWhitelist.Array();

	for (const FString& Asset : InAssetPaths)
	{
		if (!FilteredWhitelist.Contains(Asset))
		{
			OutExcludedAssets.Add(Asset);
		}
	}

	UE_LOG(LogHotUpdateAssetFilter, Log,
		TEXT("过滤完成: 白名单资产 %d 个, 排除资产 %d 个"),
		OutWhitelistAssets.Num(), OutExcludedAssets.Num());
}

bool FHotUpdateAssetFilter::MatchesFilterRule(
	const FString& AssetPath,
	const FHotUpdateAssetFilterRule& Rule,
	IAssetRegistry* AssetRegistry)
{
	if (!Rule.IsValid())
	{
		return false;
	}

	// 检查路径匹配
	bool bPathMatches = false;

	if (Rule.AssetPath.Contains(TEXT("*")))
	{
		// 通配符匹配
		bPathMatches = WildcardMatch(Rule.AssetPath, AssetPath);
	}
	else
	{
		// 精确匹配或前缀匹配
		if (Rule.bRecursive)
		{
			// 递归匹配：资产路径以规则路径开头
			bPathMatches = AssetPath.StartsWith(Rule.AssetPath);
		}
		else
		{
			// 非递归匹配：资产在规则指定的目录下（不含子目录）
			// 资产路径格式: /Game/Dir/Asset.asset
			// 规则路径格式: /Game/Dir
			int32 AssetSlashCount = 0;
			int32 RuleSlashCount = 0;
			for (int32 i = 0; i < AssetPath.Len(); i++)
			{
				if (AssetPath[i] == TEXT('/'))
				{
					AssetSlashCount++;
				}
			}
			for (int32 i = 0; i < Rule.AssetPath.Len(); i++)
			{
				if (Rule.AssetPath[i] == TEXT('/'))
			{
					RuleSlashCount++;
				}
			}

			// 非递归时，资产应该在规则的直接子目录中
			// 规则 /Game/Dir 有3个斜杠，资产 /Game/Dir/Asset.asset 有4个斜杠
			bPathMatches = AssetPath.StartsWith(Rule.AssetPath) && (AssetSlashCount <= RuleSlashCount + 1);
		}
	}

	if (!bPathMatches)
	{
		return false;
	}

	// 检查资产类型过滤
	if (Rule.AssetTypes.Num() > 0 && AssetRegistry)
	{
		FString AssetTypeName = GetAssetTypeName(AssetPath, AssetRegistry);
		if (!Rule.AssetTypes.Contains(AssetTypeName))
		{
			return false;
		}
	}

	return true;
}

bool FHotUpdateAssetFilter::IsInDirectories(
	const FString& AssetPath,
	const TArray<FDirectoryPath>& Directories,
	bool bRecursive)
{
	for (const FDirectoryPath& Dir : Directories)
	{
		if (Dir.Path.IsEmpty())
		{
			continue;
		}

		if (bRecursive)
		{
			if (AssetPath.StartsWith(Dir.Path))
			{
				return true;
			}
		}
		else
		{
			// 非递归：检查是否在直接子目录中
			if (AssetPath.StartsWith(Dir.Path))
			{
				FString RelativePath = AssetPath.RightChop(Dir.Path.Len());
				if (RelativePath.IsEmpty() || !RelativePath.Contains(TEXT("/")))
				{
					return true;
				}
			}
		}
	}
	return false;
}

TArray<FString> FHotUpdateAssetFilter::CollectAssetsFromDirectories(
	const TArray<FDirectoryPath>& Directories,
	IAssetRegistry* AssetRegistry)
{
	TArray<FString> Result;

	if (!AssetRegistry)
	{
		UE_LOG(LogHotUpdateAssetFilter, Warning, TEXT("AssetRegistry 为空，无法收集目录资产"));
		return Result;
	}

	for (const FDirectoryPath& Dir : Directories)
	{
		if (Dir.Path.IsEmpty())
		{
			continue;
		}

		// 使用 AssetRegistry 扫描目录
		FARFilter Filter;
		Filter.PackagePaths.Add(FName(*Dir.Path));
		Filter.bRecursivePaths = true;
		Filter.bIncludeOnlyOnDiskAssets = true;

		TArray<FAssetData> AssetDataList;
		AssetRegistry->GetAssets(Filter, AssetDataList);

		for (const FAssetData& AssetData : AssetDataList)
		{
			Result.Add(AssetData.PackageName.ToString());
		}
	}

	UE_LOG(LogHotUpdateAssetFilter, Log, TEXT("从 %d 个目录收集了 %d 个资产"), Directories.Num(), Result.Num());
	return Result;
}

void FHotUpdateAssetFilter::GetDependencies(
	const FString& AssetPath,
	IAssetRegistry* AssetRegistry,
	EHotUpdateDependencyStrategy Strategy,
	int32 MaxDepth,
	TSet<FString>& OutDependencies)
{
	TSet<FString> Visited;
	GetDependenciesRecursive(AssetPath, AssetRegistry, Strategy, 0, MaxDepth, OutDependencies, Visited);
}

void FHotUpdateAssetFilter::GetDependenciesRecursive(
	const FString& AssetPath,
	IAssetRegistry* AssetRegistry,
	EHotUpdateDependencyStrategy Strategy,
	int32 CurrentDepth,
	int32 MaxDepth,
	TSet<FString>& OutDependencies,
	TSet<FString>& Visited)
{
	if (!AssetRegistry)
	{
		return;
	}

	// 检查是否已访问
	if (Visited.Contains(AssetPath))
	{
		return;
	}
	Visited.Add(AssetPath);

	// 检查深度限制
	if (MaxDepth > 0 && CurrentDepth >= MaxDepth)
	{
		return;
	}

	// 根据策略获取依赖
	TArray<FName> Dependencies;
	UE::AssetRegistry::EDependencyCategory Category = UE::AssetRegistry::EDependencyCategory::Package;
	UE::AssetRegistry::FDependencyQuery Query;

	switch (Strategy)
	{
	case EHotUpdateDependencyStrategy::IncludeAll:
		Query = UE::AssetRegistry::FDependencyQuery(UE::AssetRegistry::EDependencyQuery::Hard | UE::AssetRegistry::EDependencyQuery::Soft);
		break;
	case EHotUpdateDependencyStrategy::HardOnly:
		Query = UE::AssetRegistry::FDependencyQuery(UE::AssetRegistry::EDependencyQuery::Hard);
		break;
	case EHotUpdateDependencyStrategy::SoftOnly:
		Query = UE::AssetRegistry::FDependencyQuery(UE::AssetRegistry::EDependencyQuery::Soft);
		break;
	case EHotUpdateDependencyStrategy::None:
		// 不收集依赖，直接返回
		return;
	default:
		Query = UE::AssetRegistry::FDependencyQuery(UE::AssetRegistry::EDependencyQuery::Hard);
		break;
	}

	if (AssetRegistry->GetDependencies(FName(*AssetPath), Dependencies, Category, Query))
	{
		for (const FName& Dep : Dependencies)
		{
			FString DepStr = Dep.ToString();

			// 只处理资产路径：/Game/、/Engine/、插件路径（如 /NNE/、/Water/）
			// 排除 /Script/ 路径（C++ 类型引用，不是资产）
			if (!(DepStr.StartsWith(TEXT("/Game/")) || DepStr.StartsWith(TEXT("/Engine/")) ||
				(DepStr.StartsWith(TEXT("/")) && !DepStr.StartsWith(TEXT("/Script/")))))
			{
				continue;
			}


			// 添加到结果
			OutDependencies.Add(DepStr);

			// 递归获取依赖
			GetDependenciesRecursive(DepStr, AssetRegistry, Strategy, CurrentDepth + 1, MaxDepth, OutDependencies, Visited);
		}
	}
}

FString FHotUpdateAssetFilter::GetAssetTypeName(
	const FString& AssetPath,
	IAssetRegistry* AssetRegistry)
{
	if (!AssetRegistry)
	{
		return TEXT("Unknown");
	}

	FAssetData AssetData = AssetRegistry->GetAssetByObjectPath(FSoftObjectPath(AssetPath));
	if (AssetData.IsValid())
	{
		return AssetData.AssetClassPath.GetAssetName().ToString();
	}

	return TEXT("Unknown");
}

bool FHotUpdateAssetFilter::WildcardMatch(const FString& Pattern, const FString& Text)
{
	// 简单的通配符匹配，支持 * 匹任意字符
	// 使用 UE 内置的 FString 匹配功能

	if (Pattern.IsEmpty())
	{
		return Text.IsEmpty();
	}

	if (!Pattern.Contains(TEXT("*")))
	{
		// 没有通配符，直接比较
		return Pattern.Equals(Text, ESearchCase::IgnoreCase);
	}

	// 分割模式为多个部分
	TArray<FString> PatternParts;
	Pattern.ParseIntoArray(PatternParts, TEXT("*"), true);

	// 特殊情况：只有 *
	if (PatternParts.Num() == 0)
	{
		return true;
	}

	int32 TextIndex = 0;
	bool bStartsWithWildcard = Pattern.StartsWith(TEXT("*"));
	bool bEndsWithWildcard = Pattern.EndsWith(TEXT("*"));

	for (int32 i = 0; i < PatternParts.Num(); i++)
	{
		const FString& Part = PatternParts[i];

		if (Part.IsEmpty())
		{
			continue;
		}

		// 查找部分在文本中的位置
		int32 FoundIndex = Text.Find(Part, ESearchCase::IgnoreCase, ESearchDir::FromStart, TextIndex);

		if (FoundIndex == INDEX_NONE)
		{
			return false;
		}

		// 第一个部分必须从开头匹配（除非以 * 开头）
		if (i == 0 && !bStartsWithWildcard && FoundIndex != 0)
		{
			return false;
		}

		// 更新搜索位置
		TextIndex = FoundIndex + Part.Len();
	}

	// 最后一个部分必须到结尾匹配（除非以 * 结尾）
	if (!bEndsWithWildcard)
	{
		FString LastPart = PatternParts.Last();
		if (!Text.EndsWith(LastPart, ESearchCase::IgnoreCase))
		{
			return false;
		}
	}

	return true;
}

void FHotUpdateAssetFilter::FilterAssetsForMinimalPackage(
	const TArray<FString>& InAssetPaths,
	const FHotUpdateMinimalPackageConfig& Config,
	IAssetRegistry* AssetRegistry,
	TArray<FString>& OutWhitelistAssets,
	TArray<FString>& OutEngineAssets,
	TArray<FString>& OutExcludedAssets)
{
	// 先调用标准过滤（已支持 /Engine/ 依赖收集）
	TArray<FString> WhitelistAssets;
	TArray<FString> ExcludedAssets;

	FilterAssets(InAssetPaths, Config, AssetRegistry, WhitelistAssets, ExcludedAssets);

	// 从白名单中分离 /Engine/ 路径资产
	for (const FString& Asset : WhitelistAssets)
	{
		if (Asset.StartsWith(TEXT("/Engine/")))
		{
			OutEngineAssets.Add(Asset);
		}
		else
		{
			OutWhitelistAssets.Add(Asset);
		}
	}

	// 排除资产直接输出
	OutExcludedAssets = ExcludedAssets;

	UE_LOG(LogHotUpdateAssetFilter, Log,
		TEXT("小包模式三分类完成: 白名单 %d, 引擎依赖 %d, 排除 %d"),
		OutWhitelistAssets.Num(), OutEngineAssets.Num(), OutExcludedAssets.Num());
}