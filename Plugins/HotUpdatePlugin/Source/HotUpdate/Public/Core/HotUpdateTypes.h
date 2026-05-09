// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HotUpdateTypes.generated.h"

/**
 * 热更新状态
 */
UENUM(BlueprintType)
enum class EHotUpdateState : uint8
{
	Idle            UMETA(DisplayName = "Idle"),
	CheckingVersion UMETA(DisplayName = "Checking Version"),
	UpdateAvailable UMETA(DisplayName = "Update Available"),
	Downloading     UMETA(DisplayName = "Downloading"),
	Paused          UMETA(DisplayName = "Paused"),
	Downloaded      UMETA(DisplayName = "Downloaded"),
	Installing      UMETA(DisplayName = "Installing"),
	Success         UMETA(DisplayName = "Success"),
	Failed          UMETA(DisplayName = "Failed")
};

/**
 * 热更新错误类型
 */
UENUM(BlueprintType)
enum class EHotUpdateError : uint8
{
	None                UMETA(DisplayName = "None"),
	EmptyUrl            UMETA(DisplayName = "Empty URL"),
	InvalidUrl          UMETA(DisplayName = "Invalid URL"),
	NetworkError        UMETA(DisplayName = "Network Error"),
	ParseError          UMETA(DisplayName = "Parse Error"),
	InvalidVersion      UMETA(DisplayName = "Invalid Version"),
	DownloadFailed      UMETA(DisplayName = "Download Failed"),
	VerificationFailed  UMETA(DisplayName = "Verification Failed")
};

/**
 * 包类型（基础包 vs 更新包）
 */
UENUM(BlueprintType)
enum class EHotUpdatePackageKind : uint8
{
	Base            UMETA(DisplayName = "Base"),
	Patch           UMETA(DisplayName = "Patch")
};

/**
 * 容器文件类型
 */
UENUM(BlueprintType)
enum class EHotUpdateContainerType : uint8
{
	Base            UMETA(DisplayName = "Base Container"),
	Patch           UMETA(DisplayName = "Patch Container")
};

/**
 * 版本信息
 */
USTRUCT(BlueprintType)
struct HOTUPDATE_API FHotUpdateVersionInfo
{
	GENERATED_BODY()

	/// 主版本号
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HotUpdate")
	int32 MajorVersion;

	/// 次版本号
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HotUpdate")
	int32 MinorVersion;

	/// 补丁版本号
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HotUpdate")
	int32 PatchVersion;

	/// 构建号
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HotUpdate")
	int32 BuildNumber;

	/// 版本字符串 (例如 "1.2.3.456")
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HotUpdate")
	FString VersionString;

	/// 平台标识
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HotUpdate")
	FString Platform;

	/// 发布时间戳
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HotUpdate")
	int64 Timestamp;

	/// 版本是否有效（解析成功时为 true）
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate")
	bool bIsValid;

	/// 比较版本号
	bool operator>(const FHotUpdateVersionInfo& Other) const
	{
		if (MajorVersion != Other.MajorVersion) return MajorVersion > Other.MajorVersion;
		if (MinorVersion != Other.MinorVersion) return MinorVersion > Other.MinorVersion;
		if (PatchVersion != Other.PatchVersion) return PatchVersion > Other.PatchVersion;
		return BuildNumber > Other.BuildNumber;
	}

	bool operator<(const FHotUpdateVersionInfo& Other) const
	{
		if (MajorVersion != Other.MajorVersion) return MajorVersion < Other.MajorVersion;
		if (MinorVersion != Other.MinorVersion) return MinorVersion < Other.MinorVersion;
		if (PatchVersion != Other.PatchVersion) return PatchVersion < Other.PatchVersion;
		return BuildNumber < Other.BuildNumber;
	}

	bool operator==(const FHotUpdateVersionInfo& Other) const
	{
		return MajorVersion == Other.MajorVersion
			&& MinorVersion == Other.MinorVersion
			&& PatchVersion == Other.PatchVersion
			&& BuildNumber == Other.BuildNumber;
	}

	/// 获取 Hash 值（用于 TSet/TMap 键）
	friend uint32 GetTypeHash(const FHotUpdateVersionInfo& Version)
	{
		return HashCombine(GetTypeHash(Version.MajorVersion),
			HashCombine(GetTypeHash(Version.MinorVersion),
			HashCombine(GetTypeHash(Version.PatchVersion),
			GetTypeHash(Version.BuildNumber))));
	}

	/// 从字符串解析版本
	static FHotUpdateVersionInfo FromString(const FString& InVersionString)
	{
		FHotUpdateVersionInfo Result;
		Result.VersionString = InVersionString;

		TArray<FString> Parts;
		InVersionString.ParseIntoArray(Parts, TEXT("."));

		// 验证各部分为有效数字，非数字输入返回 0
		auto ParseVersionPart = [](const FString& Str) -> int32
		{
			if (Str.IsEmpty()) return 0;
			for (int32 i = 0; i < Str.Len(); i++)
			{
				if (!FChar::IsDigit(Str[i])) return 0;
			}
			return FCString::Atoi(*Str);
		};

		if (Parts.Num() >= 1) Result.MajorVersion = ParseVersionPart(Parts[0]);
		if (Parts.Num() >= 2) Result.MinorVersion = ParseVersionPart(Parts[1]);
		if (Parts.Num() >= 3) Result.PatchVersion = ParseVersionPart(Parts[2]);
		if (Parts.Num() >= 4) Result.BuildNumber = ParseVersionPart(Parts[3]);

		// 至少有一个有效版本部分才标记为有效
		Result.bIsValid = Parts.Num() >= 1 && !Parts[0].IsEmpty();

		return Result;
	}

	/// 转换为字符串
	FString ToString() const
	{
		if (!VersionString.IsEmpty())
		{
			return VersionString;
		}
		return FString::Printf(TEXT("%d.%d.%d.%d"), MajorVersion, MinorVersion, PatchVersion, BuildNumber);
	}

	FHotUpdateVersionInfo()
		: MajorVersion(0)
		, MinorVersion(0)
		, PatchVersion(0)
		, BuildNumber(0)
		, Timestamp(0)
		, bIsValid(false)
	{
	}
};

/**
 * 下载进度信息
 */
USTRUCT(BlueprintType)
struct HOTUPDATE_API FHotUpdateProgress
{
	GENERATED_BODY()

	/// 已下载字节数
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate")
	int64 DownloadedBytes;

	/// 总字节数
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate")
	int64 TotalBytes;

	/// 当前下载速度（字节/秒）
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate")
	float DownloadSpeed;

	/// 剩余时间（秒）
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate")
	float RemainingTime;

	/// 当前文件索引
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate")
	int32 CurrentFileIndex;

	/// 总文件数
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate")
	int32 TotalFiles;

	FHotUpdateProgress()
		: DownloadedBytes(0)
		, TotalBytes(0)
		, DownloadSpeed(0.0f)
		, RemainingTime(0.0f)
		, CurrentFileIndex(0)
		, TotalFiles(0)
	{
	}
};

/**
 * 文件信息（路径、大小、Hash 三元组）
 */
USTRUCT(BlueprintType)
struct HOTUPDATE_API FHotUpdateFileInfo
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "File")
	FString Path;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "File")
	int64 Size = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "File")
	FString Hash;
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

	/// .utoc 文件信息
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Container")
	FHotUpdateFileInfo UtocFile;

	/// .ucas 文件信息
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Container")
	FHotUpdateFileInfo UcasFile;

	/// .pak 文件信息（传统 Pak 格式）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Container")
	FHotUpdateFileInfo PakFile;

	/// 容器类型（基础包/更新包）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Container")
	EHotUpdateContainerType ContainerType;

	/// 容器所属版本
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Container")
	FString Version;

	/// 下载 URL（可选覆盖）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Container")
	FString CustomDownloadUrl;

	FHotUpdateContainerInfo()
		: ContainerType(EHotUpdateContainerType::Base)
		, Version(TEXT(""))
	{
	}

	/// 获取容器总大小（utoc + ucas + pak）
	int64 GetTotalSize() const
	{
		return UtocFile.Size + UcasFile.Size + PakFile.Size;
	}

	/// 比较运算符（基于 ContainerName）
	bool operator==(const FHotUpdateContainerInfo& Other) const
	{
		return ContainerName == Other.ContainerName;
	}

	/// 哈希函数（支持 TSet/TMap）
	friend uint32 GetTypeHash(const FHotUpdateContainerInfo& Container)
	{
		return GetTypeHash(Container.ContainerName);
	}
};

/**
 * 完整 Manifest 数据
 */
USTRUCT(BlueprintType)
struct HOTUPDATE_API FHotUpdateManifest
{
	GENERATED_BODY()

	/// 包类型（基础包/更新包）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HotUpdate")
	EHotUpdatePackageKind PackageKind;

	/// 资源版本信息
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HotUpdate")
	FHotUpdateVersionInfo VersionInfo;

	/// === 更新包字段 ===

	/// 基础版本号（仅更新包有效）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HotUpdate")
	FString BaseVersion;

	/// === 通用字段 ===

	/// 容器文件列表（.utoc/.ucas）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HotUpdate")
	TArray<FHotUpdateContainerInfo> Containers;

	FHotUpdateManifest()
		: PackageKind(EHotUpdatePackageKind::Base)
	{
	}
};

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
	bool bIsMounted;

	/// 版本信息
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate")
	FHotUpdateVersionInfo Version;

	FHotUpdatePakMetadata()
		: PakSize(0)
		, bIsMounted(false)
	{
	}
};

/**
 * 版本检查结果
 */
USTRUCT(BlueprintType)
struct HOTUPDATE_API FHotUpdateVersionCheckResult
{
	GENERATED_BODY()

	/// 是否有更新
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate")
	bool bHasUpdate;

	/// 当前版本
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate")
	FHotUpdateVersionInfo CurrentVersion;

	/// 最新版本
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate")
	FHotUpdateVersionInfo LatestVersion;

	/// 需要下载的容器列表（IoStore 容器文件）
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate")
	TArray<FHotUpdateContainerInfo> UpdateContainers;

	/// 错误信息
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate")
	FString ErrorMessage;

	/// 错误类型（用于 Blueprint 按错误类型分支处理）
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate")
	EHotUpdateError ErrorCode;

	// == 增量下载统计 ==

	/// 跳过的容器数（本地已存在且未变更）
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate")
	int32 SkippedContainerCount;

	/// 跳过的容器总大小（字节）
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate")
	int64 SkippedTotalSize;

	/// 新增容器数
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate")
	int32 AddedContainerCount;

	/// 修改容器数
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate")
	int32 ModifiedContainerCount;

	/// 删除容器数
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate")
	int32 DeletedContainerCount;

	/// 实际需要下载的大小（增量下载大小）
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate")
	int64 IncrementalDownloadSize;

	FHotUpdateVersionCheckResult()
		: bHasUpdate(false)
		  , ErrorCode(), SkippedContainerCount(0)
		  , SkippedTotalSize(0)
		  , AddedContainerCount(0)
		  , ModifiedContainerCount(0)
		  , DeletedContainerCount(0)
		  , IncrementalDownloadSize(0)
	{
	}
};