// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HotUpdateWidget.generated.h"

class UTextBlock;
class UProgressBar;
class UButton;
class UWidgetSwitcher;
class UHotUpdateManager;

/**
 * 热更新界面控件
 *
 * 提供热更检查、下载进度显示、更新安装等功能
 * 状态流程：
 * 1. Idle -> 显示检查按钮
 * 2. CheckingVersion -> 显示检查中状态
 * 3. UpdateAvailable -> 显示版本信息和下载按钮
 * 4. Downloading -> 显示下载进度
 * 5. Success/Failed -> 显示结果
 */
UCLASS()
class UHotUpdateWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UHotUpdateWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** 开始检查更新 */
	UFUNCTION(BlueprintCallable, Category = "HotUpdate")
	void StartCheckUpdate();

	/** 开始下载更新 */
	UFUNCTION(BlueprintCallable, Category = "HotUpdate")
	void StartDownload();

	/** 暂停下载 */
	UFUNCTION(BlueprintCallable, Category = "HotUpdate")
	void PauseDownload();

	/** 恢复下载 */
	UFUNCTION(BlueprintCallable, Category = "HotUpdate")
	void ResumeDownload();

	/** 应用更新 */
	UFUNCTION(BlueprintCallable, Category = "HotUpdate")
	void ApplyUpdate();

	/** 关闭界面 */
	UFUNCTION(BlueprintCallable, Category = "HotUpdate")
	void CloseWidget();

protected:
	// == 状态更新 ==
	void UpdateUIForState(EHotUpdateState State) const;
	void UpdateProgressUI(const struct FHotUpdateProgress& Progress) const;
	void UpdateVersionInfo() const;

	// == 事件回调 ==
	UFUNCTION()
	void OnStateChanged(EHotUpdateState NewState);

	UFUNCTION()
	void OnVersionCheckComplete(const struct FHotUpdateVersionCheckResult& Result);

	UFUNCTION()
	void OnDownloadProgress(const struct FHotUpdateProgress& Progress);

	UFUNCTION()
	void OnDownloadComplete(bool bSuccess);

	UFUNCTION()
	void OnApplyComplete(bool bSuccess, const FString& ErrorMessage);

	UFUNCTION()
	void OnError(EHotUpdateError ErrorType, const FString& ErrorMessage);

	// == 按钮点击事件 ==
	UFUNCTION()
	void OnCheckButtonClicked();

	UFUNCTION()
	void OnDownloadButtonClicked();

	UFUNCTION()
	void OnPauseButtonClicked();

	UFUNCTION()
	void OnResumeButtonClicked();

	UFUNCTION()
	void OnApplyButtonClicked();

	UFUNCTION()
	void OnCloseButtonClicked();

protected:
	// == UI 组件绑定（子类蓝图中绑定）==

	/** 状态切换器 */
	UPROPERTY(meta = (BindWidgetOptional))
	UWidgetSwitcher* StateSwitcher;

	// == 检查更新状态 ==
	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* CheckStatePanel;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* CheckButton;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* CheckButtonText;

	// == 版本信息状态 ==
	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* VersionInfoPanel;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* CurrentVersionText;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* LatestVersionText;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* ReleaseNotesText;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* UpdateSizeText;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* DownloadButton;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* SkipButton;

	// == 下载进度状态 ==
	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* DownloadStatePanel;

	UPROPERTY(meta = (BindWidgetOptional))
	UProgressBar* DownloadProgressBar;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* ProgressPercentText;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* DownloadSpeedText;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* RemainingTimeText;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* CurrentFileText;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* PauseButton;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* ResumeButton;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* CancelButton;

	// == 安装状态 ==
	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* InstallingPanel;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* InstallingText;

	// == 成功状态 ==
	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* SuccessPanel;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* SuccessText;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* ApplyButton;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* RestartButton;

	// == 错误状态 ==
	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* ErrorPanel;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* ErrorText;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* RetryButton;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* CloseErrorButton;

	// == 无更新状态 ==
	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* NoUpdatePanel;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* NoUpdateText;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* CloseNoUpdateButton;

	// == 关闭按钮（通用）==
	UPROPERTY(meta = (BindWidgetOptional))
	UButton* CloseButton;

private:
	/** 热更管理器引用 */
	UPROPERTY(Transient)
	TObjectPtr<UHotUpdateManager> HotUpdateManager;
	/** 是否正在下载 */
	bool bIsPaused;
};