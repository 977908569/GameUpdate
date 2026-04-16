// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HotUpdateEditorTypes.h"
#include "HotUpdateIoStoreBuilder.h"
#include "HotUpdateChunkManager.h"
#include "HotUpdateVersionManager.h"
#include "HAL/CriticalSection.h"
#include <atomic>
#include "HotUpdateBasePackageBuilder.generated.h"

/**
 * 基础包构建器
 * 负责创建完整的基础资源包
 */
UCLASS(BlueprintType)
class HOTUPDATEEDITOR_API UHotUpdateBasePackageBuilder : public UObject
{
	GENERATED_BODY()

public:
	UHotUpdateBasePackageBuilder();

	/**
	 * 构建基础包
	 */
	UFUNCTION(BlueprintCallable, Category = "Hot Update|BasePackage")
	FHotUpdateBasePackageResult BuildBasePackage(const FHotUpdateBasePackageConfig& Config);

	/**
	 * 异步构建基础包
	 */
	UFUNCTION(BlueprintCallable, Category = "Hot Update|BasePackage")
	void BuildBasePackageAsync(const FHotUpdateBasePackageConfig& Config);

	/**
	 * 取消构建
	 */
	UFUNCTION(BlueprintCallable, Category = "Hot Update|BasePackage")
	void CancelBuild();

	/**
	 * 是否正在构建
	 */
	UFUNCTION(BlueprintPure, Category = "Hot Update|BasePackage")
	bool IsBuilding() const { return bIsBuilding; }

	/**
	 * 获取当前进度
	 */
	UFUNCTION(BlueprintPure, Category = "Hot Update|BasePackage")
	FHotUpdatePackageProgress GetCurrentProgress() const;

	/**
	 * 验证配置
	 */
	bool ValidateConfig(const FHotUpdateBasePackageConfig& Config, FString& OutErrorMessage);

	// 进度委托
	UPROPERTY(BlueprintAssignable, Category = "Hot Update|BasePackage")
	FOnPackageProgressDelegate OnProgress;

	// 完成委托
	UPROPERTY(BlueprintAssignable, Category = "Hot Update|BasePackage")
	FOnBasePackageCompleteDelegate OnComplete;

private:
	/**
	 * 收集资源
	 */
	bool CollectAssets(
		const FHotUpdateBasePackageConfig& Config,
		TArray<FString>& OutAssetPaths,
		TMap<FString, FString>& OutAssetDiskPaths,
		FString& OutErrorMessage);

	/**
	 * 编译项目
	 * 使用 UAT BuildCookRun -build 编译游戏代码
	 * 必须在 Cook 之前调用，确保 Cook 使用最新的代码
	 */
	bool CompileProject(const FHotUpdateBasePackageConfig& Config);

	/**
	 * Cook 资源
	 * 使用子进程执行 Cook，避免在当前 Editor 进程中调用 CookCommandlet 导致的平台冲突
	 */
	bool CookAssets(const FHotUpdateBasePackageConfig& Config);

	/**
	 * 生成 Manifest
	 */
	bool GenerateManifest(
		const FString& ManifestPath,
		const TArray<FString>& AssetPaths,
		const TMap<FString, FString>& AssetDiskPaths,
		const TArray<FHotUpdateChunkDefinition>& Chunks,
		const TArray<FHotUpdateContainerInfo>& Containers,
		const FHotUpdateBasePackageConfig& Config);

	/**
	 * 更新进度
	 */
	void UpdateProgress(
		const FString& Stage,
		const FString& CurrentFile,
		int32 ProcessedFiles,
		int32 TotalFiles);

	/**
	 * 使用预收集的资源构建基础包（可在后台线程安全调用）
	 */
	FHotUpdateBasePackageResult BuildBasePackageWithPreCollectedAssets(
		const FHotUpdateBasePackageConfig& Config,
		const TArray<FString>& PreCollectedAssetPaths,
		const TMap<FString, FString>& PreCollectedAssetDiskPaths);

	/**
	 * 内部构建逻辑（Chunk分析、IoStore创建、清单生成、版本注册）
	 */
	FHotUpdateBasePackageResult BuildBasePackageInternal(
		const FHotUpdateBasePackageConfig& Config,
		const TArray<FString>& AssetPaths,
		const TMap<FString, FString>& AssetDiskPaths);

private:
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

	// 小包模式分区数据（仅在小包模式启用时有效）
	TArray<FString> MinimalWhitelistAssets;
	TArray<FString> MinimalEngineAssets;
	TArray<FString> MinimalExcludedAssets;
};