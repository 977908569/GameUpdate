// Copyright czm. All Rights Reserved.

#include "Download/HotUpdateHttpDownloader.h"
#include "HotUpdate.h"
#include "Core/HotUpdateSettings.h"
#include "Core/HotUpdateFileUtils.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"

// === FDownloadTask 内部实现 ===
// 使用 TSharedPtr 管理生命周期，因为任务在 PendingTasks/ActiveTasks/CompletedTasks 之间流转，
// 且可能被 HTTP 回调持有，共享所有权是必要的。

struct UHotUpdateHttpDownloader::FDownloadTask
{
	FString Url;
	FString SavePath;
	FString TempPath;
	int64 ExpectedSize;
	int64 DownloadedSize;
	int64 ResumeOffset;      // 断点续传起始位置
	int64 BytesWrittenToDisk; // 已写入磁盘的字节数（绝对偏移）
	FString ExpectedHash;    // 期望的文件 Hash（SHA1），用于下载后校验
	bool bIsCompleted;
	bool bSuccess;
	bool bServerIgnoredRange; // 服务器忽略 Range 请求，返回了完整内容
	int32 RetryCount;        // 当前重试次数
	EHotUpdateError ErrorType; // 错误类型
};

// === UHotUpdateHttpDownloader ===

UHotUpdateHttpDownloader::UHotUpdateHttpDownloader()
	: MaxConcurrentDownloads(3)
	, MaxRetryCount(3)
	, RetryInterval(2.0f)
	, DownloadTimeout(300.0f)
	, bEnableResume(true)
	, bIsDownloading(false)
	, bIsPaused(false)
	, LastProgressUpdateTime(0.0)
	, LastDownloadedBytes(0)
{
}

void UHotUpdateHttpDownloader::Initialize(int32 InMaxConcurrentDownloads)
{
	MaxConcurrentDownloads = InMaxConcurrentDownloads;

	// 从 Settings 读取重试配置和超时设置
	UHotUpdateSettings* Settings = UHotUpdateSettings::Get();
	if (Settings)
	{
		MaxRetryCount = Settings->MaxRetryCount;
		RetryInterval = Settings->RetryInterval;
		DownloadTimeout = Settings->DownloadTimeout;
		bEnableResume = Settings->bEnableResume;
	}

	UE_LOG(LogHotUpdate, Log, TEXT("HttpDownloader initialized: MaxConcurrent=%d, MaxRetry=%d"),
		MaxConcurrentDownloads, MaxRetryCount);
}

void UHotUpdateHttpDownloader::AddDownloadTask(const FString& Url, const FString& SavePath, int64 ExpectedSize, const FString& InExpectedHash)
{
	TSharedPtr<FDownloadTask> Task = MakeShareable(new FDownloadTask());
	Task->Url = Url;
	Task->SavePath = SavePath;
	Task->TempPath = GetTempFilePath(SavePath);
	Task->ExpectedSize = ExpectedSize;
	Task->ExpectedHash = InExpectedHash;
	Task->DownloadedSize = 0;
	Task->ResumeOffset = 0;
	Task->BytesWrittenToDisk = 0;
	Task->bIsCompleted = false;
	Task->bSuccess = false;
	Task->bServerIgnoredRange = false;
	Task->RetryCount = 0;

	// 检查是否存在未完成的临时文件（断点续传）
	if (bEnableResume)
	{
		Task->ResumeOffset = GetExistingTempFileSize(Task->TempPath);
		Task->BytesWrittenToDisk = Task->ResumeOffset;
		if (Task->ResumeOffset > 0)
		{
			UE_LOG(LogHotUpdate, Log, TEXT("Found partial download, will resume from offset %lld: %s"), Task->ResumeOffset, *SavePath);
		}
	}

	PendingTasks.Add(Task);
	UE_LOG(LogHotUpdate, Verbose, TEXT("Added download task: %s -> %s"), *Url, *SavePath);
}

void UHotUpdateHttpDownloader::StartDownload()
{
	if (bIsDownloading)
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("Download already in progress"));
		return;
	}

	bIsDownloading = true;
	bIsPaused = false;
	LastProgressUpdateTime = FPlatformTime::Seconds();
	LastDownloadedBytes = 0;

	CurrentProgress = FHotUpdateProgress();
	CurrentProgress.TotalFiles = PendingTasks.Num();

	// 计算总大小
	for (const TSharedPtr<FDownloadTask>& Task : PendingTasks)
	{
		CurrentProgress.TotalBytes += Task->ExpectedSize;
	}

	UE_LOG(LogHotUpdate, Log, TEXT("Starting download: %d files, %lld bytes"), CurrentProgress.TotalFiles, CurrentProgress.TotalBytes);

	ProcessNextTask();
}

void UHotUpdateHttpDownloader::PauseDownload()
{
	bIsPaused = true;

	// 取消进行中的请求，临时文件保留在磁盘供续传
	for (TSharedPtr<IHttpRequest>& Request : ActiveRequests)
	{
		if (Request.IsValid())
		{
			Request->CancelRequest();
		}
	}

	UE_LOG(LogHotUpdate, Log, TEXT("Download paused"));
}

void UHotUpdateHttpDownloader::ResumeDownload()
{
	bIsPaused = false;

	// 刷新所有待下载任务的断点续传偏移
	if (bEnableResume)
	{
		for (TSharedPtr<FDownloadTask>& Task : PendingTasks)
		{
			Task->ResumeOffset = GetExistingTempFileSize(Task->TempPath);
			Task->DownloadedSize = Task->ResumeOffset;
			Task->BytesWrittenToDisk = Task->ResumeOffset;
		}
	}

	ProcessNextTask();
	UE_LOG(LogHotUpdate, Log, TEXT("Download resumed"));
}

void UHotUpdateHttpDownloader::CancelDownload(bool bDeleteTempFiles)
{
	bIsDownloading = false;
	bIsPaused = false;

	// 取消所有活跃请求
	for (TSharedPtr<IHttpRequest>& Request : ActiveRequests)
	{
		if (Request.IsValid())
		{
			Request->CancelRequest();
		}
	}
	ActiveRequests.Empty();

	// 可选：删除临时文件
	if (bDeleteTempFiles)
	{
		auto DeleteTempFiles = [](const TArray<TSharedPtr<FDownloadTask>>& Tasks)
		{
			for (const TSharedPtr<FDownloadTask>& Task : Tasks)
			{
				if (Task.IsValid() && !Task->TempPath.IsEmpty())
				{
					IFileManager::Get().Delete(*Task->TempPath);
				}
			}
		};
		DeleteTempFiles(PendingTasks);
		DeleteTempFiles(ActiveTasks);
		DeleteTempFiles(CompletedTasks);
		UE_LOG(LogHotUpdate, Log, TEXT("Download cancelled, temp files deleted"));
	}
	else
	{
		UE_LOG(LogHotUpdate, Log, TEXT("Download cancelled, temp files preserved for resume"));
	}

	// 清理任务
	PendingTasks.Empty();
	ActiveTasks.Empty();
	CompletedTasks.Empty();
}

void UHotUpdateHttpDownloader::ProcessNextTask()
{
	if (!bIsDownloading || bIsPaused)
	{
		return;
	}

	FScopeLock Lock(&TaskQueueLock);

	// 检查是否所有任务完成
	if (PendingTasks.Num() == 0 && ActiveTasks.Num() == 0)
	{
		bIsDownloading = false;

		// 检查是否所有任务成功
		bool bAllSuccess = true;
		for (const TSharedPtr<FDownloadTask>& Task : CompletedTasks)
		{
			if (!Task->bSuccess)
			{
				bAllSuccess = false;
				break;
			}
		}

		UE_LOG(LogHotUpdate, Log, TEXT("Download complete. Success: %s"), bAllSuccess ? TEXT("true") : TEXT("false"));
		OnComplete.Broadcast(bAllSuccess, bAllSuccess ? TEXT("") : TEXT("Some files failed to download"));
		return;
	}

	// 启动新任务（不超过最大并发数）
	while (ActiveRequests.Num() < MaxConcurrentDownloads && PendingTasks.Num() > 0)
	{
		TSharedPtr<FDownloadTask> Task = PendingTasks[0];
		PendingTasks.RemoveAt(0);
		ActiveTasks.Add(Task);

		// 确保目录存在
		UHotUpdateFileUtils::EnsureDirectoryExists(FPaths::GetPath(Task->SavePath));

		// 创建 HTTP 请求
		TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
		Request->SetURL(Task->Url);
		Request->SetVerb(TEXT("GET"));
		Request->SetTimeout(DownloadTimeout);

		// 断点续传：检查是否有部分下载的文件
		if (Task->ResumeOffset > 0)
		{
			Request->AppendToHeader(TEXT("Range"), *FString::Printf(TEXT("bytes=%lld-"), Task->ResumeOffset));
			Task->DownloadedSize = Task->ResumeOffset;
			UE_LOG(LogHotUpdate, Log, TEXT("Resuming download from byte %lld: %s"), Task->ResumeOffset, *Task->Url);
		}

		Request->OnProcessRequestComplete().BindUObject(this, &UHotUpdateHttpDownloader::HandleRequestComplete, Task);
		Request->OnRequestProgress64().BindUObject(this, &UHotUpdateHttpDownloader::HandleRequestProgress, Task);

		ActiveRequests.Add(Request);

		UE_LOG(LogHotUpdate, Verbose, TEXT("Starting download: %s"), *Task->Url);
		Request->ProcessRequest();
	}
}

void UHotUpdateHttpDownloader::HandleRequestComplete(TSharedPtr<IHttpRequest> Request, TSharedPtr<IHttpResponse> Response, bool bSuccess, TSharedPtr<FDownloadTask> Task)
{
	FScopeLock Lock(&TaskQueueLock);

	// 从活跃请求列表移除
	for (int32 i = ActiveRequests.Num() - 1; i >= 0; i--)
	{
		if (ActiveRequests[i] == Request)
		{
			ActiveRequests.RemoveAt(i);
			break;
		}
	}

	// 暂停导致的取消 → 移回待下载队列
	if (!bSuccess && bIsPaused)
	{
		ActiveTasks.Remove(Task);
		PendingTasks.Add(Task);
		return;
	}

	// 检查响应状态
	if (!bSuccess || !Response.IsValid())
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("HTTP request failed for: %s"), *Task->Url);
		bool bHandled = false;
		HandleTaskFailure(Task, bHandled);
		if (bHandled) return;
	}

	int32 ResponseCode = Response->GetResponseCode();
	bool bIsPartialContent = (ResponseCode == 206);
	bool bIsFullContent = (ResponseCode >= 200 && ResponseCode < 300 && ResponseCode != 206);

	// 检测服务器是否忽略了 Range 请求
	if (bIsFullContent && Task->ResumeOffset > 0)
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("Server ignored Range header, returned full content: %s"), *Task->Url);
		Task->bServerIgnoredRange = true;
	}

	if (!bIsPartialContent && !bIsFullContent)
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("HTTP request returned %d for: %s"), ResponseCode, *Task->Url);
		bool bHandled = false;
		HandleTaskFailure(Task, bHandled);
		if (bHandled) return;
	}

	// 保存响应内容
	int64 DataSize = 0;
	if (!SaveResponseToFile(Task, Response, bIsPartialContent, DataSize))
	{
		bool bHandled = false;
		HandleTaskFailure(Task, bHandled);
		if (bHandled) return;
	}

	// 校验 Hash + 重命名
	if (!VerifyAndFinalizeTask(Task, DataSize))
	{
		bool bHandled = false;
		HandleTaskFailure(Task, bHandled);
		if (bHandled) return;
	}

	// 移到完成列表、广播、更新进度
	ActiveTasks.Remove(Task);
	CompletedTasks.Add(Task);
	CurrentProgress.CurrentFileIndex = CompletedTasks.Num();
	OnFileComplete.Broadcast(Task->SavePath, Task->bSuccess, Task->ErrorType);
	UpdateProgress();
	ProcessNextTask();
}

bool UHotUpdateHttpDownloader::SaveResponseToFile(TSharedPtr<FDownloadTask> Task, TSharedPtr<IHttpResponse> Response, bool bIsPartialContent, int64& OutDataSize)
{
	if (!Response.IsValid())
	{
		UE_LOG(LogHotUpdate, Error, TEXT("SaveResponseToFile: Invalid response for %s"), *Task->SavePath);
		return false;
	}

	const TArray<uint8>& Content = Response->GetContent();
	OutDataSize = Content.Num();

	bool bSaveSuccess = false;

	// 增量写入模式：数据已在 HandleRequestProgress 中分块写入磁盘
	if (Task->BytesWrittenToDisk > 0)
	{
		// 写入剩余未写入的部分
		const int64 AlreadyWritten = Task->BytesWrittenToDisk - Task->ResumeOffset;
		const int64 Remaining = static_cast<int64>(Content.Num()) - AlreadyWritten;
		if (Remaining > 0)
		{
			bSaveSuccess = AppendDataToFile(Task->TempPath,
				TArray<uint8>(Content.GetData() + AlreadyWritten, Remaining));
		}
		else
		{
			bSaveSuccess = true;
		}
		if (bSaveSuccess)
		{
			UE_LOG(LogHotUpdate, Log, TEXT("Incremental download completed: %s (total: %lld bytes)"),
				*Task->SavePath, Task->BytesWrittenToDisk + FMath::Max(Remaining, 0LL));
		}
	}
	else if (bIsPartialContent && Task->ResumeOffset > 0)
	{
		// 传统续传：服务器返回 206，追加到临时文件
		bSaveSuccess = AppendDataToFile(Task->TempPath, Content);
		if (bSaveSuccess)
		{
			UE_LOG(LogHotUpdate, Log, TEXT("Resumed download completed: %s (total: %lld bytes)"), *Task->SavePath, Task->ResumeOffset + OutDataSize);
		}
	}
	else
	{
		// 全新下载或服务器返回 200（忽略 Range 请求），覆盖写入
		bSaveSuccess = FFileHelper::SaveArrayToFile(Content, *Task->TempPath);
		if (bSaveSuccess)
		{
			UE_LOG(LogHotUpdate, Verbose, TEXT("Downloaded: %s (%lld bytes)"), *Task->SavePath, OutDataSize);
		}
	}

	if (!bSaveSuccess)
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Failed to save file: %s"), *Task->TempPath);
	}
	return bSaveSuccess;
}

bool UHotUpdateHttpDownloader::VerifyAndFinalizeTask(TSharedPtr<FDownloadTask> Task, int64 DataSize)
{
	// Hash 校验
	if (!Task->ExpectedHash.IsEmpty())
	{
		FString ActualHash = UHotUpdateFileUtils::CalculateFileHash(Task->TempPath);
		if (ActualHash != Task->ExpectedHash)
		{
			UE_LOG(LogHotUpdate, Error, TEXT("Hash verification failed for %s (expected: %s, actual: %s)"),
				*Task->SavePath, *Task->ExpectedHash, *ActualHash);
			IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
			PF.DeleteFile(*Task->TempPath);
			Task->ErrorType = EHotUpdateError::VerificationFailed;
			return false;
		}
		UE_LOG(LogHotUpdate, Verbose, TEXT("Hash verified: %s"), *Task->SavePath);
	}

	// 重命名临时文件为最终文件
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (PlatformFile.FileExists(*Task->SavePath))
	{
		PlatformFile.DeleteFile(*Task->SavePath);
	}

	if (!PlatformFile.MoveFile(*Task->SavePath, *Task->TempPath))
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Failed to move temp file to final location: %s"), *Task->SavePath);
		return false;
	}

	// 根据服务器是否忽略 Range 请求来计算最终已下载大小
	if (Task->bServerIgnoredRange)
	{
		Task->DownloadedSize = DataSize;
	}
	else
	{
		Task->DownloadedSize = Task->ResumeOffset + DataSize;
	}
	// 防御性 clamp
	if (Task->ExpectedSize > 0)
	{
		Task->DownloadedSize = FMath::Min(Task->DownloadedSize, Task->ExpectedSize);
	}
	Task->bIsCompleted = true;
	Task->bSuccess = true;
	return true;
}

void UHotUpdateHttpDownloader::HandleTaskFailure(TSharedPtr<FDownloadTask> Task, bool& bOutHandled)
{
	bOutHandled = false;
	Task->RetryCount++;

	if (Task->RetryCount <= MaxRetryCount)
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("Download failed, retrying (%d/%d) after %.1fs: %s"),
			Task->RetryCount, MaxRetryCount, RetryInterval, *Task->Url);

		ActiveTasks.Remove(Task);

		FTimerDelegate RetryDelegate;
		RetryDelegate.BindUObject(this, &UHotUpdateHttpDownloader::RetryTask, Task);

		if (UWorld* World = GetWorld())
		{
			FTimerHandle RetryTimerHandle;
			World->GetTimerManager().SetTimer(RetryTimerHandle, RetryDelegate, RetryInterval, false);
		}
		else
		{
			UE_LOG(LogHotUpdate, Error, TEXT("Cannot schedule retry: no World context. Marking as failed: %s"), *Task->Url);
			Task->bIsCompleted = true;
			Task->bSuccess = false;
			Task->ErrorType = EHotUpdateError::DownloadFailed;
			CompletedTasks.Add(Task);
			OnFileComplete.Broadcast(Task->SavePath, false, Task->ErrorType);
		}

		bOutHandled = true;
		return;
	}

	// 超过重试次数
	Task->bIsCompleted = true;
	Task->bSuccess = false;
	UE_LOG(LogHotUpdate, Error, TEXT("Download failed after %d retries: %s"), MaxRetryCount, *Task->Url);
}

void UHotUpdateHttpDownloader::HandleRequestProgress(FHttpRequestPtr Request, uint64 BytesSent, uint64 BytesReceived, TSharedPtr<FDownloadTask> Task)
{
	if (!Task.IsValid() || Task->bIsCompleted)
	{
		return;
	}

	// 根据服务器是否忽略 Range 请求来计算实际已下载大小
	if (Task->bServerIgnoredRange)
	{
		// 服务器返回了完整内容，BytesReceived 已包含全部数据
		Task->DownloadedSize = static_cast<int64>(BytesReceived);
	}
	else
	{
		Task->DownloadedSize = Task->ResumeOffset + static_cast<int64>(BytesReceived);
	}
	// 防御性 clamp，防止进度超过预期大小
	if (Task->ExpectedSize > 0)
	{
		Task->DownloadedSize = FMath::Min(Task->DownloadedSize, Task->ExpectedSize);
	}

	// 增量写入磁盘：每累积 1MB 数据写入一次，确保暂停时数据已持久化
	constexpr int64 WriteChunkSize = 1024 * 1024; // 1MB
	const int64 AbsoluteReceived = Task->DownloadedSize;
	const int64 PendingBytes = AbsoluteReceived - Task->BytesWrittenToDisk;

	if (PendingBytes >= WriteChunkSize)
	{
		TSharedPtr<IHttpResponse> Response = Request->GetResponse();
		if (Response.IsValid())
		{
			const TArray<uint8>& Content = Response->GetContent();
			const int64 DataOffset = Task->BytesWrittenToDisk - Task->ResumeOffset;
			const int64 DataToWrite = static_cast<int64>(Content.Num()) - DataOffset;

			if (DataToWrite > 0)
			{
				// 写入磁盘：首次写入创建文件，后续追加
				bool bWriteSuccess = false;
				if (Task->BytesWrittenToDisk == 0)
				{
					bWriteSuccess = FFileHelper::SaveArrayToFile(
						TArray<uint8>(Content.GetData() + DataOffset, DataToWrite),
						*Task->TempPath);
				}
				else
				{
					bWriteSuccess = AppendDataToFile(Task->TempPath,
						TArray<uint8>(Content.GetData() + DataOffset, DataToWrite));
				}

				if (bWriteSuccess)
				{
					Task->BytesWrittenToDisk = AbsoluteReceived;
				}
			}
		}
	}

	UpdateProgress();
}

void UHotUpdateHttpDownloader::UpdateProgress()
{
	// 计算已下载字节数（包含 PendingTasks 中有断点续传数据的任务）
	int64 TotalDownloaded = 0;
	for (const TSharedPtr<FDownloadTask>& Task : PendingTasks)
	{
		TotalDownloaded += Task->DownloadedSize;
	}
	for (const TSharedPtr<FDownloadTask>& Task : ActiveTasks)
	{
		TotalDownloaded += Task->DownloadedSize;
	}
	for (const TSharedPtr<FDownloadTask>& Task : CompletedTasks)
	{
		TotalDownloaded += Task->DownloadedSize;
	}

	CurrentProgress.DownloadedBytes = TotalDownloaded;

	// 计算速度和剩余时间
	UpdateProgressCalculation(TotalDownloaded, CurrentProgress, LastProgressUpdateTime, LastDownloadedBytes);

	OnProgress.Broadcast(CurrentProgress);
}

void UHotUpdateHttpDownloader::RetryTask(TSharedPtr<FDownloadTask> Task)
{
	if (!bIsDownloading)
	{
		return;
	}

	// 重置断点续传偏移（保留已下载的临时文件用于续传）
	Task->ResumeOffset = GetExistingTempFileSize(Task->TempPath);
	Task->DownloadedSize = Task->ResumeOffset;
	Task->BytesWrittenToDisk = Task->ResumeOffset;

	PendingTasks.Add(Task);
	ProcessNextTask();
}

FString UHotUpdateHttpDownloader::GetTempFilePath(const FString& OriginalPath) const
{
	return OriginalPath + TEXT(".tmp");
}

int64 UHotUpdateHttpDownloader::GetExistingTempFileSize(const FString& TempPath) const
{
	int64 Size = IFileManager::Get().FileSize(*TempPath);
	return Size > 0 ? Size : 0;
}

bool UHotUpdateHttpDownloader::AppendDataToFile(const FString& FilePath, const TArray<uint8>& Data)
{
	TUniquePtr<FArchive> FileWriter(IFileManager::Get().CreateFileWriter(*FilePath, FILEWRITE_Append));
	if (!FileWriter)
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Failed to open file for appending: %s"), *FilePath);
		return false;
	}

	FileWriter->Serialize(const_cast<uint8*>(Data.GetData()), Data.Num());

	if (FileWriter->IsError())
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Failed to write data to file: %s"), *FilePath);
		return false;
	}

	return true;
}