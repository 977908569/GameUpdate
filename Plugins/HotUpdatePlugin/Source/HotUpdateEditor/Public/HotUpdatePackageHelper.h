// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HotUpdateEditorTypes.h"

/**
 * 打包工具类（静态，无状态）
 * 提供编译、Cook、路径解析等共享逻辑，供多个 Builder 组合使用
 */
class HOTUPDATEEDITOR_API FHotUpdatePackageHelper
{
public:
	/** 编译项目 */
	static bool CompileProject(EHotUpdatePlatform Platform);

	/** 全量 Cook */
	static bool CookAssets(EHotUpdatePlatform Platform);

	/** 增量 Cook 指定资源 */
	static bool CookAssets(EHotUpdatePlatform Platform, const TArray<FString>& AssetsToCook);

	/** 资源路径 -> Cooked 磁盘路径 */
	static FString GetAssetDiskPath(const FString& AssetPath, const FString& CookedPlatformDir);

	/** 资源路径 -> 源文件路径 */
	static FString GetAssetSourcePath(const FString& AssetPath);

	/** 资源路径 -> 文件名（manifest 格式） */
	static FString ConvertAssetPathToFileName(const FString& AssetPath, const FString& CookedPlatformDir);

	/** 文件名 -> 资源路径（UE Long Package Name 格式） */
	static FString FileNameToAssetPath(const FString& FileName);
};