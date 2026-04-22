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
		return TotalFiles > 0 ? (float)ProcessedFiles / TotalFiles * 100.0f : 0.0f;
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
	 * @param AssetPathToDiskPath 资源包路径到磁盘路径的映射（如 "/Game/Maps/Start" -> "E:/.../Content/Maps/Start.umap"）
	 * @param OutputPath 输出路径（不含扩展名）
	 * @param Config IoStore 配置
	 * @return 构建结果
	 */
	FHotUpdateIoStoreResult BuildIoStoreContainer(
		const TMap<FString, FString>& AssetPathToDiskPath,
		const FString& OutputPath,
		const FHotUpdateIoStoreConfig& Config);

	/**
	 * 异步构建 IoStore 容器
	 */
	void BuildIoStoreContainerAsync(
		const TMap<FString, FString>& AssetPathToDiskPath,
		const FString& OutputPath,
		const FHotUpdateIoStoreConfig& Config);

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
	bool ValidateConfig(const FHotUpdateIoStoreConfig& Config, FString& OutErrorMessage);

	// 进度委托
	FOnIoStoreProgressDelegate OnProgress;

	// 完成委托
	FOnIoStoreCompleteDelegate OnComplete;

private:
	/**
	 * 使用 UnrealPak 创建 IoStore 格式容器
	 */
	bool CreateIoStoreWithUnrealPak(
		const TMap<FString, FString>& AssetPathToDiskPath,
		const FString& OutputPath,
		const FHotUpdateIoStoreConfig& Config,
		FHotUpdateIoStoreResult& OutResult);

	/**
	 * 生成加密密钥文件
	 */
	FString GenerateCryptoKeyFile(
		const FString& TempDir,
		const FHotUpdateIoStoreConfig& Config);

	/**
	 * 查找 UnrealPak 工具路径
	 */
	FString FindUnrealPakPath(FString& OutErrorMessage);

	/**
	 * 准备临时目录
	 */
	bool PrepareTempDirectory(
		const FString& TempDir,
		FString& OutErrorMessage);

	/**
	 * 从 AssetPath 获取 Pak 内部路径
	 * @param AssetPath UE 包路径（如 "/Game/Maps/Start"）
	 * @return Pak 内部路径（如 "/Game/Maps/Start.umap"）
	 */
	FString GetPakInternalPath(const FString& AssetPath, const FString& DiskPath = TEXT(""));

	/**
	 * 确定资源文件的扩展名
	 * 如果路径已有 UE 扩展名（.uasset/.umap/.uexp/.ubulk/.ubulk2），会从路径中剥离
	 * @param InOutPakPath 包路径（输入/输出），已有 UE 扩展名时会被剥离
	 * @param DiskPath 磁盘路径（用于回退获取扩展名）
	 * @return 应追加的扩展名（不含点号，空表示不追加）
	 */
	FString DetermineAssetExtension(FString& InOutPakPath, const FString& DiskPath);

	/**
	 * 将虚拟包路径映射为 Pak 内部挂载路径
	 * /Game/... -> ../../../{ProjectName}/Content/...
	 * /Engine/... -> ../../../Engine/Content/...
	 * 插件路径 -> 根据 FPackageName 解析结果映射
	 * @param PakPath 虚拟包路径（不含扩展名，以 / 开头）
	 * @return Pak 内部 Dest 路径（不含扩展名）
	 */
	FString MapToPakMountPath(const FString& PakPath);

	/**
	 * 将插件包路径映射为 Pak 内部挂载路径
	 * 通过 FPackageName 解析实际文件路径，区分引擎插件和项目插件
	 * @param PluginPakPath 插件包路径（如 /NNE/Foo）
	 * @param ProjectName 项目名称
	 * @return Pak 内部 Dest 路径
	 */
	FString MapPluginPathToPakMountPath(const FString& PluginPakPath, const FString& ProjectName);

	/**
	 * 生成响应文件
	 */
	bool GenerateResponseFile(
		const TMap<FString, FString>& AssetPathToDiskPath,
		const FString& ResponseFilePath,
		const FString& CompressionFormat,
		int32& OutValidFileCount,
		int64& OutTotalSize,
		FString& OutErrorMessage);

	/**
	 * 准备输出目录，删除已存在的输出文件
	 */
	bool PrepareOutputDirectory(
		const FString& OutputPath,
		FString& OutErrorMessage);

	/**
	 * 构建 UnrealPak 命令行参数
	 */
	FString BuildUnrealPakCommandLine(
		const FString& OutputPath,
		const FString& ResponseFilePath,
		const FString& CryptoKeyPath,
		const FHotUpdateIoStoreConfig& Config);

	/**
	 * 执行 UnrealPak 并验证结果
	 */
	bool ExecuteUnrealPak(
		const FString& UnrealPakPath,
		const FString& CmdLine,
		const FString& OutputPath,
		FHotUpdateIoStoreResult& OutResult);

	/**
	 * 清理临时目录
	 */
	void CleanupTempDirectory(const FString& TempDir);

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