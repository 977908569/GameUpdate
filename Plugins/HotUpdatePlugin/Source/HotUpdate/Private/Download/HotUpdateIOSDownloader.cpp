// Copyright czm. All Rights Reserved.

#include "Download/HotUpdateIOSDownloader.h"
#include "HotUpdate.h"

UHotUpdateIOSDownloader::UHotUpdateIOSDownloader()
{
}

void UHotUpdateIOSDownloader::Initialize(int32 InMaxConcurrentDownloads)
{
	UE_LOG(LogHotUpdate, Warning, TEXT("IOSDownloader::Initialize - not implemented on this platform"));
}

void UHotUpdateIOSDownloader::AddDownloadTask(const FString& Url, const FString& SavePath, int64 ExpectedSize, const FString& InExpectedHash)
{
	UE_LOG(LogHotUpdate, Warning, TEXT("IOSDownloader::AddDownloadTask - not implemented on this platform"));
}

void UHotUpdateIOSDownloader::StartDownload()
{
	UE_LOG(LogHotUpdate, Warning, TEXT("IOSDownloader::StartDownload - not implemented on this platform"));
}

void UHotUpdateIOSDownloader::PauseDownload()
{
	UE_LOG(LogHotUpdate, Warning, TEXT("IOSDownloader::PauseDownload - not implemented on this platform"));
}

void UHotUpdateIOSDownloader::ResumeDownload()
{
	UE_LOG(LogHotUpdate, Warning, TEXT("IOSDownloader::ResumeDownload - not implemented on this platform"));
}

void UHotUpdateIOSDownloader::CancelDownload(bool bDeleteTempFiles)
{
	UE_LOG(LogHotUpdate, Warning, TEXT("IOSDownloader::CancelDownload - not implemented on this platform"));
}

FHotUpdateProgress UHotUpdateIOSDownloader::GetProgress() const
{
	return FHotUpdateProgress();
}

bool UHotUpdateIOSDownloader::IsDownloading() const
{
	return false;
}

bool UHotUpdateIOSDownloader::IsPaused() const
{
	return false;
}
