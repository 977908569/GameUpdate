// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Download/HotUpdateDownloaderBase.h"
#include "HotUpdateIOSDownloader.generated.h"

/**
 * iOS 下载器
 */
UCLASS()
class HOTUPDATE_API UHotUpdateIOSDownloader : public UHotUpdateDownloaderBase
{
	GENERATED_BODY()

public:
	UHotUpdateIOSDownloader();

	virtual void Initialize(int32 InMaxConcurrentDownloads = 3) override;
	virtual void AddDownloadTask(const FString& Url, const FString& SavePath, int64 ExpectedSize = 0, const FString& InExpectedHash = TEXT("")) override;
	virtual void StartDownload() override;
	virtual void PauseDownload() override;
	virtual void ResumeDownload() override;
	virtual void CancelDownload() override;
	virtual FHotUpdateProgress GetProgress() const override;
	virtual bool IsDownloading() const override;
	virtual bool IsPaused() const override;
};