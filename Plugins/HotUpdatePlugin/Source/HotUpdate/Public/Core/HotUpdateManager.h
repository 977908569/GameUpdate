// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "HotUpdateTypes.h"
#include "HotUpdateProgress.h"
#include "HotUpdateVersion.h"
#include "Manifest/HotUpdateManifest.h"
#include "HotUpdateManager.generated.h"

class UHotUpdateDownloaderBase;
class UHotUpdatePakManager;
class UHotUpdateVersionStorage;
class UHotUpdateIncrementalCalculator;

/**
 * 热更新管理器
 *
 * 作为 GameInstanceSubsystem 运行，统一管理版本检查、下载、安装等流程
 * 使用方法：
 * 1. 通过 GameInstance 获取子系统
 * 2. 调用 CheckForUpdate 检查更新
 * 3. 监听事件回调处理结果
 * 4. 调用 StartDownload 开始下载
 * 5. 调用 ApplyUpdate 应用更新
 */
UCLASS()
class HOTUPDATE_API UHotUpdateManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UHotUpdateManager();

	// == UGameInstanceSubsystem 接口 ==
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// == 主要功能接口 ==

	/// 检查更新
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Main")
	void CheckForUpdate();

	/// 开始下载更新
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Main")
	bool StartDownload();

	/// 暂停下载
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Main")
	void PauseDownload();

	/// 恢复下载
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Main")
	void ResumeDownload();

	/// 取消下载
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Main")
	void CancelDownload();

	/// 应用更新（挂载 Pak）
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Main")
	bool ApplyUpdate();

	/// 回滚到上一版本
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Main")
	bool Rollback();

	/// 清理旧版本
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Main")
	void CleanupOldVersions();

	// == 状态查询 ==

	/// 获取当前状态
	UFUNCTION(BlueprintPure, Category = "HotUpdate|State")
	EHotUpdateState GetCurrentState() const { return CurrentState; }

	/// 是否有可用更新
	UFUNCTION(BlueprintPure, Category = "HotUpdate|State")
	bool HasUpdateAvailable() const { return bHasUpdateAvailable; }

	/// 获取当前版本
	UFUNCTION(BlueprintPure, Category = "HotUpdate|State")
	FHotUpdateVersionInfo GetCurrentVersion() const { return CurrentVersion; }

	/// 获取最新版本
	UFUNCTION(BlueprintPure, Category = "HotUpdate|State")
	FHotUpdateVersionInfo GetLatestVersion() const { return LatestVersion; }

	/// 获取下载进度
	UFUNCTION(BlueprintPure, Category = "HotUpdate|State")
	FHotUpdateProgress GetDownloadProgress() const { return DownloadProgress; }

	/// 是否正在下载
	UFUNCTION(BlueprintPure, Category = "HotUpdate|State")
	bool IsDownloading() const { return CurrentState == EHotUpdateState::Downloading; }

	/// 获取本地版本历史
	UFUNCTION(BlueprintPure, Category = "HotUpdate|State")
	TArray<FHotUpdateVersionInfo> GetLocalVersionHistory() const;

	// == 事件委托 ==

	/// 状态改变事件
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStateChanged, EHotUpdateState, NewState);
	UPROPERTY(BlueprintAssignable, Category = "HotUpdate|Events")
	FOnStateChanged OnStateChanged;

	/// 版本检查完成事件
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnVersionCheckComplete, const FHotUpdateVersionCheckResult&, Result);
	UPROPERTY(BlueprintAssignable, Category = "HotUpdate|Events")
	FOnVersionCheckComplete OnVersionCheckComplete;

	/// 下载进度更新事件
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDownloadProgress, const FHotUpdateProgress&, Progress);
	UPROPERTY(BlueprintAssignable, Category = "HotUpdate|Events")
	FOnDownloadProgress OnDownloadProgress;

	/// 下载完成事件
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDownloadComplete, bool, bSuccess);
	UPROPERTY(BlueprintAssignable, Category = "HotUpdate|Events")
	FOnDownloadComplete OnDownloadComplete;

	/// 更新应用完成事件
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnApplyComplete, bool, bSuccess, const FString&, ErrorMessage);
	UPROPERTY(BlueprintAssignable, Category = "HotUpdate|Events")
	FOnApplyComplete OnApplyComplete;

	/// 错误事件
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnError, EHotUpdateError, ErrorType, const FString&, ErrorMessage);
	UPROPERTY(BlueprintAssignable, Category = "HotUpdate|Events")
	FOnError OnError;

protected:
	/// 设置状态
	void SetState(EHotUpdateState NewState);

	/// 处理版本检查响应
	void HandleVersionCheckResponse(TSharedPtr<class IHttpRequest> Request, TSharedPtr<class IHttpResponse> Response, bool bSuccess);

	void HandleLatestVersionResponse(TSharedPtr<class IHttpRequest> Request, TSharedPtr<class IHttpResponse> Response, bool bSuccess);

	/// 验证下载文件
	bool VerifyDownloadedFiles();

	/// 构建资源下载基础 URL
	FString BuildDownloadBaseUrl() const;

	/// 下载进度回调
	UFUNCTION()
	void HandleDownloadProgress(const FHotUpdateProgress& Progress);

	/// 下载完成回调
	UFUNCTION()
	void HandleDownloadComplete(bool bSuccess, const FString& ErrorMessage);

private:
	/// 当前状态
	UPROPERTY(Transient)
	EHotUpdateState CurrentState;

	/// 是否有可用更新
	UPROPERTY(Transient)
	bool bHasUpdateAvailable;

	/// 当前版本
	UPROPERTY(Transient)
	FHotUpdateVersionInfo CurrentVersion;

	/// 最新版本
	UPROPERTY(Transient)
	FHotUpdateVersionInfo LatestVersion;

	/// 版本检查结果
	UPROPERTY(Transient)
	FHotUpdateVersionCheckResult VersionCheckResult;

	/// 下载进度
	UPROPERTY(Transient)
	FHotUpdateProgress DownloadProgress;

	/// 下载器实例
	UPROPERTY(Transient)
	TObjectPtr<UHotUpdateDownloaderBase> Downloader;

	/// Pak 管理器
	UPROPERTY(Transient)
	TObjectPtr<UHotUpdatePakManager> PakManager;

	/// 版本存储管理器
	UPROPERTY(Transient)
	TObjectPtr<UHotUpdateVersionStorage> VersionStorage;

	/// 增量下载计算器
	UPROPERTY(Transient)
	TObjectPtr<UHotUpdateIncrementalCalculator> IncrementalCalculator;

	/// HTTP 请求句柄
	TSharedPtr<class IHttpRequest> VersionCheckRequest;

	/// 自动检查更新的定时器句柄
	FTimerHandle AutoCheckTimerHandle;

	/// 缓存的服务器 Manifest（用于成功更新后保存到本地）
	FHotUpdateManifest CachedServerManifest;
};