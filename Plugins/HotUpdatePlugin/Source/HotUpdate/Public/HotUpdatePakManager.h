// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/HotUpdateTypes.h"

/**
 * Pak 管理器（纯 C++ 类）
 *
 * 负责 Pak 文件的挂载、卸载、验证
 */
class HOTUPDATE_API FHotUpdatePakManager
{
public:
	FHotUpdatePakManager() = default;

	/// 初始化
	void Initialize(const FString& InPakDirectory);

	/// 挂载 Pak 文件
	bool MountPak(const FString& PakPath, int32 PakOrder = 0, const FString& EncryptionKey = TEXT(""));

	/// 卸载 Pak 文件
	bool UnmountPak(const FString& PakPath);

	/// 检查 Pak 是否已挂载
	bool IsPakMounted(const FString& PakPath) const;

	/// 解析 Pak 元数据
	static FHotUpdatePakMetadata ParsePakMetadata(const FString& PakPath);

	/// 生成 Pak 挂载顺序
	static int32 CalculatePakOrder(const FHotUpdateVersionInfo& Version);

private:
	/// Pak 存储目录
	FString PakDirectory;

	/// 已挂载的 Pak 列表
	TArray<FHotUpdatePakMetadata> MountedPaks;

	/// Pak 路径到 MountedPaks 索引的映射（加速查找）
	TMap<FString, int32> PakPathToIndex;

	/// 已注册的加密密钥缓存（Key: 密钥十六进制字符串, Value: GUID）
	TMap<FString, FGuid> RegisteredEncryptionKeys;
};
