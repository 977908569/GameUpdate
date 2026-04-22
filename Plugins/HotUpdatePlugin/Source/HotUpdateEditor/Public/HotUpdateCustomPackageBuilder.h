// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HotUpdateEditorTypes.h"

/**
 * 自定义打包构建器
 * 独立于热更新打包流程，只 Cook 指定资源，不做版本差异对比
 */
class HOTUPDATEEDITOR_API FHotUpdateCustomPackageBuilder : public TSharedFromThis<FHotUpdateCustomPackageBuilder>
{
public:
	FHotUpdateCustomPackageBuilder();
	
	/** 异步构建自定义包 */
	void BuildCustomPackageAsync(const FHotUpdateCustomPackageConfig& Config);

	/** 取消构建 */
	void CancelBuild();

	/** 是否正在构建 */
	bool IsBuilding() const { return bIsBuilding; }

	/** 获取当前进度 */
	FHotUpdatePackageProgress GetCurrentProgress() const;

	/** 进度委托 */
	FOnPackageProgressDelegate OnProgress;

	/** 完成委托 */
	FOnCustomPackageCompleteDelegate OnComplete;

private:
	/** 在 GameThread 将 uasset 磁盘路径反向解析为 UE 包名（供 Cook 使用） */
	TArray<FString> ResolveUassetPathsForCook() const;

	/** 将 uasset 磁盘路径反向解析为 UE 包名 */
	static FString ResolveDiskPathToPackageName(const FString& DiskPath);

	/** 确定非资产文件在 Pak 内的挂载路径 */
	FString DetermineNonAssetPakPath(const FString& DiskPath) const;

	/** 后台线程执行的构建逻辑（不含 AssetRegistry 调用） */
	FHotUpdateCustomPackageResult ExecuteBuild(const FHotUpdateCustomPackageConfig& Config, const TArray<FString>& AssetPathsToCook);

	/** 更新进度 */
	void UpdateProgress(const FString& Stage, const FString& CurrentFile, int32 ProcessedFiles, int32 TotalFiles);

private:
	/** 当前配置 */
	FHotUpdateCustomPackageConfig CurrentConfig;

	/** 是否正在构建 */
	std::atomic<bool> bIsBuilding;

	/** 是否已取消 */
	std::atomic<bool> bIsCancelled;

	/** 当前进度 */
	FHotUpdatePackageProgress CurrentProgress;

	/** 进度临界区 */
	mutable FCriticalSection ProgressCriticalSection;

	/** 异步任务 */
	TFuture<void> BuildTask;
};