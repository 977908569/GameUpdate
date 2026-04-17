// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Download/HotUpdateDownloaderBase.h"
#include "HotUpdateAndroidDownloader.generated.h"

/**
 * Android 原生下载器
 *
 * 使用 Android DownloadManager API 实现后台下载
 * - 下载在系统 DownloadManager 中执行，App 挂起后仍可继续
 * - 通过定时器轮询下载进度
 * - 支持暂停（取消 + 保留临时文件续传）、恢复、取消
 */
UCLASS()
class HOTUPDATE_API UHotUpdateAndroidDownloader : public UHotUpdateDownloaderBase
{
	GENERATED_BODY()

public:
	UHotUpdateAndroidDownloader();

	virtual void Initialize(int32 InMaxConcurrentDownloads = 3) override;
	virtual void AddDownloadTask(const FString& Url, const FString& SavePath, int64 ExpectedSize = 0, const FString& InExpectedHash = TEXT("")) override;
	virtual void StartDownload() override;
	virtual void PauseDownload() override;
	virtual void ResumeDownload() override;
	virtual void CancelDownload() override;
	virtual FHotUpdateProgress GetProgress() const override;
	virtual bool IsDownloading() const override;
	virtual bool IsPaused() const override;

protected:
	/// 内部任务结构
	struct FAndroidDownloadTask;

	/// 轮询下载进度
	void PollDownloadProgress();

	/// 处理单个任务完成
	void HandleTaskCompleted(TSharedPtr<FAndroidDownloadTask> Task, bool bSuccess);

	/// 通过 JNI 向 DownloadManager 提交下载请求，返回 downloadId（-1 表示失败）
	int64 EnqueueDownloadRequest(const FString& Url, const FString& SavePath, int64 ResumeOffset = 0);

	/// 通过 JNI 查询下载状态
	bool QueryDownloadStatus(int64 DownloadId, int32& OutStatus, int64& OutBytesSoFar, int64& OutBytesTotal);

	/// 通过 JNI 移除下载
	void RemoveDownload(int64 DownloadId);

private:
	/// 最大并发数
	int32 MaxConcurrentDownloads;

	/// 最大重试次数
	int32 MaxRetryCount;

	/// 重试间隔（秒）
	float RetryInterval;

	/// 是否启用断点续传
	bool bEnableResume;

	/// 当前进度
	UPROPERTY(Transient)
	FHotUpdateProgress CurrentProgress;

	/// 是否正在下载
	UPROPERTY(Transient)
	bool bIsDownloading;

	/// 是否暂停
	UPROPERTY(Transient)
	bool bIsPaused;

	/// 待下载任务队列
	TArray<TSharedPtr<FAndroidDownloadTask>> PendingTasks;

	/// 正在进行的任务
	TArray<TSharedPtr<FAndroidDownloadTask>> ActiveTasks;

	/// 已完成的任务
	TArray<TSharedPtr<FAndroidDownloadTask>> CompletedTasks;

	/// 上次进度更新时间
	double LastProgressUpdateTime;

	/// 上次已下载字节数
	int64 LastDownloadedBytes;

	/// 进度轮询定时器句柄
	FTimerHandle PollTimerHandle;
};