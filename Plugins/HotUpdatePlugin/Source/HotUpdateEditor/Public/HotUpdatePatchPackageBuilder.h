// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HotUpdateEditorTypes.h"
#include "HotUpdateIoStoreBuilder.h"
#include "HotUpdateChunkManager.h"
#include "HotUpdateVersionManager.h"
#include "HAL/CriticalSection.h"
#include <atomic>
#include "HotUpdatePatchPackageBuilder.generated.h"

/**
 * 更新包构建器
 * 基于基础包生成差异更新包
 */
UCLASS(BlueprintType)
class HOTUPDATEEDITOR_API UHotUpdatePatchPackageBuilder : public UObject
{
	GENERATED_BODY()

public:
	UHotUpdatePatchPackageBuilder();

	/**
	 * 构建更新包
	 */
	UFUNCTION(BlueprintCallable, Category = "Hot Update|PatchPackage")
	FHotUpdatePatchPackageResult BuildPatchPackage(const FHotUpdatePatchPackageConfig& Config);

	/**
	 * 异步构建更新包
	 */
	UFUNCTION(BlueprintCallable, Category = "Hot Update|PatchPackage")
	void BuildPatchPackageAsync(const FHotUpdatePatchPackageConfig& Config);

	/**
	 * 预览差异
	 */
	UFUNCTION(BlueprintCallable, Category = "Hot Update|PatchPackage")
	FHotUpdateDiffReport PreviewDiff(const FHotUpdatePatchPackageConfig& Config);

	/**
	 * 取消构建
	 */
	UFUNCTION(BlueprintCallable, Category = "Hot Update|PatchPackage")
	void CancelBuild();

	/**
	 * 是否正在构建
	 */
	UFUNCTION(BlueprintPure, Category = "Hot Update|PatchPackage")
	bool IsBuilding() const { return bIsBuilding; }

	/**
	 * 获取当前进度
	 */
	UFUNCTION(BlueprintPure, Category = "Hot Update|PatchPackage")
	FHotUpdatePackageProgress GetCurrentProgress() const;

	/**
	 * 验证配置
	 */
	bool ValidateConfig(const FHotUpdatePatchPackageConfig& Config, FString& OutErrorMessage);

	// 进度委托
	UPROPERTY(BlueprintAssignable, Category = "Hot Update|PatchPackage")
	FOnPackageProgressDelegate OnProgress;

	// 完成委托
	UPROPERTY(BlueprintAssignable, Category = "Hot Update|PatchPackage")
	FOnPatchPackageCompleteDelegate OnComplete;

	/**
	 * 将资源路径转换为文件名（相对路径，带后缀）
	 * 如 "/Game/Startup/umg_hotupdate" -> "Game/Startup/umg_hotupdate.uasset"
	 * 如 "/Game/Maps/MainMenu" -> "Game/Maps/MainMenu.umap"
	 */
	static FString ConvertAssetPathToFileName(const FString& AssetPath);

	/**
	 * 获取资源磁盘路径
	 */
		static FString GetAssetDiskPath(const FString& AssetPath, const FString& CookedPlatformDir = TEXT(""));

private:
	/**
	 * Cook 资源
	 * 使用子进程执行 Cook，避免在当前 Editor 进程中调用 CookCommandlet 导致的平台冲突
	 */
	bool CookAssets(const FHotUpdatePatchPackageConfig& Config);

	/**
	 * 收集资源
	 */
	bool CollectAssets(
		const FHotUpdatePatchPackageConfig& Config,
		TArray<FString>& OutAssetPaths,
		TMap<FString, FString>& OutAssetDiskPaths,
		FString& OutErrorMessage);

	/**
	 * 加载基础版本 Manifest
	 */
	bool LoadBaseManifest(
		const FString& ManifestPath,
		TMap<FString, FString>& OutAssetHashes,
		TMap<FString, int64>& OutAssetSizes);

	/**
	 * 计算差异
	 */
	bool ComputeDiff(
		const TArray<FString>& CurrentAssets,
		const TMap<FString, FString>& CurrentHashes,
		const TMap<FString, FString>& BaseHashes,
		TArray<FString>& OutChangedAssets,
		FHotUpdateDiffReport& OutReport);

	/**
	 * 加载之前的 Patch Manifest（用于链式 Patch）
	 * @param ManifestPath Manifest 文件路径
	 * @param OutContainers 输出的容器信息列表
	 * @param OutPatchFiles 输出的 patch 文件信息（source=patch 的文件）
	 * @param OutPatchVersion patch 版本号
	 * @return 是否成功加载
	 */
	bool LoadPreviousPatchManifest(
		const FString& ManifestPath,
		TArray<FHotUpdateContainerInfo>& OutContainers,
		TMap<FString, FString>& OutPatchFilesHash,
		TMap<FString, int64>& OutPatchFilesSize,
		FString& OutPatchVersion);

	/**
	 * 加载基础版本容器信息（用于全量热更新）
	 * @param ContainerDirectory 基础版本容器目录
	 * @param OutContainers 输出的容器信息列表
	 * @param OutFilesHash 输出的文件 Hash 信息
	 * @param OutFilesSize 输出的文件大小信息
	 * @return 是否成功加载
	 */
	bool LoadBaseContainers(
		const FString& ContainerDirectory,
		TArray<FHotUpdateContainerInfo>& OutContainers,
		TMap<FString, FString>& OutFilesHash,
		TMap<FString, int64>& OutFilesSize);

	/**
	 * 复制容器文件到输出目录
	 * @param SourceDir 源目录
	 * @param DestDir 目标目录
	 * @param Containers 要复制的容器列表
	 * @return 成功复制的容器数量
	 */
	int32 CopyContainerFiles(
		const FString& SourceDir,
		const FString& DestDir,
		const TArray<FHotUpdateContainerInfo>& Containers);

	/**
	 * 生成 Manifest
	 * @param ManifestPath Manifest 输出路径
	 * @param PatchUtocPath Patch .utoc 文件路径
	 * @param PatchUcasPath Patch .ucas 文件路径
	 * @param ChangedAssetPaths 变更资源路径列表（新增+修改）
	 * @param ChangedAssetDiskPaths 变更资源的磁盘路径
	 * @param BaseAssetHashes 基础版本资源 Hash（用于未变更资源）
	 * @param BaseAssetSizes 基础版本资源大小（用于未变更资源）
	 * @param DiffReport 差异报告
	 * @param Config 配置
	 */
	bool GenerateManifest(
		const FString& ManifestPath,
		const FString& PatchUtocPath,
		const FString& PatchUcasPath,
		const TArray<FString>& ChangedAssetPaths,
		const TMap<FString, FString>& ChangedAssetDiskPaths,
		const TMap<FString, FString>& BaseAssetHashes,
		const TMap<FString, int64>& BaseAssetSizes,
		const FHotUpdateDiffReport& DiffReport,
		const FHotUpdatePatchPackageConfig& Config,
		const TArray<FHotUpdateContainerInfo>& ChainPatchContainers = TArray<FHotUpdateContainerInfo>(),
		const TMap<FString, FString>& PreviousPatchFilesHash = TMap<FString, FString>(),
		const TMap<FString, int64>& PreviousPatchFilesSize = TMap<FString, int64>(),
		const TArray<FString>& PatchVersionChain = TArray<FString>(),
		const TArray<FHotUpdateContainerInfo>& BaseContainers = TArray<FHotUpdateContainerInfo>(),
		const TMap<FString, FString>& BaseContainerFilesHash = TMap<FString, FString>(),
		const TMap<FString, int64>& BaseContainerFilesSize = TMap<FString, int64>());

	/**
	 * 更新进度
	 */
	void UpdateProgress(
		const FString& Stage,
		const FString& CurrentFile,
		int32 ProcessedFiles,
		int32 TotalFiles);

	/// 是否正在构建
	std::atomic<bool> bIsBuilding;

	/// 是否已取消
	std::atomic<bool> bIsCancelled;

	/// 当前进度
	FHotUpdatePackageProgress CurrentProgress;

	/// 进度临界区
	mutable FCriticalSection ProgressCriticalSection;

	/// 构建任务
	TFuture<void> BuildTask;
};