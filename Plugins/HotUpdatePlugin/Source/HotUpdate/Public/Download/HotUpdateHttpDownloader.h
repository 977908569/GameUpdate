// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Download/HotUpdateDownloaderBase.h"
#include "HotUpdateHttpDownloader.generated.h"

/**
 * HTTP 下载器
 *
 * 基于 UE HTTP 模块的下载实现，支持多线程并发下载、断点续传、进度回调
 * 适用于 Windows / Mac / Linux 等桌面平台
 */
UCLASS(BlueprintType)
class HOTUPDATE_API UHotUpdateHttpDownloader : public UHotUpdateDownloaderBase
{
	GENERATED_BODY()

public:
	UHotUpdateHttpDownloader();

	/// 初始化下载器
	virtual void Initialize(int32 InMaxConcurrentDownloads = 3) override;

	/// 添加下载任务
	virtual void AddDownloadTask(const FString& Url, const FString& SavePath, int64 ExpectedSize = 0, const FString& InExpectedHash = TEXT("")) override;

	/// 批量添加下载任务
	virtual void AddDownloadTasks(const TArray<FHotUpdateFileInfo>& Files, const FString& BaseUrl, const FString& SaveDir) override;

	/// 批量添加容器下载任务（IoStore 容器）
	virtual void AddContainerDownloadTasks(const TArray<FHotUpdateContainerInfo>& Containers, const FString& BaseUrl, const FString& SaveDir) override;

	/// 开始下载
	virtual void StartDownload() override;

	/// 暂停下载（取消进行中的请求，临时文件保留供续传）
	virtual void PauseDownload() override;

	/// 恢复下载
	virtual void ResumeDownload() override;

	/// 取消下载
	virtual void CancelDownload() override;

	/// 获取当前进度
	virtual FHotUpdateProgress GetProgress() const override { return CurrentProgress; }

	/// 是否正在下载
	virtual bool IsDownloading() const override { return bIsDownloading; }

	/// 是否暂停中
	virtual bool IsPaused() const override { return bIsPaused; }

	protected:
	/// 内部任务结构（前向声明，实现在 cpp 中）
	struct FDownloadTask;

	/// 处理 HTTP 请求完成
	void HandleRequestComplete(TSharedPtr<class IHttpRequest> Request, TSharedPtr<class IHttpResponse> Response, bool bSuccess, TSharedPtr<FDownloadTask> Task);

	/// 更新进度
	void UpdateProgress();

	/// 处理下一个任务
	void ProcessNextTask();

	/// 创建临时文件路径
	FString GetTempFilePath(const FString& OriginalPath) const;

	/// 检查临时文件是否存在并获取已下载大小
	int64 GetExistingTempFileSize(const FString& TempPath) const;

	/// 追加数据到文件
	bool AppendDataToFile(const FString& FilePath, const TArray<uint8>& Data);

	/// 重试下载任务
	void RetryTask(TSharedPtr<FDownloadTask> Task);

private:
	/// 最大并发数
	int32 MaxConcurrentDownloads;

	/// 最大重试次数
	int32 MaxRetryCount;

	/// 重试间隔（秒）
	float RetryInterval;

	/// 下载超时时间（秒）
	float DownloadTimeout;

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
	TArray<TSharedPtr<FDownloadTask>> PendingTasks;

	/// 正在进行的任务
	TArray<TSharedPtr<FDownloadTask>> ActiveTasks;

	/// 已完成的任务
	TArray<TSharedPtr<FDownloadTask>> CompletedTasks;

	/// 上次进度更新时间
	double LastProgressUpdateTime;

	/// 上次已下载字节数
	int64 LastDownloadedBytes;

	/// HTTP 请求列表
	TArray<TSharedPtr<class IHttpRequest>> ActiveRequests;
};