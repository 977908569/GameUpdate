// Copyright czm. All Rights Reserved.

#include "Download/HotUpdateAndroidDownloader.h"
#include "HotUpdate.h"

UHotUpdateAndroidDownloader::UHotUpdateAndroidDownloader()
{
}

void UHotUpdateAndroidDownloader::Initialize(int32 InMaxConcurrentDownloads)
{
	UE_LOG(LogHotUpdate, Warning, TEXT("AndroidDownloader::Initialize - not implemented on this platform"));
}

void UHotUpdateAndroidDownloader::AddDownloadTask(const FString& Url, const FString& SavePath, int64 ExpectedSize, const FString& InExpectedHash)
{
	UE_LOG(LogHotUpdate, Warning, TEXT("AndroidDownloader::AddDownloadTask - not implemented on this platform"));
}

void UHotUpdateAndroidDownloader::StartDownload()
{
	UE_LOG(LogHotUpdate, Warning, TEXT("AndroidDownloader::StartDownload - not implemented on this platform"));
}

void UHotUpdateAndroidDownloader::PauseDownload()
{
	UE_LOG(LogHotUpdate, Warning, TEXT("AndroidDownloader::PauseDownload - not implemented on this platform"));
}

void UHotUpdateAndroidDownloader::ResumeDownload()
{
	UE_LOG(LogHotUpdate, Warning, TEXT("AndroidDownloader::ResumeDownload - not implemented on this platform"));
}

void UHotUpdateAndroidDownloader::CancelDownload(bool bDeleteTempFiles)
{
	UE_LOG(LogHotUpdate, Warning, TEXT("AndroidDownloader::CancelDownload - not implemented on this platform"));
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
