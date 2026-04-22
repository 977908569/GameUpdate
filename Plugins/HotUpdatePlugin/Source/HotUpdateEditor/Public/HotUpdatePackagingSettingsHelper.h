// Copyright czm. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "HotUpdateEditorTypes.h"

class UProjectPackagingSettings;

/**
 * 项目打包配置读取结果
 */
struct HOTUPDATEEDITOR_API FHotUpdatePackagingSettingsResult
{
	/// UE 资源路径列表（含依赖）
	TArray<FString> AssetPaths;
	
	/// 错误信息
	TArray<FString> Errors;

	FHotUpdatePackagingSettingsResult() {}
};

/**
 * 项目打包设置辅助类
 * 负责读取和解析 UProjectPackagingSettings 配置
 */
class HOTUPDATEEDITOR_API FHotUpdatePackagingSettingsHelper
{
public:
	/**
	 * 获取项目打包设置实例
	 * @return 项目打包设置对象指针
	 */
	static UProjectPackagingSettings* GetPackagingSettings();

	/**
	 * 解析项目打包配置，获取需要打包的资源列表
	 * @param bIncludeDependencies 是否包含资源依赖
	 * @return 解析结果
	 */
	static FHotUpdatePackagingSettingsResult ParsePackagingSettings(bool bIncludeDependencies = true);

	/**
	 * 从配置中收集要Cook的地图
	 * @param Settings 项目打包设置
	 * @return 地图路径列表
	 */
	static TArray<FString> CollectMapsToCook(UProjectPackagingSettings* Settings);

	/**
	 * 从配置中收集要AlwaysCook的目录下的资源
	 * @param Settings 项目打包设置
	 * @return 资源路径列表
	 */
	static TArray<FString> CollectAlwaysCookAssets(UProjectPackagingSettings* Settings, class IAssetRegistry* AssetRegistry);

	/**
	 * 检查路径是否在NeverCook目录中
	 * @param AssetPath 资源路径
	 * @param Settings 项目打包设置
	 * @return 是否应该排除
	 */
	static bool ShouldExcludeAsset(const FString& AssetPath, UProjectPackagingSettings* Settings);
	
	/**
	 * 过滤编辑器内容
	 * @param AssetPaths 资源路径列表
	 */
	static void FilterEditorContent(TArray<FString>& AssetPaths);

	/**
	 * 将路径转换为标准资源路径
	 * @param Path 路径字符串
	 * @return 标准化的资源路径
	 */
	static FString NormalizeAssetPath(const FString& Path);

private:
	/**
	 * 检查路径是否是编辑器内容
	 * @param AssetPath 资源路径
	 * @return 是否是编辑器内容
	 */
	static bool IsEditorContent(const FString& AssetPath);

	/**
	 * 收集 DirectoriesToAlwaysStageAsUFS 中的非资产文件（打包到 pak 内部）
	 * @param OutPaths 输出的 pak 内路径列表（如 Game/Setting/ui.txt）
	 */
	static void CollectStagedFilesAsUFS(TArray<FString>& OutPaths);

	/**
	 * 从单个目录收集非资产文件
	 * @param DirPath 目录路径配置
	 * @param ContentDir Content 目录路径
	 * @param OutPaths 输出的 pak 内路径列表
	 */
	static void CollectStagedFilesFromDirectory(
		const FDirectoryPath& DirPath,
		const FString& ContentDir,
		TArray<FString>& OutPaths);
};