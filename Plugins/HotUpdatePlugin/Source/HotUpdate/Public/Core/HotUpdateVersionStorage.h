// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/HotUpdateTypes.h"

/**
 * 版本存储管理器（纯 C++ 类）
 * 负责本地版本信息和 Manifest 的持久化存储
 */
class HOTUPDATE_API FHotUpdateVersionStorage
{
public:
	FHotUpdateVersionStorage() = default;

	/// 初始化存储管理器
	void Initialize(const FString& InStoragePath);

	// == 版本信息管理 ==

	/// 加载本地版本信息
	bool LoadLocalVersion(FHotUpdateVersionInfo& OutVersion);

	/// 保存本地版本信息
	bool SaveLocalVersion(const FHotUpdateVersionInfo& Version);

	// == Manifest 管理 ==

	/// 加载本地 Manifest 缓存
	bool LoadLocalManifest(FHotUpdateManifest& OutManifest);

	/// 保存 Manifest 到本地缓存
	bool SaveLocalManifest(const FHotUpdateManifest& Manifest);

	// == 路径获取 ==

	/// 获取版本文件路径
	FString GetVersionFilePath() const { return StoragePath / TEXT("version.json"); }

	/// 获取 Manifest 文件路径
	FString GetManifestFilePath() const { return StoragePath / TEXT("manifest.json"); }

private:
	/// 存储根目录
	FString StoragePath;
};
