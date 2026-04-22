// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HotUpdateEditorTypes.h"
#include "HAL/CriticalSection.h"
#include <atomic>

/**
 * 热更新打包构建器
 * 基于基础包生成差异更新包
 */
class HOTUPDATEEDITOR_API FHotUpdatePatchPackageBuilder : public TSharedFromThis<FHotUpdatePatchPackageBuilder>
{
public:
	FHotUpdatePatchPackageBuilder();

	/** 构建更新包 */
	FHotUpdatePatchPackageResult BuildPatchPackage(const FHotUpdatePatchPackageConfig& Config);

	/** 异步构建更新包 */
	void BuildPatchPackageAsync(const FHotUpdatePatchPackageConfig& Config);
	
	/** 取消构建 */
	void CancelBuild();

	/** 是否正在构建 */
	bool IsBuilding() const { return bIsBuilding; }
	
	/** 验证配置 */
	bool ValidateConfig(const FHotUpdatePatchPackageConfig& Config, FString& OutErrorMessage);
	
	// 进度委托
	FOnPackageProgressDelegate OnProgress;

	// 完成委托
	FOnPatchPackageCompleteDelegate OnComplete;

private:
	/** 收集资源 */
	bool CollectAssets(
		TArray<FString>& OutAssetPaths,
		TMap<FString, FString>& OutAssetDiskPaths,
		TMap<FString, FString>& OutAssetSourcePaths,
		FString& OutErrorMessage);

	/** 加载基础版本 Manifest */
	bool LoadBaseManifest(
		const FString& ManifestPath,
		TMap<FString, FString>& OutAssetHashes,
		TMap<FString, int64>& OutAssetSizes);

	/** 计算差异 */
	bool ComputeDiff(
		const TArray<FString>& CurrentAssets,
		const TMap<FString, FString>& CurrentHashes,
		const TMap<FString, FString>& BaseHashes,
		TArray<FString>& OutChangedAssets,
		FHotUpdateDiffReport& OutReport);

	/** 加载之前的 Patch Manifest（链式 Patch） */
	bool LoadPreviousPatchManifest(
		const FString& ManifestPath,
		TArray<FHotUpdateContainerInfo>& OutContainers,
		TMap<FString, FString>& OutPatchFilesHash,
		TMap<FString, int64>& OutPatchFilesSize,
		FString& OutPatchVersion);

	/** 加载基础版本容器信息（全量热更新） */
	bool LoadBaseContainers(
		const FString& ContainerDirectory,
		TArray<FHotUpdateContainerInfo>& OutContainers,
		TMap<FString, FString>& OutFilesHash,
		TMap<FString, int64>& OutFilesSize);

	/** 生成 Manifest */
	bool GenerateManifest(
		const FString& ManifestPath,
		const FString& PatchUtocPath,
		const FString& PatchUcasPath,
		const TArray<FString>& ChangedAssetPaths,
		const TMap<FString, FString>& ChangedAssetDiskPaths,
		const TMap<FString, FString>& BaseAssetHashes,
		const TMap<FString, int64>& BaseAssetSizes,
		const FHotUpdateDiffReport& DiffReport,
		const TArray<FHotUpdateContainerInfo>& ChainPatchContainers = TArray<FHotUpdateContainerInfo>(),
		const TMap<FString, FString>& PreviousPatchFilesHash = TMap<FString, FString>(),
		const TMap<FString, int64>& PreviousPatchFilesSize = TMap<FString, int64>(),
		const TArray<FString>& PatchVersionChain = TArray<FString>(),
		const TArray<FHotUpdateContainerInfo>& BaseContainers = TArray<FHotUpdateContainerInfo>(),
		const TMap<FString, FString>& BaseContainerFilesHash = TMap<FString, FString>(),
		const TMap<FString, int64>& BaseContainerFilesSize = TMap<FString, int64>());

	/** 更新进度 */
	void UpdateProgress(
		const FString& Stage,
		const FString& CurrentFile,
		int32 ProcessedFiles,
		int32 TotalFiles);

	/// 构建配置
	FHotUpdatePatchPackageConfig CurrentConfig;

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