// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/HotUpdateTypes.h"
#include "HotUpdatePakTypes.generated.h"

/**
 * Pak 文件元数据
 */
USTRUCT(BlueprintType)
struct HOTUPDATE_API FHotUpdatePakMetadata
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate")
	FString PakPath;

	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate")
	FString PakName;

	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate")
	int64 PakSize;

	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate")
	FString PakHash;

	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate")
	int32 ChunkId;

	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate")
	bool bIsMounted;

	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate")
	FString EncryptionKeyGuid;

	/// 版本信息
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate")
	FHotUpdateVersionInfo Version;

	FHotUpdatePakMetadata()
		: PakSize(0)
		, ChunkId(-1)
		, bIsMounted(false)
	{
	}
};

/**
 * Pak 文件条目信息
 */
USTRUCT(BlueprintType)
struct HOTUPDATE_API FHotUpdatePakEntry
{
	GENERATED_BODY()

	/// 文件在 Pak 中的路径
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate|Pak")
	FString FileName;

	/// 文件大小（原始大小）
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate|Pak")
	int64 UncompressedSize;

	/// 压缩后大小
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate|Pak")
	int64 CompressedSize;

	/// 文件偏移量
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate|Pak")
	int64 Offset;

	/// 是否压缩
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate|Pak")
	bool bIsCompressed;

	/// 是否加密
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate|Pak")
	bool bIsEncrypted;

	/// SHA1 Hash
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate|Pak")
	FString FileHash;

	FHotUpdatePakEntry()
		: UncompressedSize(0)
		, CompressedSize(0)
		, Offset(0)
		, bIsCompressed(false)
		, bIsEncrypted(false)
	{
	}
};

/**
 * 加密密钥信息
 */
USTRUCT(BlueprintType)
struct HOTUPDATE_API FHotUpdateEncryptionKey
{
	GENERATED_BODY()

	/// 密钥 GUID
	UPROPERTY(BlueprintReadOnly, Category = "Encryption")
	FGuid KeyGuid;

	/// 加密密钥（十六进制字符串）
	UPROPERTY(BlueprintReadOnly, Category = "Encryption")
	FString Key;

	/// 密钥名称（可选）
	UPROPERTY(BlueprintReadOnly, Category = "Encryption")
	FString KeyName;

	/// 密钥是否有效
	bool IsValid() const { return KeyGuid.IsValid() && !Key.IsEmpty(); }

	FHotUpdateEncryptionKey()
	{
	}

	FHotUpdateEncryptionKey(const FGuid& InGuid, const FString& InKey, const FString& InName = TEXT(""))
		: KeyGuid(InGuid)
		, Key(InKey)
		, KeyName(InName)
	{
	}
};

