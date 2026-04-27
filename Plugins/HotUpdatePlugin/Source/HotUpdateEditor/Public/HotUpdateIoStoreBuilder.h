// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HotUpdateEditorTypes.h"
#include "HAL/CriticalSection.h"
#include <atomic>

/**
 * IoStore 构建进度
 */
struct HOTUPDATEEDITOR_API FHotUpdateIoStoreProgress
{
	FString CurrentStage;
	FString CurrentFile;
	int32 ProcessedFiles;
	int32 TotalFiles;
	int64 ProcessedBytes;
	int64 TotalBytes;
	bool bIsComplete;
	bool bHasError;
	FString ErrorMessage;

	FHotUpdateIoStoreProgress()
		: ProcessedFiles(0)
		, TotalFiles(0)
		, ProcessedBytes(0)
		, TotalBytes(0)
		, bIsComplete(false)
		, bHasError(false)
	{
	}

	float GetProgressPercent() const
	{
		return TotalFiles > 0 ? static_cast<float>(ProcessedFiles) / TotalFiles * 100.0f : 0.0f;
	}
};

// 进度委托
DECLARE_MULTICAST_DELEGATE_OneParam(FOnIoStoreProgressDelegate, const FHotUpdateIoStoreProgress&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnIoStoreCompleteDelegate, const FHotUpdateIoStoreResult&);

/**
 * IoStore 构建器
 * 使用 UE5 IoStore API 创建 .utoc/.ucas 容器文件
 * 继承 TSharedFromThis 以支持异步任务中的弱引用安全访问
 */
class HOTUPDATEEDITOR_API FHotUpdateIoStoreBuilder : public TSharedFromThis<FHotUpdateIoStoreBuilder>
{
public:
	FHotUpdateIoStoreBuilder();

	/**
	 * 构建 IoStore 容器
	 * @param AssetPaths 资源包路径数组（如 "/Game/Maps/Start"）
	 * @param OutputPath 输出路径（不含扩展名）
	 * @param Config IoStore 配置
	 * @param CookedPlatformDir Cooked 平台目录路径（可选，用于查找 .uexp/.ubulk 配套文件）
	 * @return 构建结果
	 */
	FHotUpdateIoStoreResult BuildIoStoreContainer(
		const TArray<FString>& AssetPaths,
		const FString& OutputPath,
		const FHotUpdateIoStoreConfig& Config,
		const FString& CookedPlatformDir = TEXT(""));

	/**
	 * 取消构建
	 */
	void CancelBuild();

	/**
	 * 是否正在构建
	 */
	bool IsBuilding() const { return bIsBuilding; }

	/**
	 * 获取当前进度
	 */
	FHotUpdateIoStoreProgress GetCurrentProgress() const;

	/**
	 * 验证配置
	 */
	static bool ValidateConfig(const FHotUpdateIoStoreConfig& Config, FString& OutErrorMessage);

	// 进度委托
	FOnIoStoreProgressDelegate OnProgress;

	// 完成委托
	FOnIoStoreCompleteDelegate OnComplete;

private:
	/**
	 * 使用 UnrealPak 创建 IoStore 格式容器
	 */
	bool CreateIoStoreWithUnrealPak(
		const TArray<FString>& AssetPaths,
		const FString& OutputPath,
		const FHotUpdateIoStoreConfig& Config,
		const FString& CookedPlatformDir,
		FHotUpdateIoStoreResult& OutResult);

	/**
	 * 生成加密密钥文件
	 */
	static FString GenerateCryptoKeyFile(
		const FString& TempDir,
		const FHotUpdateIoStoreConfig& Config);

	/**
	 * 查找 UnrealPak 工具路径
	 */
	static FString FindUnrealPakPath(FString& OutErrorMessage);

	/**
	 * 准备临时目录
	 */
	static bool PrepareTempDirectory(
		const FString& TempDir,
		FString& OutErrorMessage);

	/**
	 * 生成响应文件
	 */
	bool GenerateResponseFile(
		const TArray<FString>& AssetPaths,
		const FString& ResponseFilePath,
		const FString& CompressionFormat,
		const FString& CookedPlatformDir,
		int32& OutValidFileCount,
		int64& OutTotalSize,
		FString& OutErrorMessage);

	/**
	 * 准备输出目录，删除已存在的输出文件
	 */
	static bool PrepareOutputDirectory(
		const FString& OutputPath,
		FString& OutErrorMessage);

	/**
	 * 构建 UnrealPak 命令行参数
	 */
	static FString BuildUnrealPakCommandLine(
		const FString& OutputPath,
		const FString& ResponseFilePath,
		const FString& CryptoKeyPath,
		const FHotUpdateIoStoreConfig& Config);

	/**
	 * 执行 UnrealPak 并验证结果
	 */
	static bool ExecuteUnrealPak(
		const FString& UnrealPakPath,
		const FString& CmdLine,
		const FString& OutputPath,
		FHotUpdateIoStoreResult& OutResult);

	/**
	 * 清理临时目录
	 */
	static void CleanupTempDirectory(const FString& TempDir);

	/**
	 * 更新进度
	 */
	void UpdateProgress(
		const FString& Stage,
		const FString& CurrentFile,
		int32 ProcessedFiles,
		int32 TotalFiles,
		int64 ProcessedBytes,
		int64 TotalBytes);

private:
	/// 是否正在构建
	std::atomic<bool> bIsBuilding;

	/// 是否已取消
	std::atomic<bool> bIsCancelled;

	/// 当前进度
	FHotUpdateIoStoreProgress CurrentProgress;

	/// 进度临界区
	mutable FCriticalSection ProgressCriticalSection;

	/// 构建任务
	TFuture<void> BuildTask;
};