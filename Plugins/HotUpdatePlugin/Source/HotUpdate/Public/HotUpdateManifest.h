// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/HotUpdateTypes.h"
#include "HotUpdateManifest.generated.h"

/**
 * 完整 Manifest 数据
 */
USTRUCT(BlueprintType)
struct HOTUPDATE_API FHotUpdateManifest
{
	GENERATED_BODY()

	/// Manifest 版本（2 = 支持基础包/更新包）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HotUpdate")
	int32 ManifestVersion;

	/// 包类型（基础包/更新包）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HotUpdate")
	EHotUpdatePackageKind PackageKind;

	/// 资源版本信息
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HotUpdate")
	FHotUpdateVersionInfo VersionInfo;

	/// === 基础包字段 ===

	/// Chunk 定义列表（包含依赖关系）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HotUpdate")
	TArray<FHotUpdateChunkInfo> Chunks;

	/// === 更新包字段 ===

	/// 基础版本号（仅更新包有效）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HotUpdate")
	FString BaseVersion;

	/// === 通用字段 ===

	/// 容器文件列表（.utoc/.ucas）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HotUpdate")
	TArray<FHotUpdateContainerInfo> Containers;

	/// 所有文件条目
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HotUpdate")
	TArray<FHotUpdateManifestEntry> Files;

	FHotUpdateManifest()
		: ManifestVersion(2)
		, PackageKind(EHotUpdatePackageKind::Base)
	{
	}
};

/**
 * Manifest 解析器
 */
UCLASS(BlueprintType)
class HOTUPDATE_API UHotUpdateManifestParser : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Manifest")
	static bool ParseFromJson(const FString& JsonString, FHotUpdateManifest& OutManifest);

	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Manifest")
	static bool SaveToFile(const FString& FilePath, const FHotUpdateManifest& Manifest);

	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Manifest")
	static FString ToJsonString(const FHotUpdateManifest& Manifest);
};