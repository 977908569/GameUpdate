// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HotUpdateEditorTypes.h"

/**
 * 资源差异比较工具
 * 用于比较两个版本间的资源差异
 */
class HOTUPDATEEDITOR_API FHotUpdateDiffTool
{
public:

	/**
	 * 比较两个Manifest文件的差异
	 */
	FHotUpdateDiffReport CompareManifests(
		const FString& BaseManifestPath,
		const FString& TargetManifestPath);

	/**
	 * 获取资源类型图标名称
	 */
	static FName GetAssetIconName(const FString& AssetPath);

	/**
	 * 格式化文件大小
	 */
	static FString FormatFileSize(int64 Size);

	/**
	 * 在版本目录中查找 filemanifest.json 文件路径
	 * 查找顺序：1) VersionDir/Windows/filemanifest.json 2) VersionDir/filemanifest.json
	 * 如果找不到 filemanifest.json，回退查找 manifest.json
	 * @param VersionDirectory 版本目录路径
	 * @return 找到的 manifest 文件路径，找不到返回空字符串
	 */
	static FString FindFileManifestPath(const FString& VersionDirectory);

	// 差异比较完成委托
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnDiffComplete, const FHotUpdateDiffReport&);
	FOnDiffComplete OnDiffComplete;

private:
	/**
	 * 扫描目录获取所有资源
	 * @param Directory 目录路径
	 * @param bIncludeHiddenFiles 是否包含隐藏文件（传递给 FindFilesRecursive 的 bFindHidden 参数）
	 * @param OutAssets 输出资源映射
	 */
	void ScanDirectory(
		const FString& Directory,
		bool bIncludeHiddenFiles,
		TMap<FString, FHotUpdateAssetDiff>& OutAssets);

	/**
	 * 解析Manifest文件
	 */
	bool ParseManifestFile(
		const FString& ManifestPath,
		TMap<FString, FHotUpdateManifestEntry>& OutEntries);

	/**
	 * 从Pak文件中提取文件名到SHA1 Hash的映射
	 */
	TMap<FString, FString> GetPakFileHashes(const FString& PakPath);

	/**
	 * 获取文件扩展名对应的资源类型
	 */
	FString GetAssetTypeFromExtension(const FString& Extension);
};