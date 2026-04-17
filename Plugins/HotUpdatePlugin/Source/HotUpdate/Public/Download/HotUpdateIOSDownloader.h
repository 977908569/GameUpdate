// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Download/HotUpdateDownloaderBase.h"
#include "HotUpdateIOSDownloader.generated.h"

/**
 * iOS 后台下载器
 *
 * 使用 NSURLSession 后台传输模式实现下载
 * - App 进入后台后下载继续执行
 * - 支持暂停/恢复（NSURLSessionDownloadTask 原生支持）
 * - 通过 NSURLSessionDownloadDelegate 回调进度和完成事件
 */
UCLASS()
class HOTUPDATE_API UHotUpdateIOSDownloader : public UHotUpdateDownloaderBase
{
	GENERATED_BODY()

public:
	UHotUpdateIOSDownloader();

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
	struct FIOSDownloadTask;

	/// 内部 Objective-C 会话包装器（前向声明）
	class FIOSSessionWrapper;

	/// 更新进度
	void UpdateProgress();

	/// 处理单个任务完成（由 Session Wrapper 回调）
	void HandleTaskCompleted(const FString& SavePath, bool bSuccess);

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
	TArray<TSharedPtr<FIOSDownloadTask>> PendingTasks;

	/// 正在进行的任务
	TArray<TSharedPtr<FIOSDownloadTask>> ActiveTasks;

	/// 已完成的任务
	TArray<TSharedPtr<FIOSDownloadTask>> CompletedTasks;

	/// 上次进度更新时间
	double LastProgressUpdateTime;

	/// 上次已下载字节数
	int64 LastDownloadedBytes;

	/// Objective-C 会话包装器
	TSharedPtr<FIOSSessionWrapper> SessionWrapper;

	/// 进度轮询定时器句柄
	FTimerHandle PollTimerHandle;
};