// Copyright czm. All Rights Reserved.

#include "Download/HotUpdateAndroidDownloader.h"
#include "HotUpdate.h"
#include "Core/HotUpdateSettings.h"
#include "Core/HotUpdateFileUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "TimerManager.h"

#if PLATFORM_ANDROID
#include "Android/AndroidJNI.h"
#include "Android/AndroidApplication.h"
#include "Android/JavaEnv.h"
#endif

// === FAndroidDownloadTask 内部实现 ===

struct UHotUpdateAndroidDownloader::FAndroidDownloadTask
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
	int64 DownloadManagerId;  // Android DownloadManager 返回的下载 ID
};

// === UHotUpdateAndroidDownloader ===

UHotUpdateAndroidDownloader::UHotUpdateAndroidDownloader()
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

void UHotUpdateAndroidDownloader::Initialize(int32 InMaxConcurrentDownloads)
{
	MaxConcurrentDownloads = InMaxConcurrentDownloads;

	UHotUpdateSettings* Settings = UHotUpdateSettings::Get();
	if (Settings)
	{
		MaxRetryCount = Settings->MaxRetryCount;
		RetryInterval = Settings->RetryInterval;
		bEnableResume = Settings->bEnableResume;
	}

#if PLATFORM_ANDROID
	UE_LOG(LogHotUpdate, Log, TEXT("AndroidDownloader initialized: MaxConcurrent=%d, MaxRetry=%d"),
		MaxConcurrentDownloads, MaxRetryCount);
#else
	UE_LOG(LogHotUpdate, Warning, TEXT("AndroidDownloader initialized on non-Android platform! This should not happen."));
#endif
}

void UHotUpdateAndroidDownloader::AddDownloadTask(const FString& Url, const FString& SavePath, int64 ExpectedSize, const FString& InExpectedHash)
{
	TSharedPtr<FAndroidDownloadTask> Task = MakeShareable(new FAndroidDownloadTask());
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
	Task->DownloadManagerId = -1;

	// 检查临时文件用于断点续传
	if (bEnableResume)
	{
		Task->ResumeOffset = IFileManager::Get().FileSize(*Task->TempPath);
		if (Task->ResumeOffset < 0)
		{
			Task->ResumeOffset = 0;
		}
		if (Task->ResumeOffset > 0)
		{
			UE_LOG(LogHotUpdate, Log, TEXT("Found partial download, will resume from offset %lld: %s"), Task->ResumeOffset, *SavePath);
		}
	}

	PendingTasks.Add(Task);
	UE_LOG(LogHotUpdate, Verbose, TEXT("Added download task: %s -> %s"), *Url, *SavePath);
}

void UHotUpdateAndroidDownloader::StartDownload()
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

	for (const TSharedPtr<FAndroidDownloadTask>& Task : PendingTasks)
	{
		CurrentProgress.TotalBytes += Task->ExpectedSize;
	}

	UE_LOG(LogHotUpdate, Log, TEXT("Starting Android download: %d files, %lld bytes"), CurrentProgress.TotalFiles, CurrentProgress.TotalBytes);

	// 提交任务到 DownloadManager
	while (ActiveTasks.Num() < MaxConcurrentDownloads && PendingTasks.Num() > 0)
	{
		TSharedPtr<FAndroidDownloadTask> Task = PendingTasks[0];
		PendingTasks.RemoveAt(0);

		UHotUpdateFileUtils::EnsureDirectoryExists(FPaths::GetPath(Task->SavePath));

		int64 DownloadId = EnqueueDownloadRequest(Task->Url, Task->SavePath, Task->ResumeOffset);
		if (DownloadId >= 0)
		{
			Task->DownloadManagerId = DownloadId;
			Task->DownloadedSize = Task->ResumeOffset;
			ActiveTasks.Add(Task);
			UE_LOG(LogHotUpdate, Log, TEXT("Enqueued download: %s (id=%lld)"), *Task->Url, DownloadId);
		}
		else
		{
			Task->bIsCompleted = true;
			Task->bSuccess = false;
			CompletedTasks.Add(Task);
			UE_LOG(LogHotUpdate, Error, TEXT("Failed to enqueue download: %s"), *Task->Url);
		}
	}

	// 启动轮询定时器
	if (UWorld* World = GetWorld())
	{
		FTimerDelegate PollDelegate;
		PollDelegate.BindUObject(this, &UHotUpdateAndroidDownloader::PollDownloadProgress);
		World->GetTimerManager().SetTimer(PollTimerHandle, PollDelegate, 0.5f, true);
	}
}

void UHotUpdateAndroidDownloader::PauseDownload()
{
	bIsPaused = true;

	// 移除所有活跃的 DownloadManager 请求，临时文件保留供续传
	for (TSharedPtr<FAndroidDownloadTask>& Task : ActiveTasks)
	{
		if (Task->DownloadManagerId >= 0)
		{
			RemoveDownload(Task->DownloadManagerId);
			Task->DownloadManagerId = -1;
		}
		PendingTasks.Add(Task);
	}
	ActiveTasks.Empty();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PollTimerHandle);
	}

	UE_LOG(LogHotUpdate, Log, TEXT("Android download paused"));
}

void UHotUpdateAndroidDownloader::ResumeDownload()
{
	bIsPaused = false;

	// 刷新断点续传偏移
	if (bEnableResume)
	{
		for (TSharedPtr<FAndroidDownloadTask>& Task : PendingTasks)
		{
			Task->ResumeOffset = IFileManager::Get().FileSize(*Task->TempPath);
			if (Task->ResumeOffset < 0) Task->ResumeOffset = 0;
			Task->DownloadedSize = Task->ResumeOffset;
		}
	}

	// 重新提交待下载任务
	while (ActiveTasks.Num() < MaxConcurrentDownloads && PendingTasks.Num() > 0)
	{
		TSharedPtr<FAndroidDownloadTask> Task = PendingTasks[0];
		PendingTasks.RemoveAt(0);

		int64 DownloadId = EnqueueDownloadRequest(Task->Url, Task->SavePath, Task->ResumeOffset);
		if (DownloadId >= 0)
		{
			Task->DownloadManagerId = DownloadId;
			ActiveTasks.Add(Task);
		}
		else
		{
			Task->bIsCompleted = true;
			Task->bSuccess = false;
			CompletedTasks.Add(Task);
		}
	}

	if (UWorld* World = GetWorld())
	{
		FTimerDelegate PollDelegate;
		PollDelegate.BindUObject(this, &UHotUpdateAndroidDownloader::PollDownloadProgress);
		World->GetTimerManager().SetTimer(PollTimerHandle, PollDelegate, 0.5f, true);
	}

	UE_LOG(LogHotUpdate, Log, TEXT("Android download resumed"));
}

void UHotUpdateAndroidDownloader::CancelDownload()
{
	bIsDownloading = false;
	bIsPaused = false;

	for (TSharedPtr<FAndroidDownloadTask>& Task : ActiveTasks)
	{
		if (Task->DownloadManagerId >= 0)
		{
			RemoveDownload(Task->DownloadManagerId);
		}
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PollTimerHandle);
	}

	PendingTasks.Empty();
	ActiveTasks.Empty();
	CompletedTasks.Empty();

	UE_LOG(LogHotUpdate, Log, TEXT("Android download cancelled"));
}

FHotUpdateProgress UHotUpdateAndroidDownloader::GetProgress() const
{
	return CurrentProgress;
}

bool UHotUpdateAndroidDownloader::IsDownloading() const
{
	return bIsDownloading;
}

bool UHotUpdateAndroidDownloader::IsPaused() const
{
	return bIsPaused;
}

void UHotUpdateAndroidDownloader::PollDownloadProgress()
{
	if (!bIsDownloading || bIsPaused)
	{
		return;
	}

#if PLATFORM_ANDROID
	TArray<TSharedPtr<FAndroidDownloadTask>> NewlyCompleted;

	for (int32 i = ActiveTasks.Num() - 1; i >= 0; i--)
	{
		TSharedPtr<FAndroidDownloadTask> Task = ActiveTasks[i];
		if (Task->DownloadManagerId < 0)
		{
			continue;
		}

		int32 Status = 0;
		int64 BytesSoFar = 0;
		int64 BytesTotal = 0;

		if (!QueryDownloadStatus(Task->DownloadManagerId, Status, BytesSoFar, BytesTotal))
		{
			continue;
		}

		// Android DownloadManager 状态码
		const int32 STATUS_SUCCESSFUL = 8;
		const int32 STATUS_FAILED = 16;

		Task->DownloadedSize = Task->ResumeOffset + BytesSoFar;

		if (Status == STATUS_SUCCESSFUL)
		{
			bool bHashOk = true;
			if (!Task->ExpectedHash.IsEmpty())
			{
				FString DownloadedFilePath = Task->SavePath;
				FString ActualHash = UHotUpdateFileUtils::CalculateFileHash(DownloadedFilePath);
				if (ActualHash != Task->ExpectedHash)
				{
					UE_LOG(LogHotUpdate, Error, TEXT("Hash verification failed for %s (expected: %s, actual: %s)"),
						*Task->SavePath, *Task->ExpectedHash, *ActualHash);
					IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
					PF.DeleteFile(*Task->TempPath);
					bHashOk = false;
				}
			}

			Task->bIsCompleted = true;
			Task->bSuccess = bHashOk;
			Task->DownloadedSize = Task->ResumeOffset + BytesTotal;
			NewlyCompleted.Add(Task);
		}
		else if (Status == STATUS_FAILED)
		{
			Task->RetryCount++;
			if (Task->RetryCount <= MaxRetryCount)
			{
				UE_LOG(LogHotUpdate, Warning, TEXT("Download failed, retrying (%d/%d): %s"),
					Task->RetryCount, MaxRetryCount, *Task->Url);
				RemoveDownload(Task->DownloadManagerId);
				Task->DownloadManagerId = -1;
				Task->ResumeOffset = IFileManager::Get().FileSize(*Task->TempPath);
				if (Task->ResumeOffset < 0) Task->ResumeOffset = 0;

				FTimerHandle RetryTimerHandle;
				FTimerDelegate RetryDelegate;
				RetryDelegate.BindLambda([this, Task]()
				{
					if (!bIsDownloading) return;
					int64 DownloadId = EnqueueDownloadRequest(Task->Url, Task->SavePath, Task->ResumeOffset);
					if (DownloadId >= 0)
					{
						Task->DownloadManagerId = DownloadId;
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
				ActiveTasks.RemoveAt(i);
				continue;
			}

			Task->bIsCompleted = true;
			Task->bSuccess = false;
			NewlyCompleted.Add(Task);
			UE_LOG(LogHotUpdate, Error, TEXT("Download failed after %d retries: %s"), MaxRetryCount, *Task->Url);
		}
	}

	// 移动完成的任务
	for (TSharedPtr<FAndroidDownloadTask>& Task : NewlyCompleted)
	{
		ActiveTasks.Remove(Task);
		CompletedTasks.Add(Task);
		RemoveDownload(Task->DownloadManagerId);
		Task->DownloadManagerId = -1;

		CurrentProgress.CurrentFileIndex = CompletedTasks.Num();
		OnFileComplete.Broadcast(Task->SavePath, Task->bSuccess);
	}

	// 启动新的待下载任务
	while (ActiveTasks.Num() < MaxConcurrentDownloads && PendingTasks.Num() > 0)
	{
		TSharedPtr<FAndroidDownloadTask> Task = PendingTasks[0];
		PendingTasks.RemoveAt(0);

		int64 DownloadId = EnqueueDownloadRequest(Task->Url, Task->SavePath, Task->ResumeOffset);
		if (DownloadId >= 0)
		{
			Task->DownloadManagerId = DownloadId;
			ActiveTasks.Add(Task);
		}
		else
		{
			Task->bIsCompleted = true;
			Task->bSuccess = false;
			CompletedTasks.Add(Task);
		}
	}

	// 更新进度
	int64 TotalDownloaded = 0;
	for (const TSharedPtr<FAndroidDownloadTask>& Task : ActiveTasks)
	{
		TotalDownloaded += Task->DownloadedSize;
	}
	for (const TSharedPtr<FAndroidDownloadTask>& Task : CompletedTasks)
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

	// 所有任务完成
	if (PendingTasks.Num() == 0 && ActiveTasks.Num() == 0)
	{
		bIsDownloading = false;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(PollTimerHandle);
		}

		bool bAllSuccess = true;
		for (const TSharedPtr<FAndroidDownloadTask>& Task : CompletedTasks)
		{
			if (!Task->bSuccess)
			{
				bAllSuccess = false;
				break;
			}
		}

		UE_LOG(LogHotUpdate, Log, TEXT("Android download complete. Success: %s"), bAllSuccess ? TEXT("true") : TEXT("false"));
		OnComplete.Broadcast(bAllSuccess, bAllSuccess ? TEXT("") : TEXT("Some files failed to download"));
	}
#endif // PLATFORM_ANDROID
}

void UHotUpdateAndroidDownloader::HandleTaskCompleted(TSharedPtr<FAndroidDownloadTask> Task, bool bSuccess)
{
}

int64 UHotUpdateAndroidDownloader::EnqueueDownloadRequest(const FString& Url, const FString& SavePath, int64 ResumeOffset)
{
#if PLATFORM_ANDROID
	int64 Result = -1;

	JNIEnv* Env = FAndroidApplication::GetJavaEnv();
	if (!Env)
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Failed to get JNI environment"));
		return Result;
	}

	// 获取 DownloadManager 实例
	jclass ContextClass = Env->FindClass("android/content/Context");
	if (!ContextClass)
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Failed to find Context class"));
		return Result;
	}

	jstring DownloadServiceStr = Env->NewStringUTF("download");
	jmethodID GetSystemServiceMethod = Env->GetMethodID(ContextClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");

	jobject Activity = FAndroidApplication::GetGameActivityThis();
	if (!Activity || !GetSystemServiceMethod)
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Failed to get activity or getSystemService method"));
		Env->DeleteLocalRef(DownloadServiceStr);
		Env->DeleteLocalRef(ContextClass);
		return Result;
	}

	jobject DownloadManagerObj = Env->CallObjectMethod(Activity, GetSystemServiceMethod, DownloadServiceStr);
	Env->DeleteLocalRef(DownloadServiceStr);

	if (!DownloadManagerObj || Env->ExceptionCheck())
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Failed to get DownloadManager instance"));
		Env->ExceptionClear();
		Env->DeleteLocalRef(ContextClass);
		return Result;
	}

	// 创建 DownloadManager.Request
	jclass RequestClass = Env->FindClass("android/app/DownloadManager$Request");
	jclass UriClass = Env->FindClass("android/net/Uri");
	if (!RequestClass || !UriClass)
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Failed to find Request or Uri class"));
		Env->DeleteLocalRef(DownloadManagerObj);
		Env->DeleteLocalRef(ContextClass);
		return Result;
	}

	jmethodID UriParseMethod = Env->GetStaticMethodID(UriClass, "parse", "(Ljava/lang/String;)Landroid/net/Uri;");
	jstring UrlStr = Env->NewStringUTF(TCHAR_TO_UTF8(*Url));
	jobject UriObj = Env->CallStaticObjectMethod(UriClass, UriParseMethod, UrlStr);
	Env->DeleteLocalRef(UrlStr);

	if (!UriObj || Env->ExceptionCheck())
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Failed to parse URL: %s"), *Url);
		Env->ExceptionClear();
		Env->DeleteLocalRef(RequestClass);
		Env->DeleteLocalRef(UriClass);
		Env->DeleteLocalRef(DownloadManagerObj);
		Env->DeleteLocalRef(ContextClass);
		return Result;
	}

	jmethodID RequestConstructor = Env->GetMethodID(RequestClass, "<init>", "(Landroid/net/Uri;)V");
	jobject RequestObj = Env->NewObject(RequestClass, RequestConstructor, UriObj);
	Env->DeleteLocalRef(UriObj);

	if (!RequestObj || Env->ExceptionCheck())
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Failed to create DownloadManager.Request"));
		Env->ExceptionClear();
		Env->DeleteLocalRef(RequestClass);
		Env->DeleteLocalRef(UriClass);
		Env->DeleteLocalRef(DownloadManagerObj);
		Env->DeleteLocalRef(ContextClass);
		return Result;
	}

	// 设置目标路径
	jmethodID SetDestinationUriMethod = Env->GetMethodID(RequestClass, "setDestinationUri", "(Landroid/net/Uri;)V");
	if (SetDestinationUriMethod)
	{
		FString AbsolutePath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*SavePath);
		FString FileUri = FString::Printf(TEXT("file://%s"), *AbsolutePath);
		jstring UriString = Env->NewStringUTF(TCHAR_TO_UTF8(*FileUri));
		jobject DestUri = Env->CallStaticObjectMethod(UriClass, UriParseMethod, UriString);
		Env->DeleteLocalRef(UriString);

		if (DestUri)
		{
			Env->CallVoidMethod(RequestObj, SetDestinationUriMethod, DestUri);
			Env->DeleteLocalRef(DestUri);
		}
	}

	// 设置标题
	jmethodID SetTitleMethod = Env->GetMethodID(RequestClass, "setTitle", "(Ljava/lang/CharSequence;)V");
	if (SetTitleMethod)
	{
		jstring TitleStr = Env->NewStringUTF("HotUpdate Download");
		Env->CallVoidMethod(RequestObj, SetTitleMethod, TitleStr);
		Env->DeleteLocalRef(TitleStr);
	}

	// 设置通知栏可见性
	jmethodID SetNotifVisibilityMethod = Env->GetMethodID(RequestClass, "setNotificationVisibility", "(I)V");
	if (SetNotifVisibilityMethod)
	{
		Env->CallVoidMethod(RequestObj, SetNotifVisibilityMethod, 1);
	}

	// 断点续传：添加 Range header
	if (ResumeOffset > 0)
	{
		jmethodID AddRequestHeaderMethod = Env->GetMethodID(RequestClass, "addRequestHeader", "(Ljava/lang/String;Ljava/lang/String;)V");
		if (AddRequestHeaderMethod)
		{
			FString RangeValue = FString::Printf(TEXT("bytes=%lld-"), ResumeOffset);
			jstring RangeKey = Env->NewStringUTF("Range");
			jstring RangeVal = Env->NewStringUTF(TCHAR_TO_UTF8(*RangeValue));
			Env->CallVoidMethod(RequestObj, AddRequestHeaderMethod, RangeKey, RangeVal);
			Env->DeleteLocalRef(RangeKey);
			Env->DeleteLocalRef(RangeVal);
		}
	}

	// 允许移动网络下载
	jmethodID SetAllowedNetworkTypesMethod = Env->GetMethodID(RequestClass, "setAllowedNetworkTypes", "(I)V");
	if (SetAllowedNetworkTypesMethod)
	{
		Env->CallVoidMethod(RequestObj, SetAllowedNetworkTypesMethod, 3);
	}

	// 调用 DownloadManager.enqueue(request)
	jclass DownloadManagerClass = Env->GetObjectClass(DownloadManagerObj);
	jmethodID EnqueueMethod = Env->GetMethodID(DownloadManagerClass, "enqueue", "(Landroid/app/DownloadManager\$Request;)J");

	if (EnqueueMethod)
	{
		Result = (int64)Env->CallLongMethod(DownloadManagerObj, EnqueueMethod, RequestObj);
		if (Env->ExceptionCheck())
		{
			UE_LOG(LogHotUpdate, Error, TEXT("DownloadManager.enqueue() threw exception"));
			Env->ExceptionClear();
			Result = -1;
		}
	}

	Env->DeleteLocalRef(RequestObj);
	Env->DeleteLocalRef(RequestClass);
	Env->DeleteLocalRef(UriClass);
	Env->DeleteLocalRef(DownloadManagerClass);
	Env->DeleteLocalRef(DownloadManagerObj);
	Env->DeleteLocalRef(ContextClass);

	return Result;
#else
	UE_LOG(LogHotUpdate, Error, TEXT("EnqueueDownloadRequest called on non-Android platform"));
	return -1;
#endif
}

bool UHotUpdateAndroidDownloader::QueryDownloadStatus(int64 DownloadId, int32& OutStatus, int64& OutBytesSoFar, int64& OutBytesTotal)
{
#if PLATFORM_ANDROID
	JNIEnv* Env = FAndroidApplication::GetJavaEnv();
	if (!Env)
	{
		return false;
	}

	jclass ContextClass = Env->FindClass("android/content/Context");
	if (!ContextClass) return false;

	jstring DownloadServiceStr = Env->NewStringUTF("download");
	jmethodID GetSystemServiceMethod = Env->GetMethodID(ContextClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
	jobject Activity = FAndroidApplication::GetGameActivityThis();

	if (!Activity || !GetSystemServiceMethod)
	{
		Env->DeleteLocalRef(DownloadServiceStr);
		Env->DeleteLocalRef(ContextClass);
		return false;
	}

	jobject DownloadManagerObj = Env->CallObjectMethod(Activity, GetSystemServiceMethod, DownloadServiceStr);
	Env->DeleteLocalRef(DownloadServiceStr);
	Env->DeleteLocalRef(ContextClass);

	if (!DownloadManagerObj) return false;

	jclass QueryClass = Env->FindClass("android/app/DownloadManager\$Query");
	if (!QueryClass)
	{
		Env->DeleteLocalRef(DownloadManagerObj);
		return false;
	}

	jmethodID QueryConstructor = Env->GetMethodID(QueryClass, "<init>", "()V");
	jobject QueryObj = Env->NewObject(QueryClass, QueryConstructor);

	jmethodID SetFilterByIdMethod = Env->GetMethodID(QueryClass, "setFilterById", "([J)V");
	jlongArray IdArray = Env->NewLongArray(1);
	jlong IdValue = (jlong)DownloadId;
	Env->SetLongArrayRegion(IdArray, 0, 1, &IdValue);
	Env->CallVoidMethod(QueryObj, SetFilterByIdMethod, IdArray);
	Env->DeleteLocalRef(IdArray);

	jclass DownloadManagerClass = Env->GetObjectClass(DownloadManagerObj);
	jmethodID QueryMethod = Env->GetMethodID(DownloadManagerClass, "query", "(Landroid/app/DownloadManager\$Query;)Landroid/database/Cursor;");
	jobject CursorObj = Env->CallObjectMethod(DownloadManagerObj, QueryMethod, QueryObj);

	bool bResult = false;
	if (CursorObj)
	{
		jclass CursorClass = Env->GetObjectClass(CursorObj);
		jmethodID MoveToFirstMethod = Env->GetMethodID(CursorClass, "moveToFirst", "()Z");

		if (Env->CallBooleanMethod(CursorObj, MoveToFirstMethod))
		{
			jmethodID GetColumnIndexMethod = Env->GetMethodID(CursorClass, "getColumnIndex", "(Ljava/lang/String;)I");
			jmethodID GetIntMethod = Env->GetMethodID(CursorClass, "getInt", "(I)I");
			jmethodID GetLongMethod = Env->GetMethodID(CursorClass, "getLong", "(I)J");

			jstring StatusCol = Env->NewStringUTF("status");
			int32 StatusIdx = (int32)Env->CallIntMethod(CursorObj, GetColumnIndexMethod, StatusCol);
			Env->DeleteLocalRef(StatusCol);
			OutStatus = (int32)Env->CallIntMethod(CursorObj, GetIntMethod, StatusIdx);

			jstring BytesSoFarCol = Env->NewStringUTF("bytes_so_far");
			int32 BytesSoFarIdx = (int32)Env->CallIntMethod(CursorObj, GetColumnIndexMethod, BytesSoFarCol);
			Env->DeleteLocalRef(BytesSoFarCol);
			OutBytesSoFar = (int64)Env->CallLongMethod(CursorObj, GetLongMethod, BytesSoFarIdx);

			jstring TotalSizeCol = Env->NewStringUTF("total_size");
			int32 TotalSizeIdx = (int32)Env->CallIntMethod(CursorObj, GetColumnIndexMethod, TotalSizeCol);
			Env->DeleteLocalRef(TotalSizeCol);
			OutBytesTotal = (int64)Env->CallLongMethod(CursorObj, GetLongMethod, TotalSizeIdx);

			bResult = true;
		}

		jmethodID CloseMethod = Env->GetMethodID(Env->GetObjectClass(CursorObj), "close", "()V");
		Env->CallVoidMethod(CursorObj, CloseMethod);
		Env->DeleteLocalRef(CursorObj);
	}

	Env->DeleteLocalRef(QueryObj);
	Env->DeleteLocalRef(QueryClass);
	Env->DeleteLocalRef(DownloadManagerClass);
	Env->DeleteLocalRef(DownloadManagerObj);

	return bResult;
#else
	OutStatus = 0;
	OutBytesSoFar = 0;
	OutBytesTotal = 0;
	return false;
#endif
}

void UHotUpdateAndroidDownloader::RemoveDownload(int64 DownloadId)
{
#if PLATFORM_ANDROID
	if (DownloadId < 0) return;

	JNIEnv* Env = FAndroidApplication::GetJavaEnv();
	if (!Env) return;

	jclass ContextClass = Env->FindClass("android/content/Context");
	if (!ContextClass) return;

	jstring DownloadServiceStr = Env->NewStringUTF("download");
	jmethodID GetSystemServiceMethod = Env->GetMethodID(ContextClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
	jobject Activity = FAndroidApplication::GetGameActivityThis();

	if (!Activity || !GetSystemServiceMethod)
	{
		Env->DeleteLocalRef(DownloadServiceStr);
		Env->DeleteLocalRef(ContextClass);
		return;
	}

	jobject DownloadManagerObj = Env->CallObjectMethod(Activity, GetSystemServiceMethod, DownloadServiceStr);
	Env->DeleteLocalRef(DownloadServiceStr);
	Env->DeleteLocalRef(ContextClass);

	if (!DownloadManagerObj) return;

	jclass DownloadManagerClass = Env->GetObjectClass(DownloadManagerObj);
	jmethodID RemoveMethod = Env->GetMethodID(DownloadManagerClass, "remove", "([J)I");

	jlongArray IdArray = Env->NewLongArray(1);
	jlong IdValue = (jlong)DownloadId;
	Env->SetLongArrayRegion(IdArray, 0, 1, &IdValue);
	Env->CallIntMethod(DownloadManagerObj, RemoveMethod, IdArray);
	Env->DeleteLocalRef(IdArray);

	Env->DeleteLocalRef(DownloadManagerClass);
	Env->DeleteLocalRef(DownloadManagerObj);
#endif
}