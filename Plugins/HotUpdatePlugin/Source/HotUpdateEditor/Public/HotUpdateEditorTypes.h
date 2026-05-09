// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/HotUpdateTypes.h"
#include "HotUpdateEditorTypes.generated.h"

/**
 * 文件变更类型
 */
UENUM(BlueprintType)
enum class EHotUpdateFileChangeType : uint8
{
	Added           UMETA(DisplayName = "Added"),
	Modified        UMETA(DisplayName = "Modified"),
	Deleted         UMETA(DisplayName = "Deleted"),
	Unchanged       UMETA(DisplayName = "Unchanged")
};

class UHotUpdatePatchPackageBuilder;


/**
 * 打包模式（基础包 vs 热更包）
 */
UENUM(BlueprintType)
enum class EHotUpdatePackagingMode : uint8
{
	BasePackage     UMETA(DisplayName = "打基础包"),
	HotfixPackage   UMETA(DisplayName = "打热更包")
};

/**
 * 目标平台
 */
UENUM(BlueprintType)
enum class EHotUpdatePlatform : uint8
{
	Windows         UMETA(DisplayName = "Windows (PC)"),
	Android         UMETA(DisplayName = "Android"),
	IOS             UMETA(DisplayName = "iOS")
};

/**
 * 构建配置
 */
UENUM(BlueprintType)
enum class EHotUpdateBuildConfiguration : uint8
{
	DebugGame       UMETA(DisplayName = "DebugGame (包含调试信息)"),
	Development     UMETA(DisplayName = "Development"),
	Shipping        UMETA(DisplayName = "Shipping (发布构建)")
};

/**
 * 分包策略
 */
UENUM(BlueprintType)
enum class EHotUpdateChunkStrategy : uint8
{
	None            UMETA(DisplayName = "不分包（所有资源打包成一个Chunk）"),
	Size            UMETA(DisplayName = "按大小分包")
};

/**
 * 依赖处理策略
 */
UENUM(BlueprintType)
enum class EHotUpdateDependencyStrategy : uint8
{
	IncludeAll      UMETA(DisplayName = "包含所有依赖"),
	HardOnly        UMETA(DisplayName = "仅硬依赖"),
	SoftOnly        UMETA(DisplayName = "仅软依赖"),
	None            UMETA(DisplayName = "不包含依赖")
};

/**
 * 基础版本构建进度
 */
USTRUCT(BlueprintType)
struct HOTUPDATEEDITOR_API FHotUpdateBaseVersionBuildProgress
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FString CurrentStage;

	UPROPERTY(BlueprintReadOnly)
	float ProgressPercent;

	UPROPERTY(BlueprintReadOnly)
	FString StatusMessage;

	FHotUpdateBaseVersionBuildProgress()
		: ProgressPercent(0.0f)
	{
	}
};

/**
 * 基础版本构建结果
 */
USTRUCT(BlueprintType)
struct HOTUPDATEEDITOR_API FHotUpdateBaseVersionBuildResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	bool bSuccess;

	UPROPERTY(BlueprintReadOnly)
	FString VersionString;

	UPROPERTY(BlueprintReadOnly)
	EHotUpdatePlatform Platform;

	/// 可执行文件路径 (exe/apk)
	UPROPERTY(BlueprintReadOnly)
	FString ExecutablePath;

	/// 输出目录
	UPROPERTY(BlueprintReadOnly)
	FString OutputDirectory;

	/// 资源Hash清单路径
	UPROPERTY(BlueprintReadOnly)
	FString ResourceHashPath;

	/// 错误信息
	UPROPERTY(BlueprintReadOnly)
	FString ErrorMessage;

	FHotUpdateBaseVersionBuildResult()
		: bSuccess(false), Platform(EHotUpdatePlatform::Windows)
	{
	}
};

/**
 * 版本选择项（用于热更包版本选择器）
 */
USTRUCT(BlueprintType)
struct HOTUPDATEEDITOR_API FHotUpdateVersionSelectItem
{
	GENERATED_BODY()

	/// 版本号
	UPROPERTY(BlueprintReadOnly, Category = "Version")
	FString VersionString;

	/// 版本类型（基础包/热更包）
	UPROPERTY(BlueprintReadOnly, Category = "Version")
	EHotUpdatePackageKind PackageKind;

	/// 如果是热更包，基于哪个基础版本
	UPROPERTY(BlueprintReadOnly, Category = "Version")
	FString BaseVersion;

	/// 显示名称（如 "1.0 (基础包)" 或 "1.1 (热更包，基于 1.0)"）
	UPROPERTY(BlueprintReadOnly, Category = "Version")
	FString DisplayName;

	/// FileManifest 路径（filemanifest.json，用于打包差异计算）
	UPROPERTY(BlueprintReadOnly, Category = "Version")
	FString FileManifestPath;

	/// 创建时间
	UPROPERTY(BlueprintReadOnly, Category = "Version")
	FDateTime CreatedTime;

	FHotUpdateVersionSelectItem()
		: PackageKind(EHotUpdatePackageKind::Base)
	{
	}
};

/**
 * 按大小分包的详细配置
 */
USTRUCT(BlueprintType)
struct HOTUPDATEEDITOR_API FHotUpdateSizeBasedChunkConfig
{
	GENERATED_BODY()

	/// 最大 Chunk 大小（MB）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Size", meta = (ClampMin = "1"))
	int32 MaxChunkSizeMB;

	/// Chunk 名称前缀（如 "Chunk"，最终为 "Chunk_0", "Chunk_1"）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Size")
	FString ChunkNamePrefix;

	/// Chunk ID 起始值
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Size")
	int32 ChunkIdStart;

	/// 是否按大小排序（大的优先打包）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Size")
	bool bSortBySize;


	FHotUpdateSizeBasedChunkConfig()
		: MaxChunkSizeMB(256)
		, ChunkNamePrefix(TEXT("Chunk"))
		, ChunkIdStart(0)
		, bSortBySize(true)
		
	{
	}
};


/**
 * Chunk 分析配置
 */
USTRUCT(BlueprintType)
struct HOTUPDATEEDITOR_API FHotUpdateChunkAnalysisConfig
{
	GENERATED_BODY()

	/// 分包策略
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk")
	EHotUpdateChunkStrategy ChunkStrategy;

	/// 按大小分包的详细配置
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk|Size")
	FHotUpdateSizeBasedChunkConfig SizeBasedConfig;
	
	/// 未匹配任何规则的资源的默认 Chunk 名称
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk")
	FString DefaultChunkName;

	/// 未匹配任何规则的资源的默认 Chunk ID（-1 表示自动分配）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk")
	int32 DefaultChunkId;

	FHotUpdateChunkAnalysisConfig()
		: ChunkStrategy(EHotUpdateChunkStrategy::Size)
		, DefaultChunkName(TEXT("Default"))
		, DefaultChunkId(-1)
	{
	}
};
/**
 * 资产过滤器规则
 * 用于定义白名单的过滤条件
 */
USTRUCT(BlueprintType)
struct HOTUPDATEEDITOR_API FHotUpdateAssetFilterRule
{
	GENERATED_BODY()

	/// 资源路径（支持通配符，如 "/Game/UI/*"）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter")
	FString AssetPath;

	/// 是否递归匹配子目录
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter")
	bool bRecursive;
	

	FHotUpdateAssetFilterRule()
		: bRecursive(true)
	{
	}

	/// 验证规则是否有效
	bool IsValid() const { return !AssetPath.IsEmpty(); }
};

/**
 * 最小包配置
 * 用于控制基础包打包时只包含指定的资源
 */
USTRUCT(BlueprintType)
struct HOTUPDATEEDITOR_API FHotUpdateMinimalPackageConfig
{
	GENERATED_BODY()

	/// 是否启用最小包模式
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MinimalPackage")
	bool bEnableMinimalPackage;

	/// 必须包含的目录列表
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MinimalPackage|Whitelist")
	TArray<FDirectoryPath> WhitelistDirectories;

	/// 依赖处理策略
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MinimalPackage|Dependencies")
	EHotUpdateDependencyStrategy DependencyStrategy;

	/// 非首包资源的分包策略（None=全部一个Chunk，其他=按策略分包）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MinimalPackage|ChunkSplitting")
	EHotUpdateChunkStrategy PatchChunkStrategy;

	/// 分包策略配置
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MinimalPackage|ChunkSplitting")
	FHotUpdateChunkAnalysisConfig PatchChunkConfig;

	FHotUpdateMinimalPackageConfig()
		: bEnableMinimalPackage(false)
		, DependencyStrategy(EHotUpdateDependencyStrategy::HardOnly)
		, PatchChunkStrategy(EHotUpdateChunkStrategy::None)
	{
	}
};


/**
 * IoStore 容器配置
 */
USTRUCT(BlueprintType)
struct HOTUPDATEEDITOR_API FHotUpdateIoStoreConfig
{
	GENERATED_BODY()

	/// 容器名称（如 "Chunk_0", "Patch_100"）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IoStore")
	FString ContainerName;

	/// 压缩格式（"Oodle", "Zlib", "GZip", "None"）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IoStore")
	FString CompressionFormat;

	/// 压缩级别（0-9，仅对 Oodle 有效）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IoStore", meta = (ClampMin = "0", ClampMax = "9"))
	int32 CompressionLevel;

	/// 加密密钥（空表示不加密）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IoStore")
	FString EncryptionKey;

	/// 是否加密索引
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IoStore")
	bool bEncryptIndex;

	/// 是否加密内容
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IoStore")
	bool bEncryptContent;

	/// 是否使用 IoStore 格式（UE5 标准：生成 .utoc/.ucas）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IoStore")
	bool bUseIoStore;

	FHotUpdateIoStoreConfig()
		: CompressionFormat(TEXT("Oodle"))
		, CompressionLevel(4)
		, bEncryptIndex(false)
		, bEncryptContent(false)
		, bUseIoStore(true)
	{
	}
};

/**
 * IoStore 输出结果
 */
USTRUCT(BlueprintType)
struct HOTUPDATEEDITOR_API FHotUpdateIoStoreResult
{
	GENERATED_BODY()

	/// 是否成功
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	bool bSuccess;

	/// .utoc 文件路径（IoStore 格式）或 .pak 文件路径（传统格式）
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	FString UtocPath;

	/// .ucas 文件路径（仅 IoStore 格式有效）
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	FString UcasPath;

	/// 容器总大小（utoc + ucas + pak）
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	int64 ContainerSize;

	/// 包含文件数
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	int32 FileCount;

	/// 错误信息
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	FString ErrorMessage;

	FHotUpdateIoStoreResult()
		: bSuccess(false)
		, ContainerSize(0)
		, FileCount(0)
	{
	}
};

/**
 * Chunk 定义
 */
USTRUCT(BlueprintType)
struct HOTUPDATEEDITOR_API FHotUpdateChunkDefinition
{
	GENERATED_BODY()

	/// Chunk ID
	UPROPERTY(BlueprintReadOnly, Category = "Chunk")
	int32 ChunkId;

	/// Chunk 名称
	UPROPERTY(BlueprintReadOnly, Category = "Chunk")
	FString ChunkName;

	/// 包含的资源路径
	UPROPERTY(BlueprintReadOnly, Category = "Chunk")
	TArray<FString> AssetPaths;

	/// 未压缩大小
	UPROPERTY(BlueprintReadOnly, Category = "Chunk")
	int64 UncompressedSize;

	/// 压缩后大小
	UPROPERTY(BlueprintReadOnly, Category = "Chunk")
	int64 CompressedSize;

	FHotUpdateChunkDefinition()
		: ChunkId(-1)
		, UncompressedSize(0)
		, CompressedSize(0)
	{
	}
};


/**
 * 更新包构建配置
 */
USTRUCT(BlueprintType)
struct HOTUPDATEEDITOR_API FHotUpdatePatchPackageConfig
{
	GENERATED_BODY()

	/// 更新包版本号（格式: Major.Minor.Patch.Build）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Version")
	FString PatchVersion;

	/// 基础版本号
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Version")
	FString BaseVersion;

	/// 目标平台
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Packaging")
	EHotUpdatePlatform Platform;

	/// 基础版本 FileManifest 路径（filemanifest.json）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Packaging")
	FFilePath BaseFileManifestPath;


	/// 是否包含依赖资源
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Packaging")
	bool bIncludeDependencies;

	/// 是否跳过 Cook 步骤（默认 false = 先 Cook 再打包）
	/// 不 Cook 的话，Patch 会使用旧的 cooked 文件，导致修改不生效
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Packaging")
	bool bSkipCook;

	/// 是否启用增量 Cook 模式
	/// 启用后只 Cook 有变更的资源（基于 Diff 结果），而非全量 Cook
	/// 使用 -PACKAGE 只 Cook 指定资源，大幅减少 Cook 时间
	/// 需要先有上次 Cook 的输出文件作为 Diff 基准
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Packaging")
	bool bIncrementalCook;

	/// 是否跳过编译步骤（默认 false = 编译后再 Cook）
	/// 编译确保 Cook 使用最新的游戏代码，避免使用旧代码逻辑
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Packaging")
	bool bSkipBuild;

	/// 输出目录
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Packaging")
	FDirectoryPath OutputDirectory;

	/// IoStore 配置
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IoStore")
	FHotUpdateIoStoreConfig IoStoreConfig;


	/// 预收集的完整资源列表
	TArray<FString> AssetPaths;

	/// 预收集的非 UE 资源列表（Staged 文件）
	TArray<FString> NonAssetPaths;

	bool bSynchronousMode;

	FHotUpdatePatchPackageConfig()
		: Platform(EHotUpdatePlatform::Windows)
		  , bIncludeDependencies(true)
		  , bSkipCook(false)
		  , bIncrementalCook(false)
		  , bSkipBuild(false)
		  , bSynchronousMode(false)
	{
	}
};


/**
 * 资源差异信息
 */
USTRUCT(BlueprintType)
struct HOTUPDATEEDITOR_API FHotUpdateAssetDiff
{
	GENERATED_BODY()

	/// 资源路径
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Diff")
	FString AssetPath;

	/// 变更类型
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Diff")
	EHotUpdateFileChangeType ChangeType;

	/// 旧文件大小（字节）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Diff")
	int64 OldSize;

	/// 新文件大小（字节）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Diff")
	int64 NewSize;

	/// 旧 Hash
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Diff")
	FString OldHash;

	/// 新 Hash
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Diff")
	FString NewHash;

	/// 详细变更描述
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Diff")
	FString ChangeDescription;

	FHotUpdateAssetDiff()
		: ChangeType(EHotUpdateFileChangeType::Unchanged)
		, OldSize(0)
		, NewSize(0)
	{
	}

	int64 GetSizeDifference() const { return NewSize - OldSize; }
};

/**
 * 版本差异报告
 */
USTRUCT(BlueprintType)
struct HOTUPDATEEDITOR_API FHotUpdateDiffReport
{
	GENERATED_BODY()

	/// 基础版本
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Report")
	FString BaseVersion;

	/// 目标版本
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Report")
	FString TargetVersion;

	/// 新增资源
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Report")
	TArray<FHotUpdateAssetDiff> AddedAssets;

	/// 修改资源
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Report")
	TArray<FHotUpdateAssetDiff> ModifiedAssets;

	/// 删除资源
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Report")
	TArray<FHotUpdateAssetDiff> DeletedAssets;

	/// 未变更资源
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Report")
	TArray<FHotUpdateAssetDiff> UnchangedAssets;

	/// 差异资源总数
	int32 GetTotalChangedCount() const
	{
		return AddedAssets.Num() + ModifiedAssets.Num() + DeletedAssets.Num();
	}

	/// 总大小变化（字节）
	int64 GetTotalSizeDifference() const
	{
		int64 TotalDiff = 0;
		for (const auto& Diff : AddedAssets) TotalDiff += Diff.NewSize;
		for (const auto& Diff : ModifiedAssets) TotalDiff += Diff.GetSizeDifference();
		for (const auto& Diff : DeletedAssets) TotalDiff -= Diff.OldSize;
		return TotalDiff;
	}
};

/**
 * 基础版本 Manifest 数据（已按资产类型分类）
 * 用于避免每次构建时筛选 .uasset/.umap
 */
struct HOTUPDATEEDITOR_API FHotUpdateBaseManifestData
{
	/// UE 资产 Hash 映射（仅 .uasset/.umap）
	TMap<FString, FString> AssetHashes;

	/// UE 资产大小映射
	TMap<FString, int64> AssetSizes;

	/// 非资产文件 Hash 映射
	TMap<FString, FString> NonAssetHashes;

	/// 非资产文件大小映射
	TMap<FString, int64> NonAssetSizes;

	FHotUpdateBaseManifestData()
		: AssetHashes(), AssetSizes(), NonAssetHashes(), NonAssetSizes()
	{}

	/// 获取总资产数
	int32 GetTotalAssetCount() const { return AssetHashes.Num(); }

	/// 获取总非资产数
	int32 GetTotalNonAssetCount() const { return NonAssetHashes.Num(); }

	/// 是否有效
	bool IsValid() const { return AssetHashes.Num() > 0 || NonAssetHashes.Num() > 0; }
};

/**
 * 更新包构建结果
 */
USTRUCT(BlueprintType)
struct HOTUPDATEEDITOR_API FHotUpdatePatchPackageResult
{
	GENERATED_BODY()

	/// 是否成功
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	bool bSuccess;

	/// 输出目录
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	FString OutputDirectory;

	/// 更新包容器文件（.utoc）
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	FString PatchUtocPath;

	/// 更新包容器文件（.ucas）
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	FString PatchUcasPath;

	/// Manifest 文件路径
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	FString ManifestFilePath;

	/// 更新包版本号
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	FString PatchVersion;

	/// 基础版本号
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	FString BaseVersion;

	/// 差异报告
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	struct FHotUpdateDiffReport DiffReport;

	/// 变更资源数
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	int32 ChangedAssetCount;

	/// 更新包大小
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	int64 PatchSize;

	
	/// 错误信息
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	FString ErrorMessage;

	FHotUpdatePatchPackageResult()
		: bSuccess(false)
		, ChangedAssetCount(0)
		, PatchSize(0)
	{
	}
};

/**
 * 打包进度信息
 */
USTRUCT(BlueprintType)
struct HOTUPDATEEDITOR_API FHotUpdatePackageProgress
{
	GENERATED_BODY()

	/// 当前阶段
	UPROPERTY(BlueprintReadOnly, Category = "Progress")
	FString CurrentStage;

	/// 阶段描述（用于显示）
	UPROPERTY(BlueprintReadOnly, Category = "Progress")
	FText StageDescription;

	/// 当前处理文件
	UPROPERTY(BlueprintReadOnly, Category = "Progress")
	FString CurrentFile;

	/// 已处理文件数
	UPROPERTY(BlueprintReadOnly, Category = "Progress")
	int32 ProcessedFiles;

	/// 总文件数
	UPROPERTY(BlueprintReadOnly, Category = "Progress")
	int32 TotalFiles;

	/// 已处理字节数
	UPROPERTY(BlueprintReadOnly, Category = "Progress")
	int64 ProcessedBytes;

	/// 总字节数
	UPROPERTY(BlueprintReadOnly, Category = "Progress")
	int64 TotalBytes;

	/// 进度百分比（0-100）
	UPROPERTY(BlueprintReadOnly, Category = "Progress")
	float ProgressPercent;

	/// 是否已完成
	UPROPERTY(BlueprintReadOnly, Category = "Progress")
	bool bIsComplete;

	/// 是否出错
	UPROPERTY(BlueprintReadOnly, Category = "Progress")
	bool bHasError;

	/// 错误信息
	UPROPERTY(BlueprintReadOnly, Category = "Progress")
	FString ErrorMessage;

	FHotUpdatePackageProgress()
		: ProcessedFiles(0)
		, TotalFiles(0)
		, ProcessedBytes(0)
		, TotalBytes(0)
		, ProgressPercent(0.0f)
		, bIsComplete(false)
		, bHasError(false)
	{
	}

	/// 获取进度百分比 (0-100)
	float GetProgressPercent() const { return ProgressPercent; }
};

/**
 * 编辑器版本信息（用于版本管理，比运行时版本信息更详细）
 */
USTRUCT(BlueprintType)
struct HOTUPDATEEDITOR_API FHotUpdateEditorVersionInfo
{
	GENERATED_BODY()

	/// 版本号
	UPROPERTY(BlueprintReadOnly, Category = "Version")
	FString VersionString;

	/// 包类型（基础包/更新包）
	UPROPERTY(BlueprintReadOnly, Category = "Version")
	EHotUpdatePackageKind PackageKind;

	/// 基础版本（仅更新包有效）
	UPROPERTY(BlueprintReadOnly, Category = "Version")
	FString BaseVersion;

	/// 平台
	UPROPERTY(BlueprintReadOnly, Category = "Version")
	EHotUpdatePlatform Platform;

	/// 创建时间
	UPROPERTY(BlueprintReadOnly, Category = "Version")
	FDateTime CreatedTime;

	/// FileManifest 文件路径（filemanifest.json，用于打包差异计算）
	UPROPERTY(BlueprintReadOnly, Category = "Version")
	FString FileManifestPath;

	/// IoStore 容器路径（.utoc）
	UPROPERTY(BlueprintReadOnly, Category = "Version")
	FString UtocPath;

	/// 资源数量
	UPROPERTY(BlueprintReadOnly, Category = "Version")
	int32 AssetCount;

	/// 包大小（字节）
	UPROPERTY(BlueprintReadOnly, Category = "Version")
	int64 PackageSize;

	FHotUpdateEditorVersionInfo()
		: PackageKind(EHotUpdatePackageKind::Base)
		, Platform(EHotUpdatePlatform::Windows)
		, AssetCount(0)
		, PackageSize(0)
	{
	}
};

/**
 * Android 纹理格式
 */
UENUM(BlueprintType)
enum class EHotUpdateAndroidTextureFormat : uint8
{
	ETC2            UMETA(DisplayName = "ETC2"),
	ASTC            UMETA(DisplayName = "ASTC"),
	DXT             UMETA(DisplayName = "DXT"),
	Multi           UMETA(DisplayName = "Multi")
};

// 打包进度委托（使用普通委托支持 Slate AddSP 绑定）
DECLARE_MULTICAST_DELEGATE_OneParam(FOnPackageProgressDelegate, const FHotUpdatePackageProgress&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnPatchPackageCompleteDelegate, const FHotUpdatePatchPackageResult&);

// 基础版本构建委托
DECLARE_MULTICAST_DELEGATE_OneParam(FOnBaseVersionBuildProgressDelegate, const FHotUpdateBaseVersionBuildProgress&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnBaseVersionBuildCompleteDelegate, const FHotUpdateBaseVersionBuildResult&);

/**
 * 打包进度辅助函数
 * 提供进度更新和广播的公共逻辑
 */
namespace HotUpdateProgressHelper
{
	/**
	 * 更新进度数据
	 * @param OutProgress 输出进度结构（会被更新）
	 * @param Stage 当前阶段
	 * @param CurrentFile 当前处理文件
	 * @param ProcessedFiles 已处理文件数
	 * @param TotalFiles 总文件数
	 * @param ProcessedBytes 已处理字节数（可选）
	 * @param TotalBytes 总字节数（可选）
	 */
	inline void UpdateProgressData(
		FHotUpdatePackageProgress& OutProgress,
		const FString& Stage,
		const FString& CurrentFile,
		int32 ProcessedFiles,
		int32 TotalFiles,
		int64 ProcessedBytes = 0,
		int64 TotalBytes = 0)
	{
		OutProgress.CurrentStage = Stage;
		OutProgress.CurrentFile = CurrentFile;
		OutProgress.ProcessedFiles = ProcessedFiles;
		OutProgress.TotalFiles = TotalFiles;
		OutProgress.ProcessedBytes = ProcessedBytes;
		OutProgress.TotalBytes = TotalBytes;
		OutProgress.bIsComplete = (ProcessedFiles >= TotalFiles && TotalFiles > 0);
		OutProgress.ProgressPercent = TotalFiles > 0 ? static_cast<float>(ProcessedFiles) / TotalFiles * 100.0f : 0.0f;
		OutProgress.StageDescription = FText::FromString(Stage);
	}

	/**
	 * 广播进度（同步模式）
	 * @param Progress 进度数据
	 * @param OnProgressDelegate 进度委托
	 */
	inline void BroadcastProgressSync(
		const FHotUpdatePackageProgress& Progress,
		FOnPackageProgressDelegate& OnProgressDelegate)
	{
		OnProgressDelegate.Broadcast(Progress);
	}

	/**
	 * 广播进度（异步模式，在游戏线程执行）
	 * 注意：此函数通过值捕获进度和委托，确保异步执行时数据安全
	 * @param Progress 进度数据
	 * @param OnProgressDelegate 进度委托引用
	 */
	inline void BroadcastProgressAsync(
		const FHotUpdatePackageProgress& Progress,
		FOnPackageProgressDelegate& OnProgressDelegate)
	{
		// 值捕获：避免异步执行时引用悬空
		FOnPackageProgressDelegate DelegateCopy = OnProgressDelegate;
		AsyncTask(ENamedThreads::GameThread, [DelegateCopy, Progress]()
		{
			DelegateCopy.Broadcast(Progress);
		});
	}

	/**
	 * 广播基础版本构建进度（异步模式，在游戏线程执行）
	 */
	inline void BroadcastBaseBuildProgressAsync(
		const FHotUpdateBaseVersionBuildProgress& Progress,
		FOnBaseVersionBuildProgressDelegate& OnProgressDelegate)
	{
		FOnBaseVersionBuildProgressDelegate DelegateCopy = OnProgressDelegate;
		AsyncTask(ENamedThreads::GameThread, [DelegateCopy, Progress]()
		{
			DelegateCopy.Broadcast(Progress);
		});
	}
}


/**
 * 输出格式
 */
UENUM(BlueprintType)
enum class EHotUpdateOutputFormat : uint8
{
	Pak             UMETA(DisplayName = "Pak 文件"),
	IoStore         UMETA(DisplayName = "IoStore 容器"),
	Both            UMETA(DisplayName = "两者都生成")
};

/**
 * 通用打包配置（简化版，用于编辑器设置）
 */
USTRUCT(BlueprintType)
struct HOTUPDATEEDITOR_API FHotUpdatePackageConfig
{
	GENERATED_BODY()

	/// 版本号
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Packaging")
	FString VersionString;

	/// 目标平台
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Packaging")
	EHotUpdatePlatform Platform;

	/// 输出格式
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Packaging")
	EHotUpdateOutputFormat OutputFormat;

	/// 输出目录
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Packaging")
	FDirectoryPath OutputDirectory;

	/// 是否启用压缩
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Packaging")
	bool bEnableCompression;

	/// 压缩级别（0-9）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Packaging", meta = (ClampMin = "0", ClampMax = "9"))
	int32 CompressionLevel;

	/// 是否包含依赖资源
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Packaging")
	bool bIncludeDependencies;

	/// Chunk ID（-1 表示自动分配）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Packaging")
	int32 ChunkId;

	/// 分包策略
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Packaging")
	EHotUpdateChunkStrategy ChunkStrategy;

	/// 按大小分包的最大 Chunk 大小（MB）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Packaging", meta = (ClampMin = "1"))
	int32 MaxChunkSizeMB;

	/// Android 纹理格式
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Packaging")
	EHotUpdateAndroidTextureFormat AndroidTextureFormat;

	/// 热更包基于的版本（可以是基础版本或热更版本）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hotfix")
	FString BasedOnVersion;

	/// 基础 FileManifest 路径（filemanifest.json，兼容旧配置）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hotfix")
	FString BaseFileManifestPath;

	/// 最小包配置
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MinimalPackage")
	FHotUpdateMinimalPackageConfig MinimalPackageConfig;

	FHotUpdatePackageConfig()
		: Platform(EHotUpdatePlatform::Windows)
		, OutputFormat(EHotUpdateOutputFormat::Pak)
		, bEnableCompression(true)
		, CompressionLevel(4)
		, bIncludeDependencies(true)
		, ChunkId(-1)
		, ChunkStrategy(EHotUpdateChunkStrategy::Size)
		, MaxChunkSizeMB(256)
		, AndroidTextureFormat(EHotUpdateAndroidTextureFormat::ETC2)
	{
	}
};

/**
 * 打包结果（通用版，用于打包面板）
 */
USTRUCT(BlueprintType)
struct HOTUPDATEEDITOR_API FHotUpdatePackageResult
{
	GENERATED_BODY()

	/// 是否成功
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	bool bSuccess;

	/// 版本号
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	FString VersionString;

	/// 输出文件路径
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	FString OutputFilePath;

	/// 文件大小（字节）
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	int64 FileSize;

	/// 资源数量
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	int32 AssetCount;

	/// Manifest 文件路径
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	FString ManifestFilePath;

	/// 错误信息
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	FString ErrorMessage;

	FHotUpdatePackageResult()
		: bSuccess(false)
		, FileSize(0)
		, AssetCount(0)
	{
	}
};
/**
 * 自定义打包配置
 */
USTRUCT(BlueprintType)
struct HOTUPDATEEDITOR_API FHotUpdateCustomPackageConfig
{
	GENERATED_BODY()

	/// 版本号（自动生成时间戳格式: Custom_YYYYMMDD_HHMMSS）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Version")
	FString PatchVersion;

	/// 目标平台
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Packaging")
	EHotUpdatePlatform Platform = EHotUpdatePlatform::Windows;

	/// uasset 文件路径列表（磁盘绝对路径）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Packaging")
	TArray<FString> UAssetFilePaths;

	/// 非资产文件路径列表（磁盘绝对路径）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Packaging")
	TArray<FString> NonAssetFilePaths;

	/// 输出目录
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Packaging")
	FDirectoryPath OutputDirectory;

	/// IoStore 配置
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IoStore")
	FHotUpdateIoStoreConfig IoStoreConfig;

	/// 是否增量 Cook（只 Cook 选中资源，不勾选则全量 Cook）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Packaging")
	bool bIncrementalCook = true;

	/// 是否跳过编译步骤
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Packaging")
	bool bSkipBuild = false;

	/// Android 纹理格式
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Android")
	EHotUpdateAndroidTextureFormat AndroidTextureFormat = EHotUpdateAndroidTextureFormat::ASTC;

	/// Pak 挂载优先级（容器名 _n_P 中的 n，0=默认_P，数字越大优先级越高）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Packaging", meta = (ClampMin = "0"))
	int32 PakPriority = 10;

	/// 自定义 Pak 名称（容器文件名，不含扩展名）
	FString CustomPakName = TEXT("CustomPatch_10");

	bool bSynchronousMode = false;
};

/**
 * 自定义打包结果
 */
USTRUCT(BlueprintType)
struct HOTUPDATEEDITOR_API FHotUpdateCustomPackageResult
{
	GENERATED_BODY()

	/// 是否成功
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	bool bSuccess = false;

	/// 错误信息
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	FString ErrorMessage;

	/// 输出目录
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	FString OutputDirectory;

	/// 版本号
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	FString PatchVersion;

	/// Pak/Utoc 文件路径
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	FString PatchUtocPath;

	/// 打包大小
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	int64 PatchSize = 0;

	/// 资源数量
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	int32 AssetCount = 0;

	/// Manifest 文件路径
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	FString ManifestFilePath;
};

/** 自定义打包完成委托（使用普通委托支持 Slate AddSP 绑定） */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnCustomPackageCompleteDelegate, const FHotUpdateCustomPackageResult&);
