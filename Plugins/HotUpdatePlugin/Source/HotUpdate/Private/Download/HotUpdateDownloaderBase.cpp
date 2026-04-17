// Copyright czm. All Rights Reserved.

#include "Download/HotUpdateDownloaderBase.h"
#include "HotUpdate.h"

// 平台特定下载器头文件
#if PLATFORM_ANDROID
	#include "Download/HotUpdateAndroidDownloader.h"
#elif PLATFORM_IOS
	#include "Download/HotUpdateIOSDownloader.h"
#else
	#include "Download/HotUpdateHttpDownloader.h"
#endif

UHotUpdateDownloaderBase::UHotUpdateDownloaderBase()
{
}

void UHotUpdateDownloaderBase::Initialize(int32 InMaxConcurrentDownloads)
{
	UE_LOG(LogHotUpdate, Warning, TEXT("UHotUpdateDownloaderBase::Initialize called on base class. Override in platform-specific subclass."));
}

void UHotUpdateDownloaderBase::AddDownloadTask(const FString& Url, const FString& SavePath, int64 ExpectedSize, const FString& InExpectedHash)
{
	UE_LOG(LogHotUpdate, Warning, TEXT("UHotUpdateDownloaderBase::AddDownloadTask called on base class. Override in platform-specific subclass."));
}

void UHotUpdateDownloaderBase::AddDownloadTasks(const TArray<FHotUpdateFileInfo>& Files, const FString& BaseUrl, const FString& SaveDir)
{
	// 共享实现：遍历调用 AddDownloadTask，子类只需重写 AddDownloadTask 即可
	for (const FHotUpdateFileInfo& File : Files)
	{
		FString FullUrl = BaseUrl / File.FilePath;
		FString SavePath = SaveDir / File.FilePath;
		AddDownloadTask(FullUrl, SavePath, File.FileSize, File.FileHash);
	}
	UE_LOG(LogHotUpdate, Log, TEXT("Added %d download tasks"), Files.Num());
}

void UHotUpdateDownloaderBase::AddContainerDownloadTasks(const TArray<FHotUpdateContainerInfo>& Containers, const FString& BaseUrl, const FString& SaveDir)
{
	// 共享实现：遍历调用 AddDownloadTask，子类只需重写 AddDownloadTask 即可
	for (const FHotUpdateContainerInfo& Container : Containers)
	{
		// 下载 .utoc 文件
		if (!Container.UtocPath.IsEmpty() && Container.UtocSize > 0)
		{
			FString FullUrl = BaseUrl.IsEmpty() ? Container.CustomDownloadUrl : BaseUrl / Container.UtocPath;
			FString SavePath = SaveDir / Container.UtocPath;
			AddDownloadTask(FullUrl, SavePath, Container.UtocSize, Container.UtocHash);
		}

		// 下载 .ucas 文件
		if (!Container.UcasPath.IsEmpty() && Container.UcasSize > 0)
		{
			FString FullUrl = BaseUrl.IsEmpty() ? Container.CustomDownloadUrl : BaseUrl / Container.UcasPath;
			FString SavePath = SaveDir / Container.UcasPath;
			AddDownloadTask(FullUrl, SavePath, Container.UcasSize, Container.UcasHash);
		}
	}
	UE_LOG(LogHotUpdate, Log, TEXT("Added %d container download tasks"), Containers.Num());
}

void UHotUpdateDownloaderBase::StartDownload()
{
	UE_LOG(LogHotUpdate, Warning, TEXT("UHotUpdateDownloaderBase::StartDownload called on base class. Override in platform-specific subclass."));
}

void UHotUpdateDownloaderBase::PauseDownload()
{
	UE_LOG(LogHotUpdate, Warning, TEXT("UHotUpdateDownloaderBase::PauseDownload called on base class. Override in platform-specific subclass."));
}

void UHotUpdateDownloaderBase::ResumeDownload()
{
	UE_LOG(LogHotUpdate, Warning, TEXT("UHotUpdateDownloaderBase::ResumeDownload called on base class. Override in platform-specific subclass."));
}

void UHotUpdateDownloaderBase::CancelDownload()
{
	UE_LOG(LogHotUpdate, Warning, TEXT("UHotUpdateDownloaderBase::CancelDownload called on base class. Override in platform-specific subclass."));
}

FHotUpdateProgress UHotUpdateDownloaderBase::GetProgress() const
{
	return FHotUpdateProgress();
}

bool UHotUpdateDownloaderBase::IsDownloading() const
{
	return false;
}

bool UHotUpdateDownloaderBase::IsPaused() const
{
	return false;
}

// == 工厂函数 ==
UHotUpdateDownloaderBase* UHotUpdateDownloaderBase::CreateDownloader(UObject* Outer)
{
#if PLATFORM_ANDROID
	UE_LOG(LogHotUpdate, Log, TEXT("Creating Android native downloader"));
	return NewObject<UHotUpdateAndroidDownloader>(Outer);
#elif PLATFORM_IOS
	UE_LOG(LogHotUpdate, Log, TEXT("Creating iOS native downloader"));
	return NewObject<UHotUpdateIOSDownloader>(Outer);
#else
	// Windows / Mac / Linux -- 使用 HTTP 下载器
	UE_LOG(LogHotUpdate, Log, TEXT("Creating HTTP downloader"));
	return NewObject<UHotUpdateHttpDownloader>(Outer);
#endif
}