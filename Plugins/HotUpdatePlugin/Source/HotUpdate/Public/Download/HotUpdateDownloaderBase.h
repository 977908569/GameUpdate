// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/HotUpdateTypes.h"
#include "Core/HotUpdateProgress.h"
#include "Core/HotUpdateFileInfo.h"
#include "Core/HotUpdateContainerTypes.h"
#include "HotUpdateDownloaderBase.generated.h"

/**
 * 下载器基类
 *
 * 定义统一的下载接口，平台特定子类重写虚方法实现各自的下载机制。
 * 由于 UE UHT 不支持纯虚 UFUNCTION，所有 UFUNCTION 提供安全的默认实现（日志警告 + 空操作）。
 * AddDownloadTasks / AddContainerDownloadTasks 在基类中提供默认实现（遍历调用 AddDownloadTask），
 * 子类只需重写 AddDownloadTask 即可自动获得批量添加能力。
 */
UCLASS(Abstract, BlueprintType, Blueprintable)
class HOTUPDATE_API UHotUpdateDownloaderBase : public UObject
{
	GENERATED_BODY()

public:
	UHotUpdateDownloaderBase();

	/// 初始化下载器
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Download")
	virtual void Initialize(int32 InMaxConcurrentDownloads = 3);

	/// 添加单个下载任务
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Download")
	virtual void AddDownloadTask(const FString& Url, const FString& SavePath, int64 ExpectedSize = 0, const FString& InExpectedHash = TEXT(""));

	/// 批量添加下载任务（默认实现遍历调用 AddDownloadTask）
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Download")
	virtual void AddDownloadTasks(const TArray<FHotUpdateFileInfo>& Files, const FString& BaseUrl, const FString& SaveDir);

	/// 批量添加容器下载任务，IoStore 容器（默认实现遍历调用 AddDownloadTask）
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Download")
	virtual void AddContainerDownloadTasks(const TArray<FHotUpdateContainerInfo>& Containers, const FString& BaseUrl, const FString& SaveDir);

	/// 开始下载
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Download")
	virtual void StartDownload();

	/// 暂停下载（取消进行中的请求，临时文件保留供续传）
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Download")
	virtual void PauseDownload();

	/// 恢复下载
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Download")
	virtual void ResumeDownload();

	/// 取消下载
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Download")
	virtual void CancelDownload();

	/// 获取当前进度
	UFUNCTION(BlueprintPure, Category = "HotUpdate|Download")
	virtual FHotUpdateProgress GetProgress() const;

	/// 是否正在下载
	UFUNCTION(BlueprintPure, Category = "HotUpdate|Download")
	virtual bool IsDownloading() const;

	/// 是否暂停中
	UFUNCTION(BlueprintPure, Category = "HotUpdate|Download")
	virtual bool IsPaused() const;

	// == 事件委托 ==

	/// 进度更新事件
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnProgress, const FHotUpdateProgress&, Progress);
	UPROPERTY(BlueprintAssignable, Category = "HotUpdate|Events")
	FOnProgress OnProgress;

	/// 下载完成事件
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnComplete, bool, bSuccess, const FString&, ErrorMessage);
	UPROPERTY(BlueprintAssignable, Category = "HotUpdate|Events")
	FOnComplete OnComplete;

	/// 单个文件完成事件
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnFileComplete, const FString&, FilePath, bool, bSuccess);
	UPROPERTY(BlueprintAssignable, Category = "HotUpdate|Events")
	FOnFileComplete OnFileComplete;

	// == 静态工厂 ==

	/**
	 * 创建当前平台合适的下载器实例
	 * @param Outer  外部对象（通常为 UHotUpdateManager）
	 * @return      新的下载器实例
	 */
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Download", meta = (DisplayName = "Create Downloader For Platform"))
	static UHotUpdateDownloaderBase* CreateDownloader(UObject* Outer);
};