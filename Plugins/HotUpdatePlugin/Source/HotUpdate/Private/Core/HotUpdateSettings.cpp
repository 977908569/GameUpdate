// Copyright czm. All Rights Reserved.

#include "Core/HotUpdateSettings.h"
#include "Misc/Paths.h"

UHotUpdateSettings::UHotUpdateSettings()
	: ManifestUrl(TEXT(""))
	, ResourceBaseUrl(TEXT(""))
	, RequestTimeout(30.0f)
	, MaxConcurrentDownloads(3)
	, MaxRetryCount(3)
	, RetryInterval(2.0f)
	, bEnableResume(true)
	, DownloadTimeout(300.0f)
	, LocalPakDirectory(TEXT("Saved/HotUpdate"))
	, MaxLocalVersionCount(3)
	, bAutoCleanupOldVersions(true)
	, bAutoCheckOnStartup(true)
	, bAutoDownload(true)
	, bEnableMinimalPackage(false)
{
}

FString UHotUpdateSettings::GetLocalPakFullPath() const
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / LocalPakDirectory);
}

UHotUpdateSettings* UHotUpdateSettings::Get()
{
	return GetMutableDefault<UHotUpdateSettings>();
}

bool UHotUpdateSettings::ValidateUrl(const FString& Url, FString& OutErrorMessage)
{
	if (Url.IsEmpty())
	{
		OutErrorMessage = TEXT("URL is empty");
		return false;
	}

	bool bIsHttps = Url.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase);
	bool bIsHttp = Url.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase);

	if (!bIsHttps && !bIsHttp)
	{
		OutErrorMessage = TEXT("URL must start with http:// or https://");
		return false;
	}

	return true;
}
