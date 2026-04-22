// Copyright czm. All Rights Reserved.

#include "Download/HotUpdateDownloaderBase.h"
#include "Download/HotUpdateHttpDownloader.h"
#include "HotUpdate.h"

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

void UHotUpdateDownloaderBase::UpdateProgressCalculation(int64 TotalDownloaded, FHotUpdateProgress& InOutProgress,
	double& InOutLastProgressUpdateTime, int64& InOutLastDownloadedBytes, float UpdateInterval)
{
	double CurrentTime = FPlatformTime::Seconds();
	double ElapsedTime = CurrentTime - InOutLastProgressUpdateTime;

	if (ElapsedTime >= UpdateInterval)
	{
		int64 BytesSinceLastUpdate = TotalDownloaded - InOutLastDownloadedBytes;
		InOutProgress.DownloadSpeed = (float)(BytesSinceLastUpdate / ElapsedTime);

		if (InOutProgress.DownloadSpeed > 0)
		{
			int64 RemainingBytes = InOutProgress.TotalBytes - TotalDownloaded;
			InOutProgress.RemainingTime = (float)(RemainingBytes / InOutProgress.DownloadSpeed);
		}

		InOutLastProgressUpdateTime = CurrentTime;
		InOutLastDownloadedBytes = TotalDownloaded;
	}
}

// == 工厂函数 ==
UHotUpdateDownloaderBase* UHotUpdateDownloaderBase::CreateDownloader(UObject* Outer)
{
	// 所有平台暂时使用 HTTP 下载器
	UE_LOG(LogHotUpdate, Log, TEXT("Creating HTTP downloader"));
	return NewObject<UHotUpdateHttpDownloader>(Outer);
}