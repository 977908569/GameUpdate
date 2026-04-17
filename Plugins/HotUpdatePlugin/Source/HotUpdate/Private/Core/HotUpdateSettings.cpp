// Copyright czm. All Rights Reserved.

#include "Core/HotUpdateSettings.h"
#include "HotUpdate.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"

UHotUpdateSettings::UHotUpdateSettings()
	: ManifestUrl(TEXT(""))
	, ResourceBaseUrl(TEXT(""))
	, RequestTimeout(30.0f)
	, MaxConcurrentDownloads(3)
	, ChunkSizeMB(4)
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
	, bAllowHttpConnection(false)
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

	// 检查协议
	bool bIsHttps = Url.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase);
	bool bIsHttp = Url.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase);

	if (!bIsHttps && !bIsHttp)
	{
		OutErrorMessage = TEXT("URL must start with http:// or https://");
		return false;
	}

	// 检查是否允许 HTTP 连接
	if (bIsHttp && !IsHttpAllowed())
	{
		OutErrorMessage = TEXT("HTTP connection is not allowed. Please use HTTPS or enable bAllowHttpConnection in settings.");
		return false;
	}

	// 手动解析域名
	FString Protocol, Domain, Port, Path;
	int32 ProtocolEnd = Url.Find(TEXT("://"));
	if (ProtocolEnd == INDEX_NONE)
	{
		OutErrorMessage = TEXT("Failed to parse URL protocol");
		return false;
	}

	Protocol = Url.Left(ProtocolEnd);
	FString RestOfUrl = Url.Mid(ProtocolEnd + 3);

	// 查找域名结束位置（/或:或字符串结束）
	int32 DomainEnd = RestOfUrl.Find(TEXT("/"));
	if (DomainEnd == INDEX_NONE)
	{
		DomainEnd = RestOfUrl.Find(TEXT(":"));
	}
	if (DomainEnd == INDEX_NONE)
	{
		DomainEnd = RestOfUrl.Len();
	}

	Domain = RestOfUrl.Left(DomainEnd);

	// 检查域名白名单
	UHotUpdateSettings* Settings = Get();
	if (Settings && Settings->AllowedDomains.Num() > 0)
	{
		bool bDomainAllowed = false;
		for (const FString& AllowedDomain : Settings->AllowedDomains)
		{
			if (Domain.Equals(AllowedDomain, ESearchCase::IgnoreCase) ||
				Domain.EndsWith(*FString::Printf(TEXT(".%s"), *AllowedDomain), ESearchCase::IgnoreCase))
			{
				bDomainAllowed = true;
				break;
			}
		}

		if (!bDomainAllowed)
		{
			OutErrorMessage = FString::Printf(TEXT("Domain '%s' is not in the allowed list"), *Domain);
			return false;
		}
	}

	return true;
}

bool UHotUpdateSettings::IsHttpAllowed()
{
	UHotUpdateSettings* Settings = Get();
	return Settings ? Settings->bAllowHttpConnection : false;
}
