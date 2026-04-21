// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HotUpdateEditorTypes.h"

class IAssetRegistry;

/**
 * 资产过滤器
 * 负责根据白名单规则过滤资产，支持最小包功能
 */
class HOTUPDATEEDITOR_API FHotUpdateAssetFilter
{
public:
	/**
	 * 过滤资产列表
	 * @param InAssetPaths 输入资产路径列表
	 * @param Config 最小包配置
	 * @param AssetRegistry 资产注册表
	 * @param OutWhitelistAssets 白名单资产输出
	 * @param OutExcludedAssets 被排除的资产输出
	 */
	static void FilterAssets(
		const TArray<FString>& InAssetPaths,
		const FHotUpdateMinimalPackageConfig& Config,
		IAssetRegistry* AssetRegistry,
		TArray<FString>& OutWhitelistAssets,
		TArray<FString>& OutExcludedAssets);
	

	/**
	 * 检查资产是否匹配过滤规则
	 * @param AssetPath 资产路径
	 * @param Rule 过滤规则
	 * @param AssetRegistry 资产注册表（用于获取资产类型）
	 * @return 是否匹配
	 */
	static bool MatchesFilterRule(
		const FString& AssetPath,
		const FHotUpdateAssetFilterRule& Rule,
		IAssetRegistry* AssetRegistry);

	/**
	 * 检查资产是否在目录列表中
	 * @param AssetPath 资产路径
	 * @param Directories 目录列表
	 * @param bRecursive 是否递归匹配子目录
	 * @return 是否在目录中
	 */
	static bool IsInDirectories(
		const FString& AssetPath,
		const TArray<FDirectoryPath>& Directories,
		bool bRecursive = true);

	/**
	 * 收集目录下所有资产
	 * @param Directories 目录列表
	 * @param AssetRegistry 资产注册表
	 * @return 资产路径列表
	 */
	static TArray<FString> CollectAssetsFromDirectories(
		const TArray<FDirectoryPath>& Directories,
		const IAssetRegistry* AssetRegistry);

	/**
	 * 获取资产依赖
	 * @param AssetPath 资源路径
	 * @param AssetRegistry 资产注册表
	 * @param Strategy 依赖策略
	 * @param MaxDepth 最大深度（0 = 无限制）
	 * @param OutDependencies 输出依赖列表
	 */
	static void GetDependencies(
		const FString& AssetPath,
		IAssetRegistry* AssetRegistry,
		EHotUpdateDependencyStrategy Strategy,
		int32 MaxDepth,
		TSet<FString>& OutDependencies);

	/**
	 * 获取资产类型名称
	 * @param AssetPath 资产路径
	 * @param AssetRegistry 资产注册表
	 * @return 资产类型名称（如 "Texture2D", "Material"）
	 */
	static FString GetAssetTypeName(
		const FString& AssetPath,
		IAssetRegistry* AssetRegistry);

private:
	/**
	 * 内部递归获取依赖
	 */
	static void GetDependenciesRecursive(
		const FString& AssetPath,
		IAssetRegistry* AssetRegistry,
		EHotUpdateDependencyStrategy Strategy,
		int32 CurrentDepth,
		int32 MaxDepth,
		TSet<FString>& OutDependencies,
		TSet<FString>& Visited);

	/**
	 * 通配符匹配
	 * @param Pattern 模式（包含 * 通配符）
	 * @param Text 要匹配的文本
	 * @return 是否匹配
	 */
	static bool WildcardMatch(const FString& Pattern, const FString& Text);
};