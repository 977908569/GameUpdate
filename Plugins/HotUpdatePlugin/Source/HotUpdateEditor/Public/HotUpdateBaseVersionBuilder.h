// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HotUpdateEditorTypes.h"
#include "HotUpdateBaseVersionBuilder.generated.h"

class IAssetRegistry;

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

// 构建进度委托 (使用多播委托以支持 Slate 控件绑定)
DECLARE_MULTICAST_DELEGATE_OneParam(FOnBaseVersionBuildProgressDelegate, const FHotUpdateBaseVersionBuildProgress&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnBaseVersionBuildCompleteDelegate, const FHotUpdateBaseVersionBuildResult&);

/**
 * 基础版本构建配置
 */
USTRUCT(BlueprintType)
struct HOTUPDATEEDITOR_API FHotUpdateBaseVersionBuildConfig
{
	GENERATED_BODY()

	/// 版本号
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString VersionString;

	/// 目标平台
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EHotUpdatePlatform Platform;

	/// 输出目录（空则使用默认）
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString OutputDirectory;

	/// 构建配置（Development 包含调试信息，Shipping 为发布构建）
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EHotUpdateBuildConfiguration BuildConfiguration;

	/// Android 打包配置
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString AndroidPackageName;

	/// Android 纹理格式
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EHotUpdateAndroidTextureFormat AndroidTextureFormat;

	/// 是否打包所有资源（true）还是只打包项目资源（false）
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bPackageAll;

	/// 是否跳过编译步骤（如果项目已编译，可以跳过以避免 Live Coding 冲突）
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bSkipBuild;

	/// 同步执行模式
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bSynchronousMode;

	/// 最小包配置（控制基础包只包含指定资源）
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FHotUpdateMinimalPackageConfig MinimalPackageConfig;

	/// 预收集的热更资产列表（ApplyMinimalPackageFilter 输出，由游戏线程填充）
	TArray<FString> PreCollectedPatchAssetPaths;

	FHotUpdateBaseVersionBuildConfig()
		: Platform(EHotUpdatePlatform::Windows)
		, BuildConfiguration(EHotUpdateBuildConfiguration::Development)
		, AndroidPackageName(TEXT("com.dragonli.czm"))
		, AndroidTextureFormat(EHotUpdateAndroidTextureFormat::ETC2)
		, bPackageAll(true)
		, bSkipBuild(false)
		, bSynchronousMode(false)
	{
	}
};

// 资源解析结果前向声明（实现细节，定义在 .cpp）
struct FHotUpdateResolvedAssetInfo;

/**
 * 基础版本构建器
 * 负责完整的项目打包（exe/apk）并保存为基础版本
 * 继承 TSharedFromThis 以支持异步任务中的弱引用安全访问
 */
class HOTUPDATEEDITOR_API FHotUpdateBaseVersionBuilder : public TSharedFromThis<FHotUpdateBaseVersionBuilder>
{
public:
	FHotUpdateBaseVersionBuilder();

	/**
	 * 开始构建基础版本
	 * @param Config 构建配置
	 */
	void BuildBaseVersion(const FHotUpdateBaseVersionBuildConfig& Config);

	/**
	 * 取消构建
	 */
	void CancelBuild();

	/**
	 * 是否正在构建
	 */
	bool IsBuilding() const { return bIsBuilding; }

	/**
	 * 获取默认输出目录
	 */
	static FString GetDefaultOutputDirectory();

	// 进度委托
	FOnBaseVersionBuildProgressDelegate OnBuildProgress;

	// 完成委托
	FOnBaseVersionBuildCompleteDelegate OnBuildComplete;

private:
	/**
	 * 执行构建
	 */
	void ExecuteBuildInternal();

	/**
	 * 写入最小包配置到临时文件，供 UAT 打包时读取
	 */
	void WriteMinimalPackageConfig();

	/**
	 * 执行 UAT 打包命令
	 */
	bool ExecuteUATPackage(const FString& UATCommand, FString& OutError);

	/**
	 * 生成 UAT 命令行
	 */
	FString GenerateUATCommand();

	/**
	 * 打包完成后保存资源Hash清单
	 */
	bool SaveResourceHashesInGameThread();

	/**
	 * 解析平台打包输出目录（处理 Android 纹理格式命名）
	 * @return 解析后的平台目录路径，失败返回空字符串
	 */
	FString ResolvePlatformOutputDir() const;

	/**
	 * 收集 IoStore 容器文件信息（.utoc/.ucas）
	 */
	TArray<FHotUpdateContainerInfo> CollectContainerInfos(const FString& PlatformDir, const FString& VersionDir) const;

	/**
	 * 生成并保存 manifest.json
	 * @param OutVersionObject 共享的版本信息 JSON 对象（供 filemanifest.json 复用）
	 * @param OutChunksArray 共享的 chunks 数组（供 filemanifest.json 复用）
	 */
	bool BuildManifestJson(
		const FString& VersionDir,
		const TArray<FHotUpdateContainerInfo>& ContainerInfos,
		TSharedPtr<FJsonObject>& OutVersionObject,
		TArray<TSharedPtr<FJsonValue>>& OutChunksArray) const;

	/**
	 * 预计算非白名单资源的 Chunk 分配，用 ChunkManager 按策略分包
	 */
	void PreComputeChunkMapping();

	/**
	 * 解析资源磁盘路径并构建解析信息（消除 ConvertAssetPathToFileName 的冗余 GetAssetDiskPath 调用）
	 */
	TArray<FHotUpdateResolvedAssetInfo> ResolveAssetInfo(
		const TArray<FString>& AssetPaths,
		const FString& CookedPlatformDir) const;

	/**
	 * 生成并保存 filemanifest.json（去重基础/热更资源的文件条目生成）
	 */
	bool BuildFileManifestJson(
		const FString& VersionDir,
		const TSharedPtr<FJsonObject>& VersionObject,
		const TArray<TSharedPtr<FJsonValue>>& ChunksArray,
		const TArray<FHotUpdateResolvedAssetInfo>& BaseAssets,
		const TArray<FHotUpdateResolvedAssetInfo>& PatchAssets) const;

	/**
	 * 检查打包输出是否存在
	 */
	bool CheckBuildOutput(const FString& OutputDir, FString& OutExecutablePath);

	/**
	 * 更新进度
	 */
	void UpdateProgress(const FString& Stage, float Percent, const FString& Message);

private:
	/// 构建配置
	FHotUpdateBaseVersionBuildConfig CurrentConfig;

	/// 是否正在构建
	std::atomic<bool> bIsBuilding;

	/// 是否已取消
	std::atomic<bool> bIsCancelled;

	/// 构建任务
	TFuture<void> BuildTask;

	/// 缓存的 Chunk 映射（PackageName -> ChunkId），由 PreComputeChunkMapping 填充
	TMap<FString, int32> CachedChunkMapping;

	/// 缓存的 Chunk 定义列表，由 PreComputeChunkMapping 填充
	TArray<FHotUpdateChunkDefinition> CachedChunkDefinitions;

	/// 缓存的白名单资源列表（首包 Chunk0 资源），由 BuildBaseVersion 填充
	TArray<FString> CachedWhitelistAssetPaths;
};