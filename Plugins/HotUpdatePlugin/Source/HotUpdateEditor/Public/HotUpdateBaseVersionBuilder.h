// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HotUpdateEditorTypes.h"
#include "HotUpdatePackagingSettingsHelper.h"
#include "HotUpdateBaseVersionBuilder.generated.h"

class IAssetRegistry;

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

	/// 是否跳过编译步骤（默认 false = 编译后再 Cook）
	/// 在编辑器进程内运行 commandlet 时，DLL 被锁定无法重新编译，需要跳过
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

	/// 预收集的 Staged 文件列表（非 UE 资产）
	TArray<FString> PreCollectedNonAssetFiles;

	FHotUpdateBaseVersionBuildConfig()
		: Platform(EHotUpdatePlatform::Windows)
		, BuildConfiguration(EHotUpdateBuildConfiguration::Development)
		, AndroidPackageName(TEXT("com.dragonli.czm"))
		, AndroidTextureFormat(EHotUpdateAndroidTextureFormat::ETC2)
		, bSkipBuild(false)
		, bSynchronousMode(false)
	{
	}
};

// 资源解析结果前向声明（实现细节，定义在 .cpp）
struct FHotUpdateResolvedAssetInfo;

/**
 * 非资产文件的 Chunk 分配信息
 */
struct FNonAssetChunkInfo
{
	int32 ChunkId = 0;
	FString PakInternalPath;  // pak 内部路径，如 ../../../GameUpdate/Content/Setting/txt_pak.txt
	FString SourcePath;        // 完整磁盘路径，如 E:\Test\HotPatch\GameUpdate\Content\Setting\txt_pak.txt
};

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
	bool ExecuteUATPackage(const FString& UATCommand, FString& OutError) const;

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
	 * 收集指定目录下的 IoStore/Pak 容器文件信息
	 * @param SearchDir 搜索目录
	 * @param BaseDir 基准目录（用于计算相对路径）
	 * @param ContainerType 容器类型（Base/Patch）
	 * @param OutContainerInfos 输出容器信息数组
	 */
	static void CollectContainerFiles(
		const FString& SearchDir,
		const FString& BaseDir,
		EHotUpdateContainerType ContainerType,
		TArray<FHotUpdateContainerInfo>& OutContainerInfos);

	/**
	 * 生成并保存 manifest.json
	 * @param VersionDir
	 * @param ContainerInfos
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
	 * 解析资源磁盘路径并构建解析信息（消除 ConvertAssetPathToFileName 的冗余 GetCookedAssetPath 调用）
	 */
	static TArray<FHotUpdateResolvedAssetInfo> ResolveAssetInfo(
		const TArray<FString>& AssetPaths,
		const FString& CookedPlatformDir);

	/**
	 * 生成并保存 filemanifest.json（去重基础/热更资源的文件条目生成）
	 */
	static bool BuildFileManifestJson(
		const FString& VersionDir,
		const TSharedPtr<FJsonObject>& VersionObject,
		const TArray<TSharedPtr<FJsonValue>>& ChunksArray,
		const TArray<FHotUpdateResolvedAssetInfo>& BaseAssets,
		const TArray<FHotUpdateResolvedAssetInfo>& PatchAssets);

	/**
	 * 检查打包输出是否存在
	 */
	bool CheckBuildOutput(const FString& OutputDir, FString& OutExecutablePath) const;

	/**
	 * 更新进度
	 */
	void UpdateProgress(const FString& Stage, float Percent, const FString& Message);

	/**
	 * 广播构建结果（根据同步/异步模式选择直接广播或调度到游戏线程）
	 */
	void BroadcastBuildResult(const FHotUpdateBaseVersionBuildResult& Result);

	/**
	 * 创建错误结果
	 */
	static FHotUpdateBaseVersionBuildResult MakeErrorResult(const FString& ErrorMessage);

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

	/// 缓存的非资产文件 Chunk 映射（ChunkId -> 文件列表），由 PreComputeChunkMapping 填充
	TMap<int32, TArray<FNonAssetChunkInfo>> CachedNonAssetChunkMapping;
};