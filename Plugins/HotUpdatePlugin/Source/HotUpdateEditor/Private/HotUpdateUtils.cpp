// Copyright czm. All Rights Reserved.

#include "HotUpdateUtils.h"

FString HotUpdateUtils::GetPlatformString(EHotUpdatePlatform Platform)
{
	switch (Platform)
	{
	case EHotUpdatePlatform::Windows:
		return TEXT("Windows");
	case EHotUpdatePlatform::Android:
		return TEXT("Android");
	case EHotUpdatePlatform::IOS:
		return TEXT("IOS");
	default:
		return TEXT("Windows");
	}
}

FString HotUpdateUtils::GetPlatformDirectoryName(EHotUpdatePlatform Platform)
{
	switch (Platform)
	{
	case EHotUpdatePlatform::Windows:
		return TEXT("Win64");
	case EHotUpdatePlatform::Android:
		return TEXT("Android");
	case EHotUpdatePlatform::IOS:
		return TEXT("IOS");
	default:
		return TEXT("Win64");
	}
}