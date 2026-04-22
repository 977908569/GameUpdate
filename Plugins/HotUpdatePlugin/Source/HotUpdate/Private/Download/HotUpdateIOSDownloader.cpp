// Copyright czm. All Rights Reserved.

#include "Download/HotUpdateIOSDownloader.h"
#include "HotUpdate.h"

UHotUpdateIOSDownloader::UHotUpdateIOSDownloader()
{
}

void UHotUpdateIOSDownloader::Initialize(int32 InMaxConcurrentDownloads)
{
	UE_LOG(LogHotUpdate, Warning, TEXT("IOSDownloader::Initialize - using empty implementation"));
}

void UHotUpdateIOSDownloader::AddDownloadTask(const FString& Url, const FString& SavePath, int64 ExpectedSize, const FString& InExpectedHash)
{
	UE_LOG(LogHotUpdate, Warning, TEXT("IOSDownloader::AddDownloadTask - using empty implementation"));
}

void UHotUpdateIOSDownloader::StartDownload()
{
	UE_LOG(LogHotUpdate, Warning, TEXT("IOSDownloader::StartDownload - using empty implementation"));
}

void UHotUpdateIOSDownloader::PauseDownload()
{
	UE_LOG(LogHotUpdate, Warning, TEXT("IOSDownloader::PauseDownload - using empty implementation"));
}

void UHotUpdateIOSDownloader::ResumeDownload()
{
	UE_LOG(LogHotUpdate, Warning, TEXT("IOSDownloader::ResumeDownload - using empty implementation"));
}

void UHotUpdateIOSDownloader::CancelDownload()
{
	UE_LOG(LogHotUpdate, Warning, TEXT("IOSDownloader::CancelDownload - using empty implementation"));
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