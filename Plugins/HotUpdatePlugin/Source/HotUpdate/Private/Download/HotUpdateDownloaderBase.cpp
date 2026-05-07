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
	// BaseUrl 格式: ResourceBaseUrl/（不含版本号和平台）
	// 容器通过 version 字段指定版本目录，支持链式热更
	const FString PlatformName = FPlatformProperties::PlatformName();
	for (const FHotUpdateContainerInfo& Container : Containers)
	{
		// 构建容器级 URL: ResourceBaseUrl/Version/Platform/File
		FString ContainerBaseUrl;
		if (!Container.Version.IsEmpty() && !BaseUrl.IsEmpty())
		{
			ContainerBaseUrl = BaseUrl / Container.Version / PlatformName;
		}
		else
		{
			ContainerBaseUrl = BaseUrl;
		}

		auto DownloadFile = [&](const FHotUpdateFileInfo& File)
		{
			if (File.Path.IsEmpty() || File.Size <= 0) return;

			FString FullUrl = ContainerBaseUrl.IsEmpty() ? Container.CustomDownloadUrl : ContainerBaseUrl / File.Path;
			FString SavePath = SaveDir / File.Path;
			AddDownloadTask(FullUrl, SavePath, File.Size, File.Hash);
		};

		DownloadFile(Container.UtocFile);
		DownloadFile(Container.UcasFile);
		DownloadFile(Container.PakFile);
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

void UHotUpdateDownloaderBase::CancelDownload(bool bDeleteTempFiles)
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