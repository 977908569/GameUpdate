// Copyright czm. All Rights Reserved.

#include "Download/HotUpdateAndroidDownloader.h"
#include "HotUpdate.h"

UHotUpdateAndroidDownloader::UHotUpdateAndroidDownloader()
{
}

void UHotUpdateAndroidDownloader::Initialize(int32 InMaxConcurrentDownloads)
{
	UE_LOG(LogHotUpdate, Warning, TEXT("AndroidDownloader::Initialize - using empty implementation"));
}

void UHotUpdateAndroidDownloader::AddDownloadTask(const FString& Url, const FString& SavePath, int64 ExpectedSize, const FString& InExpectedHash)
{
	UE_LOG(LogHotUpdate, Warning, TEXT("AndroidDownloader::AddDownloadTask - using empty implementation"));
}

void UHotUpdateAndroidDownloader::StartDownload()
{
	UE_LOG(LogHotUpdate, Warning, TEXT("AndroidDownloader::StartDownload - using empty implementation"));
}

void UHotUpdateAndroidDownloader::PauseDownload()
{
	UE_LOG(LogHotUpdate, Warning, TEXT("AndroidDownloader::PauseDownload - using empty implementation"));
}

void UHotUpdateAndroidDownloader::ResumeDownload()
{
	UE_LOG(LogHotUpdate, Warning, TEXT("AndroidDownloader::ResumeDownload - using empty implementation"));
}

void UHotUpdateAndroidDownloader::CancelDownload()
{
	UE_LOG(LogHotUpdate, Warning, TEXT("AndroidDownloader::CancelDownload - using empty implementation"));
}

FHotUpdateProgress UHotUpdateAndroidDownloader::GetProgress() const
{
	return FHotUpdateProgress();
}

bool UHotUpdateAndroidDownloader::IsDownloading() const
{
	return false;
}

bool UHotUpdateAndroidDownloader::IsPaused() const
{
	return false;
}