// Copyright czm. All Rights Reserved.

#include "HotUpdateWidget.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/Button.h"
#include "Components/WidgetSwitcher.h"
#include "Core/HotUpdateManager.h"
#include "Core/HotUpdateTypes.h"
#include "Core/HotUpdateVersion.h"
#include "Core/HotUpdateProgress.h"
#include "Core/HotUpdateVersionInfo.h"
#include "UObject/UObjectGlobals.h"

UHotUpdateWidget::UHotUpdateWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
		  , StateSwitcher(nullptr), CheckStatePanel(nullptr), CheckButton(nullptr), CheckButtonText(nullptr),
		  VersionInfoPanel(nullptr),
		  CurrentVersionText(nullptr),
		  LatestVersionText(nullptr),
		  ReleaseNotesText(nullptr),
		  UpdateSizeText(nullptr),
		  DownloadButton(nullptr),
		  SkipButton(nullptr),
		  DownloadStatePanel(nullptr),
		  DownloadProgressBar(nullptr),
		  ProgressPercentText(nullptr),
		  DownloadSpeedText(nullptr),
		  RemainingTimeText(nullptr), CurrentFileText(nullptr),
		  PauseButton(nullptr),
		  ResumeButton(nullptr),
		  CancelButton(nullptr),
		  InstallingPanel(nullptr),
		  InstallingText(nullptr),
		  SuccessPanel(nullptr),
		  SuccessText(nullptr),
		  ApplyButton(nullptr),
		  RestartButton(nullptr), ErrorPanel(nullptr),
		  ErrorText(nullptr),
		  RetryButton(nullptr),
		  CloseErrorButton(nullptr),
		  NoUpdatePanel(nullptr),
		  NoUpdateText(nullptr),
		  CloseNoUpdateButton(nullptr),
		  CloseButton(nullptr),
		  bIsPaused(false)
{
}

void UHotUpdateWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 获取热更管理器
	if (UGameInstance* GI = GetGameInstance())
	{
		HotUpdateManager = GI->GetSubsystem<UHotUpdateManager>();
	}

	// 绑定事件
	if (HotUpdateManager)
	{
		HotUpdateManager->OnStateChanged.AddDynamic(this, &UHotUpdateWidget::OnStateChanged);
		HotUpdateManager->OnVersionCheckComplete.AddDynamic(this, &UHotUpdateWidget::OnVersionCheckComplete);
		HotUpdateManager->OnDownloadProgress.AddDynamic(this, &UHotUpdateWidget::OnDownloadProgress);
		HotUpdateManager->OnDownloadComplete.AddDynamic(this, &UHotUpdateWidget::OnDownloadComplete);
		HotUpdateManager->OnApplyComplete.AddDynamic(this, &UHotUpdateWidget::OnApplyComplete);
		HotUpdateManager->OnError.AddDynamic(this, &UHotUpdateWidget::OnError);
	}

	// 绑定按钮事件
	if (CheckButton)
	{
		CheckButton->OnClicked.AddDynamic(this, &UHotUpdateWidget::OnCheckButtonClicked);
	}
	if (DownloadButton)
	{
		DownloadButton->OnClicked.AddDynamic(this, &UHotUpdateWidget::OnDownloadButtonClicked);
	}
	if (PauseButton)
	{
		PauseButton->OnClicked.AddDynamic(this, &UHotUpdateWidget::OnPauseButtonClicked);
	}
	if (ResumeButton)
	{
		ResumeButton->OnClicked.AddDynamic(this, &UHotUpdateWidget::OnResumeButtonClicked);
	}
	if (ApplyButton)
	{
		ApplyButton->OnClicked.AddDynamic(this, &UHotUpdateWidget::OnApplyButtonClicked);
	}
	if (CloseButton)
	{
		CloseButton->OnClicked.AddDynamic(this, &UHotUpdateWidget::OnCloseButtonClicked);
	}
	if (CloseErrorButton)
	{
		CloseErrorButton->OnClicked.AddDynamic(this, &UHotUpdateWidget::OnCloseButtonClicked);
	}
	if (CloseNoUpdateButton)
	{
		CloseNoUpdateButton->OnClicked.AddDynamic(this, &UHotUpdateWidget::OnCloseButtonClicked);
	}
	if (RetryButton)
	{
		RetryButton->OnClicked.AddDynamic(this, &UHotUpdateWidget::OnCheckButtonClicked);
	}
	if (SkipButton)
	{
		SkipButton->OnClicked.AddDynamic(this, &UHotUpdateWidget::OnCloseButtonClicked);
	}

	// 初始化UI状态
	if (HotUpdateManager)
	{
		UpdateUIForState(HotUpdateManager->GetCurrentState());
	}
}

void UHotUpdateWidget::NativeDestruct()
{
	// 解绑事件
	if (HotUpdateManager)
	{
		HotUpdateManager->OnStateChanged.RemoveDynamic(this, &UHotUpdateWidget::OnStateChanged);
		HotUpdateManager->OnVersionCheckComplete.RemoveDynamic(this, &UHotUpdateWidget::OnVersionCheckComplete);
		HotUpdateManager->OnDownloadProgress.RemoveDynamic(this, &UHotUpdateWidget::OnDownloadProgress);
		HotUpdateManager->OnDownloadComplete.RemoveDynamic(this, &UHotUpdateWidget::OnDownloadComplete);
		HotUpdateManager->OnApplyComplete.RemoveDynamic(this, &UHotUpdateWidget::OnApplyComplete);
		HotUpdateManager->OnError.RemoveDynamic(this, &UHotUpdateWidget::OnError);
	}

	Super::NativeDestruct();
}

void UHotUpdateWidget::StartCheckUpdate()
{
	if (HotUpdateManager)
	{
		HotUpdateManager->CheckForUpdate();
	}
}

void UHotUpdateWidget::StartDownload()
{
	if (HotUpdateManager)
	{
		HotUpdateManager->StartDownload();
	}
}

void UHotUpdateWidget::PauseDownload()
{
	if (HotUpdateManager)
	{
		HotUpdateManager->PauseDownload();
		bIsPaused = true;
		if (PauseButton) PauseButton->SetVisibility(ESlateVisibility::Collapsed);
		if (ResumeButton) ResumeButton->SetVisibility(ESlateVisibility::Visible);
	}
}

void UHotUpdateWidget::ResumeDownload()
{
	if (HotUpdateManager)
	{
		HotUpdateManager->ResumeDownload();
		bIsPaused = false;
		if (PauseButton) PauseButton->SetVisibility(ESlateVisibility::Visible);
		if (ResumeButton) ResumeButton->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UHotUpdateWidget::ApplyUpdate()
{
	if (HotUpdateManager)
	{
		HotUpdateManager->ApplyUpdate();
	}
}

void UHotUpdateWidget::CloseWidget()
{
	RemoveFromParent();
}

void UHotUpdateWidget::UpdateUIForState(const EHotUpdateState State) const
{
	// 更新 StateSwitcher 活动控件
	if (StateSwitcher)
	{
		switch (State)
		{
		case EHotUpdateState::Idle:
			if (CheckStatePanel) StateSwitcher->SetActiveWidget(CheckStatePanel);
			break;
		case EHotUpdateState::CheckingVersion:
			// 检查中状态可以复用检查面板或显示加载动画
			if (CheckStatePanel) StateSwitcher->SetActiveWidget(CheckStatePanel);
			if (CheckButtonText) CheckButtonText->SetText(FText::FromString(TEXT("检查中...")));
			if (CheckButton) CheckButton->SetIsEnabled(false);
			break;
		case EHotUpdateState::UpdateAvailable:
			if (VersionInfoPanel) StateSwitcher->SetActiveWidget(VersionInfoPanel);
			break;
		case EHotUpdateState::Downloading:
			if (DownloadStatePanel) StateSwitcher->SetActiveWidget(DownloadStatePanel);
			break;
		case EHotUpdateState::Installing:
			if (InstallingPanel) StateSwitcher->SetActiveWidget(InstallingPanel);
			break;
		case EHotUpdateState::Success:
			if (SuccessPanel) StateSwitcher->SetActiveWidget(SuccessPanel);
			break;
		case EHotUpdateState::Failed:
			if (ErrorPanel) StateSwitcher->SetActiveWidget(ErrorPanel);
			break;
		default:
			break;
		}
	}
}

void UHotUpdateWidget::UpdateProgressUI(const FHotUpdateProgress& Progress) const
{
	// 更新进度条
	if (DownloadProgressBar)
	{
		DownloadProgressBar->SetPercent(Progress.TotalBytes > 0 ? (float)Progress.DownloadedBytes / Progress.TotalBytes : 0.0f);
	}

	// 更新进度百分比文本
	if (ProgressPercentText)
	{
		FString PercentStr = FString::Printf(TEXT("%.1f%%"), Progress.TotalBytes > 0 ? (float)Progress.DownloadedBytes / Progress.TotalBytes * 100.0f : 0.0f);
		ProgressPercentText->SetText(FText::FromString(PercentStr));
	}

	// 更新下载速度
	if (DownloadSpeedText)
	{
		float SpeedMB = Progress.DownloadSpeed / (1024.0f * 1024.0f);
		FString SpeedStr;
		if (SpeedMB >= 1.0f)
		{
			SpeedStr = FString::Printf(TEXT("%.2f MB/s"), SpeedMB);
		}
		else
		{
			float SpeedKB = Progress.DownloadSpeed / 1024.0f;
			SpeedStr = FString::Printf(TEXT("%.0f KB/s"), SpeedKB);
		}
		DownloadSpeedText->SetText(FText::FromString(SpeedStr));
	}

	// 更新剩余时间
	if (RemainingTimeText)
	{
		int32 TotalSeconds = FMath::RoundToInt(Progress.RemainingTime);
		int32 Minutes = TotalSeconds / 60;
		int32 Seconds = TotalSeconds % 60;
		FString TimeStr = FString::Printf(TEXT("剩余时间: %d:%02d"), Minutes, Seconds);
		RemainingTimeText->SetText(FText::FromString(TimeStr));
	}

	// 更新当前文件信息
	if (CurrentFileText)
	{
		FString FileStr = FString::Printf(TEXT("文件: %d / %d"), Progress.CurrentFileIndex + 1, Progress.TotalFiles);
		CurrentFileText->SetText(FText::FromString(FileStr));
	}
}

void UHotUpdateWidget::UpdateVersionInfo() const
{
	if (!HotUpdateManager) return;

	FHotUpdateVersionInfo CurrentVer = HotUpdateManager->GetCurrentVersion();
	FHotUpdateVersionInfo LatestVer = HotUpdateManager->GetLatestVersion();

	// 更新版本号文本
	if (CurrentVersionText)
	{
		FString VerStr = FString::Printf(TEXT("当前版本: %s"), *CurrentVer.ToString());
		CurrentVersionText->SetText(FText::FromString(VerStr));
	}

	if (LatestVersionText)
	{
		FString VerStr = FString::Printf(TEXT("最新版本: %s"), *LatestVer.ToString());
		LatestVersionText->SetText(FText::FromString(VerStr));
	}

	// 更新版本说明
	if (ReleaseNotesText)
	{
		ReleaseNotesText->SetText(FText::FromString(LatestVer.ReleaseNotes));
	}

	// 更新下载大小（从进度信息获取）
	if (UpdateSizeText)
	{
		FHotUpdateProgress Progress = HotUpdateManager->GetDownloadProgress();
		float SizeMB = static_cast<float>(Progress.TotalBytes) / (1024.0f * 1024.0f);
		FString SizeStr = FString::Printf(TEXT("更新大小: %.2f MB"), SizeMB);
		UpdateSizeText->SetText(FText::FromString(SizeStr));
	}
}

void UHotUpdateWidget::OnStateChanged(const EHotUpdateState NewState)
{
	UpdateUIForState(NewState);
}

void UHotUpdateWidget::OnVersionCheckComplete(const FHotUpdateVersionCheckResult& Result)
{
	// 恢复检查按钮状态
	if (CheckButton)
	{
		CheckButton->SetIsEnabled(true);
		if (CheckButtonText) CheckButtonText->SetText(FText::FromString(TEXT("检查更新")));
	}

	// 状态机已驱动 UpdateUIForState 切换到 VersionInfoPanel 或 NoUpdatePanel
	// 这里只需更新版本信息文本
	if (Result.bHasUpdate)
	{
		UpdateVersionInfo();
	}
	else
	{
		if (NoUpdateText)
		{
			NoUpdateText->SetText(FText::FromString(TEXT("已是最新版本")));
		}
	}
}

void UHotUpdateWidget::OnDownloadProgress(const FHotUpdateProgress& Progress)
{
	UpdateProgressUI(Progress);
}

void UHotUpdateWidget::OnDownloadComplete(bool bSuccess)
{
	if (bSuccess)
	{
		// 下载完成，准备安装
		if (StateSwitcher && SuccessPanel)
		{
			StateSwitcher->SetActiveWidget(SuccessPanel);
		}
		if (SuccessText)
		{
			SuccessText->SetText(FText::FromString(TEXT("下载完成，点击应用更新")));
		}
	}
	else
	{
		// 下载失败
		if (StateSwitcher && ErrorPanel)
		{
			StateSwitcher->SetActiveWidget(ErrorPanel);
		}
		if (ErrorText)
		{
			ErrorText->SetText(FText::FromString(TEXT("下载失败，请重试")));
		}
	}
}

void UHotUpdateWidget::OnApplyComplete(bool bSuccess, const FString& ErrorMessage)
{
	if (bSuccess)
	{
		if (SuccessText)
		{
			SuccessText->SetText(FText::FromString(TEXT("更新成功！")));
		}
	}
	else
	{
		if (StateSwitcher && ErrorPanel)
		{
			StateSwitcher->SetActiveWidget(ErrorPanel);
		}
		if (ErrorText)
		{
			ErrorText->SetText(FText::FromString(ErrorMessage));
		}
	}
}

void UHotUpdateWidget::OnError(EHotUpdateError ErrorType, const FString& ErrorMessage)
{
	if (StateSwitcher && ErrorPanel)
	{
		StateSwitcher->SetActiveWidget(ErrorPanel);
	}
	if (ErrorText)
	{
		FString ErrorTypeStr = UEnum::GetDisplayValueAsText(ErrorType).ToString();
		FString FullError = FString::Printf(TEXT("错误 [%s]: %s"), *ErrorTypeStr, *ErrorMessage);
		ErrorText->SetText(FText::FromString(FullError));
	}
}

void UHotUpdateWidget::OnCheckButtonClicked()
{
	StartCheckUpdate();
}

void UHotUpdateWidget::OnDownloadButtonClicked()
{
	StartDownload();
}

void UHotUpdateWidget::OnPauseButtonClicked()
{
	PauseDownload();
}

void UHotUpdateWidget::OnResumeButtonClicked()
{
	ResumeDownload();
}

void UHotUpdateWidget::OnApplyButtonClicked()
{
	ApplyUpdate();
}

void UHotUpdateWidget::OnCloseButtonClicked()
{
	CloseWidget();
}