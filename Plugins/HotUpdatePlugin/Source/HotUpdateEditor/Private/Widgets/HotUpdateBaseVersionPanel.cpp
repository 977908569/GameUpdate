// Copyright czm. All Rights Reserved.

#include "Widgets/HotUpdateBaseVersionPanel.h"
#include "HotUpdateEditorStyle.h"
#include "HotUpdateNotificationHelper.h"
#include "HotUpdateBaseVersionBuilder.h"
#include "HotUpdateUtils.h"
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
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "HotUpdateBaseVersionPanel"

void SHotUpdateBaseVersionPanel::Construct(const FArguments& InArgs)
{
	ParentWindow = InArgs._ParentWindow;
	bIsPackaging = false;

	InitPlatformOptions();
	InitAndroidTextureFormatOptions();

	// 初始化构建配置选项
	BuildConfigOptions.Add(MakeShareable(new EHotUpdateBuildConfiguration(EHotUpdateBuildConfiguration::DebugGame)));
	BuildConfigOptions.Add(MakeShareable(new EHotUpdateBuildConfiguration(EHotUpdateBuildConfiguration::Development)));
	BuildConfigOptions.Add(MakeShareable(new EHotUpdateBuildConfiguration(EHotUpdateBuildConfiguration::Shipping)));
	SelectedBuildConfig = BuildConfigOptions[1];

	// 初始化依赖策略选项
	DependencyStrategyOptions.Add(MakeShareable(new EHotUpdateDependencyStrategy(EHotUpdateDependencyStrategy::IncludeAll)));
	DependencyStrategyOptions.Add(MakeShareable(new EHotUpdateDependencyStrategy(EHotUpdateDependencyStrategy::HardOnly)));
	DependencyStrategyOptions.Add(MakeShareable(new EHotUpdateDependencyStrategy(EHotUpdateDependencyStrategy::SoftOnly)));
	DependencyStrategyOptions.Add(MakeShareable(new EHotUpdateDependencyStrategy(EHotUpdateDependencyStrategy::None)));
	SelectedDependencyStrategy = DependencyStrategyOptions[1];

	// 分包策略选项初始化
	PatchChunkStrategyOptions.Add(MakeShareable(new EHotUpdateChunkStrategy(EHotUpdateChunkStrategy::None)));
	PatchChunkStrategyOptions.Add(MakeShareable(new EHotUpdateChunkStrategy(EHotUpdateChunkStrategy::Size)));
	SelectedPatchChunkStrategy = PatchChunkStrategyOptions[0];

	// 创建构建器
	Builder = MakeShareable(new FHotUpdateBaseVersionBuilder());
	Builder->OnBuildProgress.AddSP(this, &SHotUpdateBaseVersionPanel::OnBuildProgress);
	Builder->OnBuildComplete.AddSP(this, &SHotUpdateBaseVersionPanel::OnBuildComplete);

	// 默认输出目录
	BuildConfig.OutputDirectory = FHotUpdateBaseVersionBuilder::GetDefaultOutputDirectory();

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

SHotUpdateBaseVersionPanel::~SHotUpdateBaseVersionPanel()
{
	if (Builder.IsValid())
	{
		Builder->OnBuildProgress.RemoveAll(this);
		Builder->OnBuildComplete.RemoveAll(this);
		Builder.Reset();
	}
}

TSharedRef<SWidget> SHotUpdateBaseVersionPanel::CreateConfigSection()
{
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
			.Font(FHotUpdateEditorStyle::GetNormalFont()),
			100.0f
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
			],
			100.0f
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
				],
				100.0f
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
					],
					100.0f
				)
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
			.Visibility_Lambda([this]() { return bIsPackaging ? EVisibility::Visible : EVisibility::Collapsed; })
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
	if (bIsPackaging)
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
	bIsPackaging = true;
	StatusTextBlock->SetText(LOCTEXT("BuildingStatus", "正在构建..."));
	StatusTextBlock->SetColorAndOpacity(FHotUpdateEditorStyle::GetProcessingColor());
	ProgressBar->SetPercent(0.0f);

	Builder->BuildBaseVersion(BuildConfig);

	return FReply::Handled();
}

FReply SHotUpdateBaseVersionPanel::OnCancelClicked()
{
	if (bIsPackaging)
	{
		CancelCurrentBuild();
		bIsPackaging = false;
		SetStatusText(LOCTEXT("CancelledStatus", "已取消").ToString(), FHotUpdateEditorStyle::GetWarningColor());
		ResetProgressUI();
	}
	return FReply::Handled();
}

void SHotUpdateBaseVersionPanel::CancelCurrentBuild()
{
	if (Builder && Builder->IsBuilding())
	{
		Builder->CancelBuild();
	}
}

bool SHotUpdateBaseVersionPanel::CanBuild() const
{
	return !bIsPackaging;
}

void SHotUpdateBaseVersionPanel::OnBuildProgress(const FHotUpdateBaseVersionBuildProgress& Progress)
{
	FString StatusText = FString::Printf(TEXT("%s - %s"),
		*Progress.CurrentStage, *Progress.StatusMessage);
	StatusTextBlock->SetText(FText::FromString(StatusText));
	ProgressBar->SetPercent(Progress.ProgressPercent);
}

void SHotUpdateBaseVersionPanel::OnBuildComplete(const FHotUpdateBaseVersionBuildResult& Result)
{
	bIsPackaging = false;

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

		FHotUpdateNotificationHelper::ShowNotification(FText::FromString(FString::Printf(TEXT("基础版本构建成功: %s"), *Result.VersionString)),
			SNotificationItem::CS_Success);
	}
	else
	{
		StatusTextBlock->SetText(FText::FromString(Result.ErrorMessage));
		StatusTextBlock->SetColorAndOpacity(FHotUpdateEditorStyle::GetErrorColor());
		ProgressBar->SetPercent(0.0f);

		FHotUpdateNotificationHelper::ShowNotification(FText::FromString(Result.ErrorMessage), SNotificationItem::CS_Fail);
	}
}

FReply SHotUpdateBaseVersionPanel::OnBrowseOutputDirectory()
{
	return BrowseOutputDirectory(BuildConfig.OutputDirectory, OutputDirTextBox, BuildConfig.OutputDirectory);
}

TSharedRef<SWidget> SHotUpdateBaseVersionPanel::GenerateBuildConfigComboBoxItem(TSharedPtr<EHotUpdateBuildConfiguration> InItem)
{
	return SNew(STextBlock)
		.Text(HotUpdateUtils::GetBuildConfigDisplayName(*InItem))
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
		return HotUpdateUtils::GetBuildConfigDisplayName(*SelectedBuildConfig);
	}
	return HotUpdateUtils::GetBuildConfigDisplayName(EHotUpdateBuildConfiguration::Development);
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
				SAssignNew(EnableMinimalPackageCheckBox, SCheckBox)
				.IsChecked_Lambda([this]() {
					return BuildConfig.MinimalPackageConfig.bEnableMinimalPackage ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) {
					BuildConfig.MinimalPackageConfig.bEnableMinimalPackage = NewState == ECheckBoxState::Checked;
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("EnableMinimalPackage", "启用最小包模式"))
				]
			]
			// 条件可见的子配置项
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.Visibility(this, &SHotUpdateBaseVersionPanel::GetMinimalPackageSettingsVisibility)
				.BorderBackgroundColor(FLinearColor::Transparent)
				.Padding(0)
				[
					SNew(SVerticalBox)
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
					// 按大小分包的最大 Chunk 大小
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 2)
					[
						SNew(SHorizontalBox)
						.Visibility_Lambda([this]() {
							return SelectedPatchChunkStrategy.IsValid() && *SelectedPatchChunkStrategy == EHotUpdateChunkStrategy::Size
								? EVisibility::Visible : EVisibility::Collapsed;
						})
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SBox)
							.WidthOverride(90)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("MaxChunkSizeLabel", "最大大小(MB):"))
							]
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(8, 0)
						.VAlign(VAlign_Center)
						[
							SAssignNew(MaxChunkSizeSpinBox, SSpinBox<float>)
							.Value(this, &SHotUpdateBaseVersionPanel::GetMaxChunkSizeValue)
							.MinValue(1)
							.MaxValue(2048)
							.MinSliderValue(1)
							.MaxSliderValue(512)
							.SliderExponent(1.0f)
							.OnValueChanged_Lambda([this](float NewValue) {
								BuildConfig.MinimalPackageConfig.PatchChunkConfig.SizeBasedConfig.MaxChunkSizeMB = FMath::RoundToInt(NewValue);
							})
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(8, 0)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("MaxChunkSizeHint", "每个Chunk的最大大小（MB）"))
							.ColorAndOpacity(FLinearColor::Gray)
							.Font(FAppStyle::GetFontStyle("SmallFont"))
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
	if (IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get())
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
	return SNew(STextBlock)
		.Text(HotUpdateUtils::GetDependencyStrategyDisplayName(*InItem))
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
		return HotUpdateUtils::GetDependencyStrategyDisplayName(*SelectedDependencyStrategy);
	}
	return HotUpdateUtils::GetDependencyStrategyDisplayName(EHotUpdateDependencyStrategy::HardOnly);
}

TSharedRef<SWidget> SHotUpdateBaseVersionPanel::GeneratePatchChunkStrategyComboBoxItem(TSharedPtr<EHotUpdateChunkStrategy> InItem)
{
	return SNew(STextBlock)
		.Text(HotUpdateUtils::GetChunkStrategyDisplayName(*InItem))
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
		return HotUpdateUtils::GetChunkStrategyDisplayName(*SelectedPatchChunkStrategy);
	}
	return HotUpdateUtils::GetChunkStrategyDisplayName(EHotUpdateChunkStrategy::None);
}

float SHotUpdateBaseVersionPanel::GetMaxChunkSizeValue() const
{
	return static_cast<float>(BuildConfig.MinimalPackageConfig.PatchChunkConfig.SizeBasedConfig.MaxChunkSizeMB);
}

EVisibility SHotUpdateBaseVersionPanel::GetMinimalPackageSettingsVisibility() const
{
	return BuildConfig.MinimalPackageConfig.bEnableMinimalPackage ? EVisibility::Visible : EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE