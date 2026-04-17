// Copyright czm. All Rights Reserved.

#include "Download/HotUpdateIOSDownloader.h"
#include "HotUpdate.h"
#include "Core/HotUpdateSettings.h"
#include "Core/HotUpdateFileUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "TimerManager.h"

#if PLATFORM_IOS
#import <Foundation/Foundation.h>
#endif

// === FIOSDownloadTask 内部实现 ===

struct UHotUpdateIOSDownloader::FIOSDownloadTask
{
	FString Url;
	FString SavePath;
	FString TempPath;
	int64 ExpectedSize;
	int64 DownloadedSize;
	int64 ResumeOffset;
	FString ExpectedHash;
	bool bIsCompleted;
	bool bSuccess;
	int32 RetryCount;
#if PLATFORM_IOS
	NSURLSessionDownloadTask* NativeTask;
	NSData* ResumeData;
#else
	void* NativeTask;
	void* ResumeData;
#endif
};

// === FIOSSessionWrapper - Objective-C 会话包装器 ===

#if PLATFORM_IOS
@interface HotUpdateSessionDelegate : NSObject <NSURLSessionDownloadDelegate>
@property (nonatomic, assign) UHotUpdateIOSDownloader* Downloader;
@end

@implementation HotUpdateSessionDelegate

- (void)URLSession:(NSURLSession*)session
	  downloadTask:(NSURLSessionDownloadTask*)downloadTask
	  didWriteData:(int64_t)bytesWritten
	 totalBytesWritten:(int64_t)totalBytesWritten
	 totalBytesExpectedToWrite:(int64_t)totalBytesExpectedToWrite
{
}

- (void)URLSession:(NSURLSession*)session
	  downloadTask:(NSURLSessionDownloadTask*)downloadTask
didFinishDownloadingToURL:(NSURL*)location
{
	if (!self.Downloader) return;

	for (const TSharedPtr<UHotUpdateIOSDownloader::FIOSDownloadTask>& Task : self.Downloader->ActiveTasks)
	{
		if (Task->NativeTask == downloadTask)
		{
			FString TempPath = Task->TempPath;
			FString SavePath = Task->SavePath;

			NSString* SourcePath = [location path];
			NSString* DestPath = [NSString stringWithFString:TempPath];

			NSFileManager* FileManager = [NSFileManager defaultManager];
			NSError* Error = nil;

			NSString* DestDir = [DestPath stringByDeletingLastPathComponent];
			if (![FileManager fileExistsAtPath:DestDir])
			{
				[FileManager createDirectoryAtPath:DestDir withIntermediateDirectories:YES attributes:nil error:nil];
			}

			if ([FileManager moveItemAtPath:SourcePath toPath:DestPath error:&Error])
			{
				bool bHashOk = true;
				if (!Task->ExpectedHash.IsEmpty())
				{
					FString ActualHash = UHotUpdateFileUtils::CalculateFileHash(TempPath);
					if (ActualHash != Task->ExpectedHash)
					{
						UE_LOG(LogHotUpdate, Error, TEXT("Hash verification failed for %s (expected: %s, actual: %s)"),
							*SavePath, *Task->ExpectedHash, *ActualHash);
						[FileManager removeItemAtPath:DestPath error:nil];
						bHashOk = false;
					}
				}

				if (bHashOk)
				{
					NSString* FinalPath = [NSString stringWithFString:SavePath];
					if ([FileManager fileExistsAtPath:FinalPath])
					{
						[FileManager removeItemAtPath:FinalPath error:nil];
					}
					[FileManager moveItemAtPath:DestPath toPath:FinalPath error:nil];

					Task->bIsCompleted = true;
					Task->bSuccess = true;
					Task->DownloadedSize = Task->ExpectedSize;
				}
				else
				{
					Task->bIsCompleted = true;
					Task->bSuccess = false;
				}
			}
			else
			{
				UE_LOG(LogHotUpdate, Error, TEXT("Failed to move downloaded file: %s"), *SavePath);
				Task->bIsCompleted = true;
				Task->bSuccess = false;
			}

			Task->NativeTask = nil;
			break;
		}
	}
}

- (void)URLSession:(NSURLSession*)session task:(NSURLSessionTask*)task didCompleteWithError:(NSError*)error
{
	if (!self.Downloader) return;

	if (error)
	{
		for (const TSharedPtr<UHotUpdateIOSDownloader::FIOSDownloadTask>& Task : self.Downloader->ActiveTasks)
		{
			if (Task->NativeTask == (NSURLSessionDownloadTask*)task)
			{
				if (error.code != NSURLErrorCancelled)
				{
					UE_LOG(LogHotUpdate, Error, TEXT("Download task failed: %s, error: %s"),
						*Task->Url, *FString([error localizedDescription]));
				}
				Task->NativeTask = nil;
				break;
			}
		}
	}
}

- (void)URLSession:(NSURLSession*)session
	  downloadTask:(NSURLSessionDownloadTask*)downloadTask
	  didResumeAtOffset:(int64_t)fileOffset
expectedTotalBytes:(int64_t)expectedTotalBytes
{
}
@end
#endif // PLATFORM_IOS

// === FIOSSessionWrapper 实现 ===

class UHotUpdateIOSDownloader::FIOSSessionWrapper
{
public:
#if PLATFORM_IOS
	HotUpdateSessionDelegate* Delegate;
	NSURLSession* Session;

	FIOSSessionWrapper(UHotUpdateIOSDownloader* InDownloader)
	{
		Delegate = [[HotUpdateSessionDelegate alloc] init];
		Delegate.Downloader = InDownloader;

		NSString* Identifier = [NSString stringWithFormat:@"com.hotupdate.download.%@", [NSString stringWithFString:FGuid::NewGuid().ToString()]];
		NSURLSessionConfiguration* Config = [NSURLSessionConfiguration backgroundSessionWithIdentifier:Identifier];
		Config.allowsCellularAccess = YES;
		Config.discretionary = NO;
		Config.timeoutIntervalForResource = 3600.0;

		Session = [NSURLSession sessionWithConfiguration:Config delegate:Delegate delegateQueue:nil];
	}

	~FIOSSessionWrapper()
	{
		[Session invalidateAndCancel];
		Delegate.Downloader = nil;
	}

	NSURLSessionDownloadTask* CreateDownloadTask(const FString& Url)
	{
		NSString* UrlStr = [NSString stringWithFString:Url];
		NSURL* NsUrl = [NSURL URLWithString:UrlStr];
		if (!NsUrl) return nil;
		return [Session downloadTaskWithURL:NsUrl];
	}

	NSURLSessionDownloadTask* CreateDownloadTaskWithResumeData(NSData* Data)
	{
		if (!Data) return nil;
		return [Session downloadTaskWithResumeData:Data];
	}

	void Invalidate()
	{
		[Session invalidateAndCancel];
		Delegate.Downloader = nil;
	}
#else
	FIOSSessionWrapper(UHotUpdateIOSDownloader* InDownloader) {}
	~FIOSSessionWrapper() {}
	void* CreateDownloadTask(const FString& Url) { return nullptr; }
	void* CreateDownloadTaskWithResumeData(void* Data) { return nullptr; }
	void Invalidate() {}
#endif
};

// === UHotUpdateIOSDownloader ===

UHotUpdateIOSDownloader::UHotUpdateIOSDownloader()
	: MaxConcurrentDownloads(3)
	, MaxRetryCount(3)
	, RetryInterval(2.0f)
	, bEnableResume(true)
	, bIsDownloading(false)
	, bIsPaused(false)
	, LastProgressUpdateTime(0.0)
	, LastDownloadedBytes(0)
{
}

void UHotUpdateIOSDownloader::Initialize(int32 InMaxConcurrentDownloads)
{
	MaxConcurrentDownloads = InMaxConcurrentDownloads;

	UHotUpdateSettings* Settings = UHotUpdateSettings::Get();
	if (Settings)
	{
		MaxRetryCount = Settings->MaxRetryCount;
		RetryInterval = Settings->RetryInterval;
		bEnableResume = Settings->bEnableResume;
	}

#if PLATFORM_IOS
	UE_LOG(LogHotUpdate, Log, TEXT("IOSDownloader initialized: MaxConcurrent=%d, MaxRetry=%d"),
		MaxConcurrentDownloads, MaxRetryCount);
#else
	UE_LOG(LogHotUpdate, Warning, TEXT("IOSDownloader initialized on non-iOS platform! This should not happen."));
#endif
}

void UHotUpdateIOSDownloader::AddDownloadTask(const FString& Url, const FString& SavePath, int64 ExpectedSize, const FString& InExpectedHash)
{
	TSharedPtr<FIOSDownloadTask> Task = MakeShareable(new FIOSDownloadTask());
	Task->Url = Url;
	Task->SavePath = SavePath;
	Task->TempPath = SavePath + TEXT(".tmp");
	Task->ExpectedSize = ExpectedSize;
	Task->ExpectedHash = InExpectedHash;
	Task->DownloadedSize = 0;
	Task->ResumeOffset = 0;
	Task->bIsCompleted = false;
	Task->bSuccess = false;
	Task->RetryCount = 0;
	Task->NativeTask = nullptr;
	Task->ResumeData = nullptr;

	if (bEnableResume)
	{
		Task->ResumeOffset = IFileManager::Get().FileSize(*Task->TempPath);
		if (Task->ResumeOffset < 0) Task->ResumeOffset = 0;
		if (Task->ResumeOffset > 0)
		{
			UE_LOG(LogHotUpdate, Log, TEXT("Found partial download, will resume from offset %lld: %s"), Task->ResumeOffset, *SavePath);
		}
	}

	PendingTasks.Add(Task);
	UE_LOG(LogHotUpdate, Verbose, TEXT("Added download task: %s -> %s"), *Url, *SavePath);
}

void UHotUpdateIOSDownloader::StartDownload()
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

	for (const TSharedPtr<FIOSDownloadTask>& Task : PendingTasks)
	{
		CurrentProgress.TotalBytes += Task->ExpectedSize;
	}

	SessionWrapper = MakeShareable(new FIOSSessionWrapper(this));

	UE_LOG(LogHotUpdate, Log, TEXT("Starting iOS download: %d files, %lld bytes"), CurrentProgress.TotalFiles, CurrentProgress.TotalBytes);

#if PLATFORM_IOS
	while (ActiveTasks.Num() < MaxConcurrentDownloads && PendingTasks.Num() > 0)
	{
		TSharedPtr<FIOSDownloadTask> Task = PendingTasks[0];
		PendingTasks.RemoveAt(0);

		UHotUpdateFileUtils::EnsureDirectoryExists(FPaths::GetPath(Task->SavePath));

		NSURLSessionDownloadTask* NativeTask = nil;
		if (Task->ResumeData)
		{
			NativeTask = SessionWrapper->CreateDownloadTaskWithResumeData((NSData*)Task->ResumeData);
		}
		else
		{
			NativeTask = SessionWrapper->CreateDownloadTask(Task->Url);
		}

		if (NativeTask)
		{
			Task->NativeTask = NativeTask;
			[NativeTask resume];
			ActiveTasks.Add(Task);
			UE_LOG(LogHotUpdate, Log, TEXT("Started download task: %s"), *Task->Url);
		}
		else
		{
			Task->bIsCompleted = true;
			Task->bSuccess = false;
			CompletedTasks.Add(Task);
			UE_LOG(LogHotUpdate, Error, TEXT("Failed to create download task: %s"), *Task->Url);
		}
	}
#endif

	if (UWorld* World = GetWorld())
	{
		FTimerDelegate PollDelegate;
		PollDelegate.BindUObject(this, &UHotUpdateIOSDownloader::UpdateProgress);
		World->GetTimerManager().SetTimer(PollTimerHandle, PollDelegate, 0.5f, true);
	}
}

void UHotUpdateIOSDownloader::PauseDownload()
{
	bIsPaused = true;

#if PLATFORM_IOS
	for (TSharedPtr<FIOSDownloadTask>& Task : ActiveTasks)
	{
		if (Task->NativeTask)
		{
			NSURLSessionDownloadTask* TaskToCancel = (NSURLSessionDownloadTask*)Task->NativeTask;
			[TaskToCancel cancelByProducingResumeData:^(NSData* data)
			{
				Task->ResumeData = data;
			}];
			Task->NativeTask = nil;
		}
		PendingTasks.Add(Task);
	}
	ActiveTasks.Empty();
#endif

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PollTimerHandle);
	}

	UE_LOG(LogHotUpdate, Log, TEXT("iOS download paused"));
}

void UHotUpdateIOSDownloader::ResumeDownload()
{
	bIsPaused = false;

#if PLATFORM_IOS
	while (ActiveTasks.Num() < MaxConcurrentDownloads && PendingTasks.Num() > 0)
	{
		TSharedPtr<FIOSDownloadTask> Task = PendingTasks[0];
		PendingTasks.RemoveAt(0);

		NSURLSessionDownloadTask* NativeTask = nil;
		if (Task->ResumeData)
		{
			NativeTask = SessionWrapper->CreateDownloadTaskWithResumeData((NSData*)Task->ResumeData);
			Task->ResumeData = nil;
		}
		else
		{
			NativeTask = SessionWrapper->CreateDownloadTask(Task->Url);
		}

		if (NativeTask)
		{
			Task->NativeTask = NativeTask;
			[NativeTask resume];
			ActiveTasks.Add(Task);
		}
		else
		{
			Task->bIsCompleted = true;
			Task->bSuccess = false;
			CompletedTasks.Add(Task);
		}
	}
#endif

	if (UWorld* World = GetWorld())
	{
		FTimerDelegate PollDelegate;
		PollDelegate.BindUObject(this, &UHotUpdateIOSDownloader::UpdateProgress);
		World->GetTimerManager().SetTimer(PollTimerHandle, PollDelegate, 0.5f, true);
	}

	UE_LOG(LogHotUpdate, Log, TEXT("iOS download resumed"));
}

void UHotUpdateIOSDownloader::CancelDownload()
{
	bIsDownloading = false;
	bIsPaused = false;

#if PLATFORM_IOS
	for (TSharedPtr<FIOSDownloadTask>& Task : ActiveTasks)
	{
		if (Task->NativeTask)
		{
			[(NSURLSessionDownloadTask*)Task->NativeTask cancel];
			Task->NativeTask = nil;
		}
	}
#endif

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PollTimerHandle);
	}

	if (SessionWrapper.IsValid())
	{
		SessionWrapper->Invalidate();
		SessionWrapper.Reset();
	}

	PendingTasks.Empty();
	ActiveTasks.Empty();
	CompletedTasks.Empty();

	UE_LOG(LogHotUpdate, Log, TEXT("iOS download cancelled"));
}

FHotUpdateProgress UHotUpdateIOSDownloader::GetProgress() const
{
	return CurrentProgress;
}

bool UHotUpdateIOSDownloader::IsDownloading() const
{
	return bIsDownloading;
}

bool UHotUpdateIOSDownloader::IsPaused() const
{
	return bIsPaused;
}

void UHotUpdateIOSDownloader::UpdateProgress()
{
	if (!bIsDownloading || bIsPaused)
	{
		return;
	}

#if PLATFORM_IOS
	TArray<TSharedPtr<FIOSDownloadTask>> NewlyCompleted;
	for (int32 i = ActiveTasks.Num() - 1; i >= 0; i--)
	{
		TSharedPtr<FIOSDownloadTask> Task = ActiveTasks[i];
		if (Task->bIsCompleted)
		{
			NewlyCompleted.Add(Task);
		}
		else if (Task->NativeTask)
		{
			Task->DownloadedSize = (int64)((NSURLSessionDownloadTask*)Task->NativeTask).countOfBytesReceived;
		}
	}

	for (TSharedPtr<FIOSDownloadTask>& Task : NewlyCompleted)
	{
		ActiveTasks.Remove(Task);
		CompletedTasks.Add(Task);

		if (!Task->bSuccess)
		{
			Task->RetryCount++;
			if (Task->RetryCount <= MaxRetryCount)
			{
				UE_LOG(LogHotUpdate, Warning, TEXT("Download failed, retrying (%d/%d): %s"),
					Task->RetryCount, MaxRetryCount, *Task->Url);
				CompletedTasks.Remove(Task);
				Task->bIsCompleted = false;
				Task->NativeTask = nil;

				FTimerHandle RetryTimerHandle;
				FTimerDelegate RetryDelegate;
				RetryDelegate.BindLambda([this, Task]()
				{
					if (!bIsDownloading) return;
					NSURLSessionDownloadTask* NativeTask = SessionWrapper->CreateDownloadTask(Task->Url);
					if (NativeTask)
					{
						Task->NativeTask = NativeTask;
						[NativeTask resume];
						ActiveTasks.Add(Task);
					}
					else
					{
						Task->bIsCompleted = true;
						Task->bSuccess = false;
						CompletedTasks.Add(Task);
					}
				});

				if (UWorld* World = GetWorld())
				{
					World->GetTimerManager().SetTimer(RetryTimerHandle, RetryDelegate, RetryInterval, false);
				}
				continue;
			}
		}

		CurrentProgress.CurrentFileIndex = CompletedTasks.Num();
		OnFileComplete.Broadcast(Task->SavePath, Task->bSuccess);
	}

	while (ActiveTasks.Num() < MaxConcurrentDownloads && PendingTasks.Num() > 0)
	{
		TSharedPtr<FIOSDownloadTask> Task = PendingTasks[0];
		PendingTasks.RemoveAt(0);

		NSURLSessionDownloadTask* NativeTask = nil;
		if (Task->ResumeData)
		{
			NativeTask = SessionWrapper->CreateDownloadTaskWithResumeData((NSData*)Task->ResumeData);
			Task->ResumeData = nil;
		}
		else
		{
			NativeTask = SessionWrapper->CreateDownloadTask(Task->Url);
		}

		if (NativeTask)
		{
			Task->NativeTask = NativeTask;
			[NativeTask resume];
			ActiveTasks.Add(Task);
		}
		else
		{
			Task->bIsCompleted = true;
			Task->bSuccess = false;
			CompletedTasks.Add(Task);
		}
	}

	int64 TotalDownloaded = 0;
	for (const TSharedPtr<FIOSDownloadTask>& Task : ActiveTasks)
	{
		TotalDownloaded += Task->DownloadedSize;
	}
	for (const TSharedPtr<FIOSDownloadTask>& Task : CompletedTasks)
	{
		TotalDownloaded += Task->DownloadedSize;
	}
	CurrentProgress.DownloadedBytes = TotalDownloaded;

	double CurrentTime = FPlatformTime::Seconds();
	double ElapsedTime = CurrentTime - LastProgressUpdateTime;
	if (ElapsedTime > 0.5)
	{
		int64 BytesSinceLastUpdate = TotalDownloaded - LastDownloadedBytes;
		CurrentProgress.DownloadSpeed = (float)(BytesSinceLastUpdate / ElapsedTime);
		if (CurrentProgress.DownloadSpeed > 0)
		{
			int64 RemainingBytes = CurrentProgress.TotalBytes - TotalDownloaded;
			CurrentProgress.RemainingTime = (float)(RemainingBytes / CurrentProgress.DownloadSpeed);
		}
		LastProgressUpdateTime = CurrentTime;
		LastDownloadedBytes = TotalDownloaded;
	}
	OnProgress.Broadcast(CurrentProgress);

	if (PendingTasks.Num() == 0 && ActiveTasks.Num() == 0)
	{
		bIsDownloading = false;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(PollTimerHandle);
		}

		bool bAllSuccess = true;
		for (const TSharedPtr<FIOSDownloadTask>& Task : CompletedTasks)
		{
			if (!Task->bSuccess)
			{
				bAllSuccess = false;
				break;
			}
		}

		UE_LOG(LogHotUpdate, Log, TEXT("iOS download complete. Success: %s"), bAllSuccess ? TEXT("true") : TEXT("false"));
		OnComplete.Broadcast(bAllSuccess, bAllSuccess ? TEXT("") : TEXT("Some files failed to download"));

		if (SessionWrapper.IsValid())
		{
			SessionWrapper->Invalidate();
			SessionWrapper.Reset();
		}
	}
#endif // PLATFORM_IOS
}

void UHotUpdateIOSDownloader::HandleTaskCompleted(const FString& SavePath, bool bSuccess)
{
}