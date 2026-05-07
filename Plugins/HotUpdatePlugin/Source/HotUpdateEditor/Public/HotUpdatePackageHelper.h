// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HotUpdateEditorTypes.h"

/**
 * 打包工具类（静态，无状态）
 * 提供编译、Cook、路径解析等共享逻辑，供多个 Builder 组合使用
 *
 * 路径格式约定：
 * - Long Package Name:  UE 虚拟路径格式，如 /Game/Maps/Start（不含扩展名）
 * - Pak Mount Path:      Pak 内部路径格式，如 ../../../GameUpdate/Content/Maps/Start.umap（含扩展名）
 * - Absolute Path:       磁盘绝对路径，如 E:/Test/HotPatch/GameUpdate/Content/Maps/Start.umap
 * - Cooked Path:         Cooked 输出路径，如 {ProjectDir}/Saved/Cooked/Windows/GameUpdate/Content/Maps/Start.umap
 */
class HOTUPDATEEDITOR_API FHotUpdatePackageHelper
{
public:
	/** 编译项目 */
	static bool CompileProject(EHotUpdatePlatform Platform);

	/** 全量 Cook */
	static bool CookAssets(EHotUpdatePlatform Platform);

	/** 增量 Cook 指定资源（不含依赖收集） */
	static bool CookAssets(EHotUpdatePlatform Platform, const TArray<FString>& AssetsToCook);

	/**
	 * 收集硬依赖并过滤引擎资产（必须在主线程调用）
	 * 统一入口，供 PatchPackageBuilder 和 CustomPackageBuilder 共用
	 * @param AssetsToCook 要 Cook 的资源列表（Long Package Name 格式）
	 * @return 包含硬依赖的资源列表（已过滤引擎资产）
	 */
	static TArray<FString> CollectDependenciesAndFilterEngine(const TArray<FString>& AssetsToCook);

	// ==================== 路径转换函数 ====================

	/** 资源路径 -> Cooked 文件路径 */
	static FString GetCookedAssetPath(const FString& AssetPath, const FString& CookedPlatformDir);

	/** 资源路径 -> 源文件路径 */
	static FString GetAssetSourcePath(const FString& AssetPath);

	/** 文件路径 -> UE Long Package Name（仅处理 .uasset/.umap） */
	static FString FilePathToLongPackageName(const FString& FilePath);

	/** 文件路径 -> Content 目录虚拟路径（用于非资产文件如 .txt/.json） */
	static FString FilePathToContentMountPath(const FString& FilePath);

	/**
	 * 将虚拟包路径映射为 Pak 内部挂载路径
	 * /Game/... -> ../../../{ProjectName}/Content/...
	 * /Engine/... -> ../../../Engine/Content/...
	 * 插件路径 -> 根据 FPackageName 解析结果映射
	 * @param AssetPath 虚拟包路径（不含扩展名，以 / 开头）
	 * @return Pak 内部 Dest 路径（不含扩展名）
	 */
	static FString GetAssetPakMountPath(const FString& AssetPath);

	/**
	 * 规范化资源路径为 Long Package Name 格式
	 * - 去除前后空格
	 * - 虚拟路径（/开头）直接返回
	 * - 绝对磁盘路径（含盘符）通过 FilePathToLongPackageName 转换
	 * - 相对路径自动添加 /Game/ 前缀
	 * @param Path 输入路径（可能是相对路径如 "Maps/Start"、虚拟路径如 "/Game/Maps/Start" 或磁盘路径）
	 * @return 规范化的虚拟路径（如 "/Game/Maps/Start"），无法转换时返回原路径
	 */
	static FString NormalizeAssetPath(const FString& Path);

	/**
	 * 将虚拟路径转换为磁盘绝对路径（用于非资产文件）
	 * 支持的输入格式：
	 *   - /Game/... -> 项目 Content 目录
	 *   - ../../../{ProjectName}/Content/... -> 项目 Content 目录
	 *   - 相对路径 -> 相对于项目目录
	 *   - 绝对路径 -> 直接返回
	 * @param VirtualPath 虚拟路径（如 "/Game/Setting/txt_pak.txt"）
	 * @return 磁盘绝对路径，无法识别时返回空字符串
	 */
	static FString VirtualPathToDiskPath(const FString& VirtualPath);

	// ==================== 资产类型判断函数 ====================

	/**
	 * 判断文件扩展名是否是 UE 资产格式
	 * @param Extension 文件扩展名（不含点，如 "uasset"、"umap"）
	 * @return 是否是 UE 资产扩展名
	 */
	static bool IsUAssetExtension(const FString& Extension);

	/**
	 * 判断文件路径是否是 UE 资产文件（根据扩展名）
	 * @param FilePath 文件路径（可以是虚拟路径或磁盘路径）
	 * @return 是否是 UE 资产文件
	 */
	static bool IsUAssetFile(const FString& FilePath);

	// ==================== 辅助判断函数 ====================

	/** 是否是外部资产（ExternalActors/ExternalObjects/Script/Memory 包） */
	static bool IsExternalAsset(const FString& AssetPath);

	/** 判断路径是否是有效的 UE Package（可以被 Cook） */
	static bool IsValidPackagePath(const FString& AssetPath);

	// ==================== 平台目录函数 ====================

	/**
	 * 获取 Cooked 平台目录路径（Saved/Cooked/{PlatformName}）
	 * @param Platform 平台枚举值
	 * @return Cooked 平台目录的完整路径
	 */
	static FString GetCookedPlatformDir(EHotUpdatePlatform Platform);

	/**
	 * 获取 Cooked 平台目录路径（考虑 Android 纹理格式后缀，如 Android_ASTC）
	 * @param Platform 平台枚举值
	 * @param AndroidTextureFormat Android 纹理格式（仅 Android 平台有效）
	 * @return Cooked 平台目录的完整路径
	 */
	static FString GetCookedPlatformDir(EHotUpdatePlatform Platform, EHotUpdateAndroidTextureFormat AndroidTextureFormat);

private:
	/** 获取平台目录名（含 Android 纹理格式后缀） */
	static FString GetPlatformDirName(EHotUpdatePlatform Platform, EHotUpdateAndroidTextureFormat TextureFormat);

};