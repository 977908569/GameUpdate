// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HotUpdateEditorTypes.h"
#include "HAL/CriticalSection.h"

/**
 * 版本链配置
 */
struct HOTUPDATEEDITOR_API FHotUpdateVersionChain
{
	/// 基础版本号
	FString BaseVersion;

	/// Patch 版本链
	TArray<FString> PatchChain;

	/// 当前最新版本
	FString CurrentVersion;

	/// 平台
	EHotUpdatePlatform Platform;

	FHotUpdateVersionChain()
		: Platform(EHotUpdatePlatform::Windows)
	{
	}
};

/**
 * 版本管理器
 * 管理基础包和更新包的版本链
 */
class HOTUPDATEEDITOR_API FHotUpdateVersionManager
{
public:
	/**
	 * 注册版本
	 */
	bool RegisterVersion(const FHotUpdateEditorVersionInfo& VersionInfo);

	/**
	 * 注销版本
	 */
	bool UnregisterVersion(const FString& VersionString, EHotUpdatePlatform Platform);

	/**
	 * 获取版本历史
	 */
	TArray<FHotUpdateEditorVersionInfo> GetVersionHistory(EHotUpdatePlatform Platform);

	/**
	 * 获取版本信息
	 */
	FHotUpdateEditorVersionInfo GetVersionInfo(const FString& VersionString, EHotUpdatePlatform Platform);

	/**
	 * 获取版本链
	 */
	FHotUpdateVersionChain GetVersionChain(const FString& BaseVersion, EHotUpdatePlatform Platform);

	/**
	 * 获取最新版本
	 */
	FString GetLatestVersion(EHotUpdatePlatform Platform);

	/**
	 * 获取基础版本列表
	 */
	TArray<FString> GetBaseVersions(EHotUpdatePlatform Platform);

	/**
	 * 获取所有可选择版本（用于版本选择器）
	 * 包括基础版本和热更版本
	 */
	TArray<FHotUpdateVersionSelectItem> GetSelectableVersions(EHotUpdatePlatform Platform);

	/**
	 * 获取版本存储根目录
	 */
	static FString GetVersionRootDir();

	/**
	 * 获取版本目录
	 */
	static FString GetVersionDir(const FString& VersionString, EHotUpdatePlatform Platform);

	/**
	 * 获取版本目录（考虑 Android 纹理格式后缀，如 Android_ASTC）
	 */
	static FString GetVersionDir(const FString& VersionString, EHotUpdatePlatform Platform, EHotUpdateAndroidTextureFormat AndroidTextureFormat);

	/**
	 * 检查版本是否存在
	 */
	bool VersionExists(const FString& VersionString, EHotUpdatePlatform Platform);

private:
	/**
	 * 加载版本注册表
	 */
	bool LoadVersionRegistry();

	/**
	 * 保存版本注册表
	 */
	bool SaveVersionRegistry();

	/**
	 * 解析版本号
	 */
	static bool ParseVersionString(const FString& VersionString, int32& OutMajor, int32& OutMinor, int32& OutPatch, int32& OutBuild);

	/**
	 * 比较版本号
	 * @return -1: A < B, 0: A == B, 1: A > B
	 */
	static int32 CompareVersions(const FString& A, const FString& B);

private:
	/// 版本注册表（版本号 -> 平台 -> 版本信息）
	TMap<FString, TMap<EHotUpdatePlatform, FHotUpdateEditorVersionInfo>> VersionRegistry;

	/// 注册表是否已加载
	bool bRegistryLoaded = false;

	/// 注册表访问锁
	mutable FCriticalSection RegistryLock;
};