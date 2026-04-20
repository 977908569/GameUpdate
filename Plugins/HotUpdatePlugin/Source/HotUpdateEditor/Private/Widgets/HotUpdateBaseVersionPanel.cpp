// Copyright czm. All Rights Reserved.

#include "Widgets/HotUpdateBaseVersionPanel.h"
#include "HotUpdateEditor.h"
#include "HotUpdateEditorStyle.h"
#include "HotUpdateBaseVersionBuilder.h"
#include "Styling/AppStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/Paths.h"
#include "Async/Async.h"

#define LOCTEXT_NAMESPACE "HotUpdateBaseVersionPanel"

void SHotUpdateBaseVersionPanel::Construct(const FArguments& InArgs)
{
	ParentWindow = InArgs._ParentWindow;
	bIsBuilding = false;

	// 初始化平台选项
	PlatformOptions.Add(MakeShareable(new EHotUpdatePlatform(EHotUpdatePlatform::Windows)));
	PlatformOptions.Add(MakeShareable(new EHotUpdatePlatform(EHotUpdatePlatform::Android)));
		PlatformOptions.Add(MakeShareable(new EHotUpdatePlatform(EHotUpdatePlatform::IOS)));
	SelectedPlatform = PlatformOptions[0];

	// 初始化构建配置选项
	BuildConfigOptions.Add(MakeShareable(new EHotUpdateBuildConfiguration(EHotUpdateBuildConfiguration::DebugGame)));
	BuildConfigOptions.Add(MakeShareable(new EHotUpdateBuildConfiguration(EHotUpdateBuildConfiguration::Development)));
	BuildConfigOptions.Add(MakeShareable(new EHotUpdateBuildConfiguration(EHotUpdateBuildConfiguration::Shipping)));
	SelectedBuildConfig = BuildConfigOptions[1];

		// 初始化 Android 纹理格式选项
		AndroidTextureFormatOptions.Add(MakeShareable(new EHotUpdateAndroidTextureFormat(EHotUpdateAndroidTextureFormat::ETC2)));
		AndroidTextureFormatOptions.Add(MakeShareable(new EHotUpdateAndroidTextureFormat(EHotUpdateAndroidTextureFormat::ASTC)));
		AndroidTextureFormatOptions.Add(MakeShareable(new EHotUpdateAndroidTextureFormat(EHotUpdateAndroidTextureFormat::DXT)));
		AndroidTextureFormatOptions.Add(MakeShareable(new EHotUpdateAndroidTextureFormat(EHotUpdateAndroidTextureFormat::Multi)));
		SelectedAndroidTextureFormat = AndroidTextureFormatOptions[0];

	// 初始化依赖策略选项
	DependencyStrategyOptions.Add(MakeShareable(new EHotUpdateDependencyStrategy(EHotUpdateDependencyStrategy::IncludeAll)));
	DependencyStrategyOptions.Add(MakeShareable(new EHotUpdateDependencyStrategy(EHotUpdateDependencyStrategy::HardOnly)));
	DependencyStrategyOptions.Add(MakeShareable(new EHotUpdateDependencyStrategy(EHotUpdateDependencyStrategy::SoftOnly)));
	DependencyStrategyOptions.Add(MakeShareable(new EHotUpdateDependencyStrategy(EHotUpdateDependencyStrategy::None)));
	SelectedDependencyStrategy = DependencyStrategyOptions[1];

	// 分包策略选项初始化
	PatchChunkStrategyOptions.Add(MakeShareable(new EHotUpdateChunkStrategy(EHotUpdateChunkStrategy::None)));
	PatchChunkStrategyOptions.Add(MakeShareable(new EHotUpdateChunkStrategy(EHotUpdateChunkStrategy::Size)));
	PatchChunkStrategyOptions.Add(MakeShareable(new EHotUpdateChunkStrategy(EHotUpdateChunkStrategy::Directory)));
	PatchChunkStrategyOptions.Add(MakeShareable(new EHotUpdateChunkStrategy(EHotUpdateChunkStrategy::PrimaryAsset)));
	PatchChunkStrategyOptions.Add(MakeShareable(new EHotUpdateChunkStrategy(EHotUpdateChunkStrategy::Hybrid)));
	SelectedPatchChunkStrategy = PatchChunkStrategyOptions[0];

	// 创建构建器
	Builder = NewObject<UHotUpdateBaseVersionBuilder>();
	Builder->OnBuildProgress.AddSP(this, &SHotUpdateBaseVersionPanel::OnBuildProgress);
	Builder->OnBuildComplete.AddSP(this, &SHotUpdateBaseVersionPanel::OnBuildComplete);

	// 默认输出目录
	BuildConfig.OutputDirectory = UHotUpdateBaseVersionBuilder::GetDefaultOutputDirectory();

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(0)
		[
			SNew(SVerticalBox)
			// 标题
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
				.Padding(FMargin(12, 8))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Fill)
					.Padding(0, 0, 12, 0)
					[
						SNew(SBox)
						.WidthOverride(3.0f)
						[
							SNew(SBorder)
							.BorderBackgroundColor(FHotUpdateEditorStyle::GetPrimaryColor())
							.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
						]
					]
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Title", "基础版本打包"))
						.Font(FHotUpdateEditorStyle::GetSubtitleFont())
						.ColorAndOpacity(FHotUpdateEditorStyle::GetTextPrimaryColor())
					]
				]
			]
			// 内容区域
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				.Padding(16)
				[
					CreateConfigSection()
				]
			]
			// 分隔线
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SSeparator)
			]
			// 进度和操作区域
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(16, 12)
			[
				CreateProgressSection()
			]
		]
	];
}

TSharedRef<SWidget> SHotUpdateBaseVersionPanel::CreateConfigSection()
{
	auto MakeSettingRow = [](const FText& Label, TSharedRef<SWidget> ValueWidget) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(100)
				[
					SNew(STextBlock)
					.Text(Label)
					.Font(FHotUpdateEditorStyle::GetNormalFont())
					.ColorAndOpacity(FHotUpdateEditorStyle::GetTextSecondaryColor())
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(8, 4)
			.VAlign(VAlign_Center)
			[
				ValueWidget
			];
	};

	return SNew(SVerticalBox)
	// 说明文字
	+ SVerticalBox::Slot()
	.AutoHeight()
	.Padding(0, 0, 0, 16)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("Description", "打包完整的基础版本（包含 exe/apk），并保存资源清单供后续增量打包使用。"))
		.Font(FHotUpdateEditorStyle::GetSmallFont())
		.ColorAndOpacity(FHotUpdateEditorStyle::GetTextSecondaryColor())
		.AutoWrapText(true)
	]
	// 版本号
	+ SVerticalBox::Slot()
	.AutoHeight()
	.Padding(0, 4)
	[
		MakeSettingRow(
			LOCTEXT("VersionLabel", "版本号:"),
			SAssignNew(VersionTextBox, SEditableText)
			.Text(FText::FromString(BuildConfig.VersionString))
			.HintText(LOCTEXT("VersionHint", "如 1.0.0"))
			.Font(FHotUpdateEditorStyle::GetNormalFont())
		)
	]
	// 目标平台
	+ SVerticalBox::Slot()
	.AutoHeight()
	.Padding(0, 4)
	[
		MakeSettingRow(
			LOCTEXT("PlatformLabel", "目标平台:"),
			SAssignNew(PlatformComboBox, SComboBox<TSharedPtr<EHotUpdatePlatform>>)
			.OptionsSource(&PlatformOptions)
			.OnGenerateWidget(this, &SHotUpdateBaseVersionPanel::GeneratePlatformComboBoxItem)
			.OnSelectionChanged(this, &SHotUpdateBaseVersionPanel::OnPlatformSelected)
			.InitiallySelectedItem(SelectedPlatform)
			[
				SNew(STextBlock)
				.Text(this, &SHotUpdateBaseVersionPanel::GetSelectedPlatformText)
				.Font(FHotUpdateEditorStyle::GetNormalFont())
			]
		)
	]
	// 输出目录
	+ SVerticalBox::Slot()
	.AutoHeight()
	.Padding(0, 4)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(100)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("OutputDirLabel", "输出目录:"))
				.Font(FHotUpdateEditorStyle::GetNormalFont())
				.ColorAndOpacity(FHotUpdateEditorStyle::GetTextSecondaryColor())
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.Padding(8, 4)
		.VAlign(VAlign_Center)
		[
			SAssignNew(OutputDirTextBox, SEditableText)
			.Text(FText::FromString(BuildConfig.OutputDirectory))
			.IsReadOnly(true)
			.Font(FHotUpdateEditorStyle::GetNormalFont())
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4, 0)
		[
			SNew(SButton)
			.Text(LOCTEXT("Browse", "浏览"))
			.ButtonStyle(FAppStyle::Get(), "FlatButton")
			.OnClicked(this, &SHotUpdateBaseVersionPanel::OnBrowseOutputDirectory)
		]
	]
	// 分隔
	+ SVerticalBox::Slot()
	.AutoHeight()
	.Padding(0, 12)
	[
		SNew(SSeparator)
	]
	// 构建配置
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 4)
		[
			MakeSettingRow(
				LOCTEXT("BuildConfigLabel", "构建配置:"),
				SAssignNew(BuildConfigComboBox, SComboBox<TSharedPtr<EHotUpdateBuildConfiguration>>)
				.OptionsSource(&BuildConfigOptions)
				.OnGenerateWidget(this, &SHotUpdateBaseVersionPanel::GenerateBuildConfigComboBoxItem)
				.OnSelectionChanged(this, &SHotUpdateBaseVersionPanel::OnBuildConfigSelected)
				.InitiallySelectedItem(SelectedBuildConfig)
				[
					SNew(STextBlock)
					.Text(this, &SHotUpdateBaseVersionPanel::GetSelectedBuildConfigText)
					.Font(FHotUpdateEditorStyle::GetNormalFont())
				]
			)
		]
		// Android 纹理格式（仅 Android 平台可见）
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 4)
		[
			SNew(SBorder)
			.Visibility(this, &SHotUpdateBaseVersionPanel::GetAndroidTextureFormatVisibility)
			.BorderBackgroundColor(FLinearColor::Transparent)
			.Padding(0)
			[
				MakeSettingRow(
					LOCTEXT("AndroidTextureFormatLabel", "纹理格式:"),
					SAssignNew(AndroidTextureFormatComboBox, SComboBox<TSharedPtr<EHotUpdateAndroidTextureFormat>>)
					.OptionsSource(&AndroidTextureFormatOptions)
					.OnGenerateWidget(this, &SHotUpdateBaseVersionPanel::GenerateAndroidTextureFormatComboBoxItem)
					.OnSelectionChanged(this, &SHotUpdateBaseVersionPanel::OnAndroidTextureFormatSelected)
					.InitiallySelectedItem(SelectedAndroidTextureFormat)
					[
						SNew(STextBlock)
						.Text(this, &SHotUpdateBaseVersionPanel::GetSelectedAndroidTextureFormatText)
						.Font(FHotUpdateEditorStyle::GetNormalFont())
					]
				)
			]
		]
	// 跳过编译选项（解决 Live Coding 冲突）
	+ SVerticalBox::Slot()
	.AutoHeight()
	.Padding(0, 4)
	[
		SAssignNew(SkipBuildCheckBox, SCheckBox)
		.IsChecked(BuildConfig.bSkipBuild ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
		.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) {
			BuildConfig.bSkipBuild = (NewState == ECheckBoxState::Checked);
		})
		.ToolTipText(LOCTEXT("SkipBuildTooltip", "如果项目已编译，跳过编译步骤可以避免编辑器运行时的 Live Coding 冲突"))
		[
			SNew(STextBlock)
			.Text(LOCTEXT("SkipBuild", "跳过编译 (编辑器运行时需要)"))
			.Font(FHotUpdateEditorStyle::GetNormalFont())
		]
	]
	// 分隔线
	+ SVerticalBox::Slot()
	.AutoHeight()
	.Padding(0, 12)
	[
		SNew(SSeparator)
	]
	// 最小包配置区域
	+ SVerticalBox::Slot()
	.AutoHeight()
	.Padding(0, 4)
	[
		CreateMinimalPackageSettings()
	];
}

TSharedRef<SWidget> SHotUpdateBaseVersionPanel::CreateProgressSection()
{
	return SNew(SVerticalBox)
	// 状态信息
	+ SVerticalBox::Slot()
	.AutoHeight()
	[
		SAssignNew(StatusTextBlock, STextBlock)
		.Text(LOCTEXT("ReadyStatus", "准备就绪"))
		.ColorAndOpacity(FHotUpdateEditorStyle::GetSuccessColor())
		.Font(FHotUpdateEditorStyle::GetNormalFont())
	]
	// 进度条
	+ SVerticalBox::Slot()
	.AutoHeight()
	.Padding(0, 8, 0, 12)
	[
		SAssignNew(ProgressBar, SProgressBar)
		.Percent(0.0f)
		.FillColorAndOpacity(FHotUpdateEditorStyle::GetPrimaryColor())
	]
	// 操作按钮
	+ SVerticalBox::Slot()
	.AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew(SSpacer)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4, 0)
		[
			SAssignNew(CancelButton, SButton)
			.Text(LOCTEXT("Cancel", "取消"))
			.ButtonStyle(FAppStyle::Get(), "Button")
			.IsEnabled(this, &SHotUpdateBaseVersionPanel::CanBuild)
			.Visibility_Lambda([this]() { return bIsBuilding ? EVisibility::Visible : EVisibility::Collapsed; })
			.OnClicked(this, &SHotUpdateBaseVersionPanel::OnCancelClicked)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4, 0)
		[
			SAssignNew(BuildButton, SButton)
			.Text(LOCTEXT("Build", "开始构建"))
			.ToolTipText(LOCTEXT("BuildTooltip", "开始构建基础版本"))
			.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
			.IsEnabled(this, &SHotUpdateBaseVersionPanel::CanBuild)
			.OnClicked(this, &SHotUpdateBaseVersionPanel::OnBuildClicked)
		]
	];
}

FReply SHotUpdateBaseVersionPanel::OnBuildClicked()
{
	if (bIsBuilding)
	{
		return FReply::Handled();
	}

	// 更新配置
	if (VersionTextBox.IsValid())
	{
		BuildConfig.VersionString = VersionTextBox->GetText().ToString();
	}

	if (OutputDirTextBox.IsValid())
	{
		BuildConfig.OutputDirectory = OutputDirTextBox->GetText().ToString();
	}

	if (SelectedPlatform.IsValid())
	{
		BuildConfig.Platform = *SelectedPlatform;
	}

		if (SelectedAndroidTextureFormat.IsValid())
		{
			BuildConfig.AndroidTextureFormat = *SelectedAndroidTextureFormat;
		}
	// 验证版本号
	if (BuildConfig.VersionString.IsEmpty())
	{
		StatusTextBlock->SetText(LOCTEXT("ErrorNoVersion", "请输入版本号"));
		StatusTextBlock->SetColorAndOpacity(FHotUpdateEditorStyle::GetErrorColor());
		return FReply::Handled();
	}

	// 开始构建
	bIsBuilding = true;
	StatusTextBlock->SetText(LOCTEXT("BuildingStatus", "正在构建..."));
	StatusTextBlock->SetColorAndOpacity(FHotUpdateEditorStyle::GetProcessingColor());
	ProgressBar->SetPercent(0.0f);

	Builder->BuildBaseVersion(BuildConfig);

	return FReply::Handled();
}

FReply SHotUpdateBaseVersionPanel::OnCancelClicked()
{
	if (bIsBuilding)
	{
		Builder->CancelBuild();
		bIsBuilding = false;
		StatusTextBlock->SetText(LOCTEXT("CancelledStatus", "已取消"));
		StatusTextBlock->SetColorAndOpacity(FHotUpdateEditorStyle::GetWarningColor());
	}
	return FReply::Handled();
}

bool SHotUpdateBaseVersionPanel::CanBuild() const
{
	return !bIsBuilding;
}

void SHotUpdateBaseVersionPanel::OnBuildProgress(const FHotUpdateBaseVersionBuildProgress& Progress)
{
	AsyncTask(ENamedThreads::GameThread, [this, Progress]()
	{
		FString StatusText = FString::Printf(TEXT("%s - %s"),
			*Progress.CurrentStage, *Progress.StatusMessage);
		StatusTextBlock->SetText(FText::FromString(StatusText));
		ProgressBar->SetPercent(Progress.ProgressPercent);
	});
}

void SHotUpdateBaseVersionPanel::OnBuildComplete(const FHotUpdateBaseVersionBuildResult& Result)
{
	bIsBuilding = false;

	AsyncTask(ENamedThreads::GameThread, [this, Result]()
	{
		if (Result.bSuccess)
		{
			FString SuccessMsg = FString::Printf(
				TEXT("构建成功!\n版本: %s\n输出: %s"),
				*Result.VersionString,
				*Result.ExecutablePath
			);
			StatusTextBlock->SetText(FText::FromString(SuccessMsg));
			StatusTextBlock->SetColorAndOpacity(FHotUpdateEditorStyle::GetSuccessColor());
			ProgressBar->SetPercent(1.0f);

			ShowNotification(FText::FromString(FString::Printf(TEXT("基础版本构建成功: %s"), *Result.VersionString)),
				SNotificationItem::CS_Success);
		}
		else
		{
			StatusTextBlock->SetText(FText::FromString(Result.ErrorMessage));
			StatusTextBlock->SetColorAndOpacity(FHotUpdateEditorStyle::GetErrorColor());
			ProgressBar->SetPercent(0.0f);

			ShowNotification(FText::FromString(Result.ErrorMessage), SNotificationItem::CS_Fail);
		}
	});
}

FReply SHotUpdateBaseVersionPanel::OnBrowseOutputDirectory()
{
	TSharedPtr<SWindow> ParentWindowPtr = ParentWindow.IsValid() ? ParentWindow : FSlateApplication::Get().FindBestParentWindowForDialogs(nullptr);

	void* ParentWindowHandle = ParentWindowPtr.IsValid() ? ParentWindowPtr->GetNativeWindow()->GetOSWindowHandle() : nullptr;

	FString SelectedDirectory;
	if (FDesktopPlatformModule::Get()->OpenDirectoryDialog(
		ParentWindowHandle,
		LOCTEXT("SelectOutputDir", "选择输出目录").ToString(),
		BuildConfig.OutputDirectory,
		SelectedDirectory))
	{
		BuildConfig.OutputDirectory = SelectedDirectory;
		if (OutputDirTextBox.IsValid())
		{
			OutputDirTextBox->SetText(FText::FromString(SelectedDirectory));
		}
	}

	return FReply::Handled();
}

void SHotUpdateBaseVersionPanel::RefreshBuildState()
{
}

void SHotUpdateBaseVersionPanel::ShowNotification(const FText& Message, SNotificationItem::ECompletionState State)
{
	TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(
		FNotificationInfo(Message)
	);

	if (NotificationItem.IsValid())
	{
		NotificationItem->SetCompletionState(State);
	}
}

TSharedRef<SWidget> SHotUpdateBaseVersionPanel::GeneratePlatformComboBoxItem(TSharedPtr<EHotUpdatePlatform> InItem)
{
	FText PlatformText;
	switch (*InItem)
	{
	case EHotUpdatePlatform::Windows:
		PlatformText = LOCTEXT("PlatformWindows", "Windows (PC)");
		break;
	case EHotUpdatePlatform::Android:
		PlatformText = LOCTEXT("PlatformAndroid", "Android");
		break;
	case EHotUpdatePlatform::IOS:
		PlatformText = LOCTEXT("PlatformIOS", "iOS");
		break;
	}

	return SNew(STextBlock)
		.Text(PlatformText)
		.Font(FHotUpdateEditorStyle::GetNormalFont())
		.Margin(FMargin(4, 2));
}

void SHotUpdateBaseVersionPanel::OnPlatformSelected(TSharedPtr<EHotUpdatePlatform> InItem, ESelectInfo::Type SelectInfo)
{
	SelectedPlatform = InItem;
	if (InItem.IsValid())
	{
		BuildConfig.Platform = *InItem;
	}
}

FText SHotUpdateBaseVersionPanel::GetSelectedPlatformText() const
{
	if (SelectedPlatform.IsValid())
	{
		switch (*SelectedPlatform)
		{
		case EHotUpdatePlatform::Windows:
			return LOCTEXT("PlatformWindows", "Windows (PC)");
		case EHotUpdatePlatform::Android:
			return LOCTEXT("PlatformAndroid", "Android");
		case EHotUpdatePlatform::IOS:
			return LOCTEXT("PlatformIOS", "iOS");
		}
	}
	return LOCTEXT("PlatformWindows", "Windows (PC)");
}

TSharedRef<SWidget> SHotUpdateBaseVersionPanel::GenerateBuildConfigComboBoxItem(TSharedPtr<EHotUpdateBuildConfiguration> InItem)
{
	FText ConfigText;
	switch (*InItem)
	{
	case EHotUpdateBuildConfiguration::DebugGame:
		ConfigText = LOCTEXT("BuildConfigDebugGame", "DebugGame (包含调试信息)");
		break;
	case EHotUpdateBuildConfiguration::Development:
		ConfigText = LOCTEXT("BuildConfigDevelopment", "Development");
		break;
	case EHotUpdateBuildConfiguration::Shipping:
		ConfigText = LOCTEXT("BuildConfigShipping", "Shipping (发布构建)");
		break;
	}

	return SNew(STextBlock)
		.Text(ConfigText)
		.Font(FHotUpdateEditorStyle::GetNormalFont())
		.Margin(FMargin(4, 2));
}

void SHotUpdateBaseVersionPanel::OnBuildConfigSelected(TSharedPtr<EHotUpdateBuildConfiguration> InItem, ESelectInfo::Type SelectInfo)
{
	SelectedBuildConfig = InItem;
	if (InItem.IsValid())
	{
		BuildConfig.BuildConfiguration = *InItem;
	}
}

FText SHotUpdateBaseVersionPanel::GetSelectedBuildConfigText() const
{
	if (SelectedBuildConfig.IsValid())
	{
		switch (*SelectedBuildConfig)
		{
		case EHotUpdateBuildConfiguration::DebugGame:
			return LOCTEXT("BuildConfigDebugGame", "DebugGame (包含调试信息)");
		case EHotUpdateBuildConfiguration::Development:
			return LOCTEXT("BuildConfigDevelopment", "Development");
		case EHotUpdateBuildConfiguration::Shipping:
			return LOCTEXT("BuildConfigShipping", "Shipping (发布构建)");
		}
	}
	return LOCTEXT("BuildConfigDevelopment", "Development");
}

TSharedRef<SWidget> SHotUpdateBaseVersionPanel::CreateMinimalPackageSettings()
{
	return SNew(SExpandableArea)
		.Style(FAppStyle::Get(), "ExpandableArea")
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.HeaderPadding(FMargin(0, 4))
		.AllowAnimatedTransition(true)
		.HeaderContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("MinimalPackageSettings", "最小包配置"))
			.Font(FAppStyle::GetFontStyle("NormalFontBold"))
			.ColorAndOpacity(FLinearColor::Gray)
		]
		.BodyContent()
		[
			SNew(SVerticalBox)
			// 启用最小包模式
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 2)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() {
					return BuildConfig.MinimalPackageConfig.bEnableMinimalPackage
						? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) {
					BuildConfig.MinimalPackageConfig.bEnableMinimalPackage =
						(NewState == ECheckBoxState::Checked);
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("EnableMinimalPackage", "启用最小包模式"))
				]
			]
			// 依赖策略
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 2)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(90)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("DependencyStrategyLabel", "依赖策略:"))
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(8, 0)
				[
					SAssignNew(DependencyStrategyComboBox, SComboBox<TSharedPtr<EHotUpdateDependencyStrategy>>)
					.OptionsSource(&DependencyStrategyOptions)
					.OnGenerateWidget(this, &SHotUpdateBaseVersionPanel::GenerateDependencyStrategyComboBoxItem)
					.OnSelectionChanged(this, &SHotUpdateBaseVersionPanel::OnDependencyStrategySelected)
					.InitiallySelectedItem(SelectedDependencyStrategy)
					[
						SNew(STextBlock)
						.Text(this, &SHotUpdateBaseVersionPanel::GetSelectedDependencyStrategyText)
					]
				]
			]
			// 分包策略
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 2)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(90)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("PatchChunkStrategyLabel", "分包策略:"))
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(8, 0)
				[
					SAssignNew(PatchChunkStrategyComboBox, SComboBox<TSharedPtr<EHotUpdateChunkStrategy>>)
					.OptionsSource(&PatchChunkStrategyOptions)
					.OnGenerateWidget(this, &SHotUpdateBaseVersionPanel::GeneratePatchChunkStrategyComboBoxItem)
					.OnSelectionChanged(this, &SHotUpdateBaseVersionPanel::OnPatchChunkStrategySelected)
					[
						SNew(STextBlock)
						.Text(this, &SHotUpdateBaseVersionPanel::GetSelectedPatchChunkStrategyText)
					]
				]
			]
			// 必须包含的目录
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 4)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("RequiredDirectories", "必须包含的目录:"))
					.Font(FAppStyle::GetFontStyle("NormalFontBold"))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 4)
				[
					SNew(SButton)
					.Text(LOCTEXT("AddWhitelistDirectory", "添加目录"))
					.ButtonStyle(FAppStyle::Get(), "FlatButton")
					.OnClicked(this, &SHotUpdateBaseVersionPanel::OnAddWhitelistDirectoryClicked)
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				.MaxHeight(100.0f)
				[
					SAssignNew(WhitelistDirectoryListView, SListView<TSharedPtr<FDirectoryPath>>)
					.ListItemsSource(&WhitelistDirectoryItems)
					.OnGenerateRow(this, &SHotUpdateBaseVersionPanel::GenerateWhitelistDirectoryRow)
					.SelectionMode(ESelectionMode::Single)
				]
			]
		];
}

TSharedRef<ITableRow> SHotUpdateBaseVersionPanel::GenerateWhitelistDirectoryRow(TSharedPtr<FDirectoryPath> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FDirectoryPath>>, OwnerTable)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(InItem.IsValid() ? InItem->Path : TEXT("")))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("Remove", "删除"))
				.ButtonStyle(FAppStyle::Get(), "FlatButton")
				.OnClicked(this, &SHotUpdateBaseVersionPanel::OnRemoveWhitelistDirectoryClicked, InItem)
			]
		];
}

FReply SHotUpdateBaseVersionPanel::OnAddWhitelistDirectoryClicked()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		TSharedPtr<SWindow> ParentWindowPtr = ParentWindow.IsValid() ? ParentWindow : FSlateApplication::Get().FindBestParentWindowForDialogs(nullptr);
		void* ParentWindowHandle = ParentWindowPtr.IsValid() ? ParentWindowPtr->GetNativeWindow()->GetOSWindowHandle() : nullptr;

		FString SelectedDirectory;
		if (DesktopPlatform->OpenDirectoryDialog(ParentWindowHandle, LOCTEXT("SelectWhitelistDirectory", "选择必须包含的目录").ToString(), TEXT(""), SelectedDirectory))
		{
			// 转换为相对路径
			FString RelativePath = SelectedDirectory;
			FPaths::MakePathRelativeTo(RelativePath, *FPaths::ProjectContentDir());
			if (!RelativePath.StartsWith(TEXT("..")))
			{
				FString GamePath = FString::Printf(TEXT("/Game/%s"), *RelativePath);
				TSharedPtr<FDirectoryPath> NewDir = MakeShareable(new FDirectoryPath());
				NewDir->Path = GamePath;
				WhitelistDirectoryItems.Add(NewDir);
				BuildConfig.MinimalPackageConfig.WhitelistDirectories.Add(*NewDir);
				WhitelistDirectoryListView->RequestListRefresh();
			}
		}
	}
	return FReply::Handled();
}

FReply SHotUpdateBaseVersionPanel::OnRemoveWhitelistDirectoryClicked(TSharedPtr<FDirectoryPath> InItem)
{
	if (InItem.IsValid())
	{
		WhitelistDirectoryItems.Remove(InItem);
		BuildConfig.MinimalPackageConfig.WhitelistDirectories.RemoveAll([&](const FDirectoryPath& Dir) {
			return Dir.Path == InItem->Path;
		});
		WhitelistDirectoryListView->RequestListRefresh();
	}
	return FReply::Handled();
}

TSharedRef<SWidget> SHotUpdateBaseVersionPanel::GenerateDependencyStrategyComboBoxItem(TSharedPtr<EHotUpdateDependencyStrategy> InItem)
{
	FText StrategyText;
	switch (*InItem)
	{
	case EHotUpdateDependencyStrategy::IncludeAll:
		StrategyText = LOCTEXT("DependencyIncludeAll", "包含所有依赖");
		break;
	case EHotUpdateDependencyStrategy::HardOnly:
		StrategyText = LOCTEXT("DependencyHardOnly", "仅硬依赖");
		break;
	case EHotUpdateDependencyStrategy::SoftOnly:
		StrategyText = LOCTEXT("DependencySoftOnly", "仅软依赖");
		break;
	case EHotUpdateDependencyStrategy::None:
		StrategyText = LOCTEXT("DependencyNone", "不包含依赖");
		break;
	}
	return SNew(STextBlock)
		.Text(StrategyText)
		.Font(FHotUpdateEditorStyle::GetNormalFont())
		.Margin(FMargin(4, 2));
}

void SHotUpdateBaseVersionPanel::OnDependencyStrategySelected(TSharedPtr<EHotUpdateDependencyStrategy> InItem, ESelectInfo::Type SelectInfo)
{
	SelectedDependencyStrategy = InItem;
	if (InItem.IsValid())
	{
		BuildConfig.MinimalPackageConfig.DependencyStrategy = *InItem;
	}
}

FText SHotUpdateBaseVersionPanel::GetSelectedDependencyStrategyText() const
{
	if (SelectedDependencyStrategy.IsValid())
	{
		switch (*SelectedDependencyStrategy)
		{
		case EHotUpdateDependencyStrategy::IncludeAll:
			return LOCTEXT("DependencyIncludeAll", "包含所有依赖");
		case EHotUpdateDependencyStrategy::HardOnly:
			return LOCTEXT("DependencyHardOnly", "仅硬依赖");
		case EHotUpdateDependencyStrategy::SoftOnly:
			return LOCTEXT("DependencySoftOnly", "仅软依赖");
		case EHotUpdateDependencyStrategy::None:
			return LOCTEXT("DependencyNone", "不包含依赖");
		}
	}
	return LOCTEXT("DependencyHardOnly", "仅硬依赖");
}

TSharedRef<SWidget> SHotUpdateBaseVersionPanel::GeneratePatchChunkStrategyComboBoxItem(TSharedPtr<EHotUpdateChunkStrategy> InItem)
{
	FText StrategyText;
	switch (*InItem)
	{
	case EHotUpdateChunkStrategy::None:
		StrategyText = LOCTEXT("ChunkStrategyNone", "不分包（全部一个Chunk）");
		break;
	case EHotUpdateChunkStrategy::Size:
		StrategyText = LOCTEXT("ChunkStrategySize", "按大小分包");
		break;
	case EHotUpdateChunkStrategy::Directory:
		StrategyText = LOCTEXT("ChunkStrategyDirectory", "按目录分包");
		break;
		break;
	case EHotUpdateChunkStrategy::PrimaryAsset:
		StrategyText = LOCTEXT("ChunkStrategyPrimaryAsset", "UE5标准分包");
		break;
	case EHotUpdateChunkStrategy::Hybrid:
		StrategyText = LOCTEXT("ChunkStrategyHybrid", "混合模式");
		break;
	}
	return SNew(STextBlock)
		.Text(StrategyText)
		.Font(FHotUpdateEditorStyle::GetNormalFont())
		.Margin(FMargin(4, 2));
}

void SHotUpdateBaseVersionPanel::OnPatchChunkStrategySelected(TSharedPtr<EHotUpdateChunkStrategy> InItem, ESelectInfo::Type SelectInfo)
{
	SelectedPatchChunkStrategy = InItem;
	if (InItem.IsValid())
	{
		BuildConfig.MinimalPackageConfig.PatchChunkStrategy = *InItem;
	}
}

FText SHotUpdateBaseVersionPanel::GetSelectedPatchChunkStrategyText() const
{
	if (SelectedPatchChunkStrategy.IsValid())
	{
		switch (*SelectedPatchChunkStrategy)
		{
		case EHotUpdateChunkStrategy::None:
			return LOCTEXT("ChunkStrategyNone", "不分包（全部一个Chunk）");
		case EHotUpdateChunkStrategy::Size:
			return LOCTEXT("ChunkStrategySize", "按大小分包");
		case EHotUpdateChunkStrategy::Directory:
			return LOCTEXT("ChunkStrategyDirectory", "按目录分包");
		case EHotUpdateChunkStrategy::PrimaryAsset:
			return LOCTEXT("ChunkStrategyPrimaryAsset", "UE5标准分包");
		case EHotUpdateChunkStrategy::Hybrid:
			return LOCTEXT("ChunkStrategyHybrid", "混合模式");
		}
	}
	return LOCTEXT("ChunkStrategyNone", "不分包（全部一个Chunk）");
}

TSharedRef<SWidget> SHotUpdateBaseVersionPanel::GenerateAndroidTextureFormatComboBoxItem(TSharedPtr<EHotUpdateAndroidTextureFormat> InItem)
{
	FText FormatText;
	switch (*InItem)
	{
	case EHotUpdateAndroidTextureFormat::ETC2:
		FormatText = LOCTEXT("TextureFormatETC2", "ETC2");
		break;
	case EHotUpdateAndroidTextureFormat::ASTC:
		FormatText = LOCTEXT("TextureFormatASTC", "ASTC");
		break;
	case EHotUpdateAndroidTextureFormat::DXT:
		FormatText = LOCTEXT("TextureFormatDXT", "DXT");
		break;
	case EHotUpdateAndroidTextureFormat::Multi:
		FormatText = LOCTEXT("TextureFormatMulti", "Multi");
		break;
	}
	return SNew(STextBlock)
		.Text(FormatText)
		.Font(FHotUpdateEditorStyle::GetNormalFont())
		.Margin(FMargin(4, 2));
}

void SHotUpdateBaseVersionPanel::OnAndroidTextureFormatSelected(TSharedPtr<EHotUpdateAndroidTextureFormat> InItem, ESelectInfo::Type SelectInfo)
{
	SelectedAndroidTextureFormat = InItem;
	if (InItem.IsValid())
	{
		BuildConfig.AndroidTextureFormat = *InItem;
	}
}

FText SHotUpdateBaseVersionPanel::GetSelectedAndroidTextureFormatText() const
{
	if (SelectedAndroidTextureFormat.IsValid())
	{
		switch (*SelectedAndroidTextureFormat)
		{
		case EHotUpdateAndroidTextureFormat::ETC2:
			return LOCTEXT("TextureFormatETC2", "ETC2");
		case EHotUpdateAndroidTextureFormat::ASTC:
			return LOCTEXT("TextureFormatASTC", "ASTC");
		case EHotUpdateAndroidTextureFormat::DXT:
			return LOCTEXT("TextureFormatDXT", "DXT");
		case EHotUpdateAndroidTextureFormat::Multi:
			return LOCTEXT("TextureFormatMulti", "Multi");
		}
	}
	return LOCTEXT("TextureFormatETC2", "ETC2");
}

EVisibility SHotUpdateBaseVersionPanel::GetAndroidTextureFormatVisibility() const
{
	if (SelectedPlatform.IsValid() && *SelectedPlatform == EHotUpdatePlatform::Android)
	{
		return EVisibility::Visible;
	}
	return EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE