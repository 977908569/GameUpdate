// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/HotUpdateTypes.h"
#include "Core/HotUpdatePakTypes.h"
#include "HotUpdatePakManager.generated.h"

/**
 * Pak 管理器
 *
 * 负责 Pak 文件的挂载、卸载、验证
 */
UCLASS(BlueprintType)
class HOTUPDATE_API UHotUpdatePakManager : public UObject
{
	GENERATED_BODY()

public:
	UHotUpdatePakManager();

	/// 初始化
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Pak")
	void Initialize(const FString& InPakDirectory);

	/// 挂载 Pak 文件
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Pak")
	bool MountPak(const FString& PakPath, int32 PakOrder = 0, const FString& EncryptionKey = TEXT(""));

	/// 卸载 Pak 文件
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Pak")
	bool UnmountPak(const FString& PakPath);

	/// 获取已挂载的 Pak 列表
	UFUNCTION(BlueprintPure, Category = "HotUpdate|Pak")
	TArray<FHotUpdatePakMetadata> GetMountedPaks() const { return MountedPaks; }

	/// 检查 Pak 是否已挂载
	UFUNCTION(BlueprintPure, Category = "HotUpdate|Pak")
	bool IsPakMounted(const FString& PakPath) const;

	/// 获取 Pak 文件条目详细信息
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Pak")
	TArray<FHotUpdatePakEntry> GetPakEntries(const FString& PakPath);

	// == 加密密钥管理 ==

	/// 注册加密密钥
	/// @param KeyGuid 密钥 GUID
	/// @param EncryptionKey 加密密钥（十六进制字符串）
	/// @param KeyName 密钥名称（可选）
	/// @return 是否成功
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Encryption")
	bool RegisterEncryptionKey(const FGuid& KeyGuid, const FString& EncryptionKey, const FString& KeyName = TEXT(""));

	/// 获取已注册的加密密钥
	UFUNCTION(BlueprintPure, Category = "HotUpdate|Encryption")
	bool GetRegisteredEncryptionKey(const FGuid& KeyGuid, FHotUpdateEncryptionKey& OutKey) const;

	// == 事件委托 ==

	/// Pak 挂载完成事件
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPakMounted, const FString&, PakPath, bool, bSuccess);
	UPROPERTY(BlueprintAssignable, Category = "HotUpdate|Events")
	FOnPakMounted OnPakMounted;

	/// Pak 卸载完成事件
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPakUnmounted, const FString&, PakPath);
	UPROPERTY(BlueprintAssignable, Category = "HotUpdate|Events")
	FOnPakUnmounted OnPakUnmounted;

	/// 解析 Pak 元数据
	FHotUpdatePakMetadata ParsePakMetadata(const FString& PakPath);

	/// 生成 Pak 挂载顺序
	int32 CalculatePakOrder(const FString& PakName, const FHotUpdateVersionInfo& Version);

private:
	/// Pak 存储目录
	UPROPERTY(Transient)
	FString PakDirectory;

	/// 已挂载的 Pak 列表
	UPROPERTY(Transient)
	TArray<FHotUpdatePakMetadata> MountedPaks;

	/// 已注册的加密密钥映射
	UPROPERTY(Transient)
	TMap<FString, FHotUpdateEncryptionKey> RegisteredEncryptionKeys;

	/// 加密密钥访问临界区
	mutable FCriticalSection EncryptionKeyCriticalSection;

	/// 向引擎注册加密密钥
	bool RegisterEncryptionKeyWithEngine(const FGuid& KeyGuid, const FString& EncryptionKey);
};