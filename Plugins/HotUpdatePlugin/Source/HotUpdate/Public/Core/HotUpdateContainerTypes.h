// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HotUpdateContainerTypes.generated.h"

/**
 * 容器文件类型
 */
UENUM(BlueprintType)
enum class EHotUpdateContainerType : uint8
{
	Base            UMETA(DisplayName = "基础包容器"),
	Patch           UMETA(DisplayName = "更新包容器")
};

/**
 * IoStore 容器文件信息
 */
USTRUCT(BlueprintType)
struct HOTUPDATE_API FHotUpdateContainerInfo
{
	GENERATED_BODY()

	/// 容器名称（不含扩展名）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Container")
	FString ContainerName;

	/// .utoc 文件相对路径
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Container")
	FString UtocPath;

	/// .utoc 文件大小（字节）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Container")
	int64 UtocSize;

	/// .utoc 文件 SHA1 Hash
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Container")
	FString UtocHash;

	/// .ucas 文件相对路径
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Container")
	FString UcasPath;

	/// .ucas 文件大小（字节）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Container")
	int64 UcasSize;

	/// .ucas 文件 SHA1 Hash
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Container")
	FString UcasHash;

	/// 容器类型（基础包/更新包）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Container")
	EHotUpdateContainerType ContainerType;

	/// 容器所属 Chunk ID
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Container")
	int32 ChunkId;

	/// 容器所属版本
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Container")
	FString Version;

	/// 下载 URL（可选覆盖）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Container")
	FString CustomDownloadUrl;

	FHotUpdateContainerInfo()
		: UtocSize(0)
		, UcasSize(0)
		, ContainerType(EHotUpdateContainerType::Base)
		, ChunkId(0)
		, Version(TEXT(""))
	{
	}

	/// 比较运算符（基于 ChunkId，与增量计算器一致）
	bool operator==(const FHotUpdateContainerInfo& Other) const
	{
		return ChunkId == Other.ChunkId;
	}

	/// 哈希函数（支持 TSet/TMap）
	friend uint32 GetTypeHash(const FHotUpdateContainerInfo& Container)
	{
		return GetTypeHash(Container.ChunkId);
	}
};