// Copyright czm. All Rights Reserved.

#include "Widgets/HotUpdatePackagingPanel.h"
#include "HotUpdateEditor.h"
#include "HotUpdateEditorSettings.h"
#include "HotUpdateEditorStyle.h"
#include "HotUpdateNotificationHelper.h"
#include "HotUpdatePatchPackageBuilder.h"
#include "HotUpdateVersionManager.h"
#include "HotUpdateUtils.h"
#include "Styling/AppStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Dialogs/Dialogs.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "HotUpdatePackagingPanel"

void SHotUpdatePackagingPanel::Construct(const FArguments& InArgs)
{
	UE_LOG(LogHotUpdateEditor, Log, TEXT("SHotUpdatePackagingPanel::Construct 开始"));

	ParentWindow = InArgs._ParentWindow;
	bIsPackaging = false;

	InitPlatformOptions();
	InitAndroidTextureFormatOptions();

	// 初始化分包策略选项
	ChunkStrategyOptions.Add(MakeShareable(new EHotUpdateChunkStrategy(EHotUpdateChunkStrategy::None)));
	ChunkStrategyOptions.Add(MakeShareable(new EHotUpdateChunkStrategy(EHotUpdateChunkStrategy::Size)));
	SelectedChunkStrategy = ChunkStrategyOptions[1]; // 默认 Size

	// 初始化版本选择选项
	RefreshVersionSelectOptions();

	// 创建更新包构建器
	PatchPackageBuilder = MakeShareable(new FHotUpdatePatchPackageBuilder());
	PatchPackageBuilder->OnProgress.AddSP(this, &SHotUpdatePackagingPanel::OnPackagingProgress);
	PatchPackageBuilder->OnComplete.AddSP(this, &SHotUpdatePackagingPanel::OnPackagingComplete);

	UE_LOG(LogHotUpdateEditor, Log, TEXT("SHotUpdatePackagingPanel::Construct 完成"));

	// 默认输出目录
	PackageConfig.OutputDirectory.Path = FPaths::ProjectSavedDir() / TEXT("HotUpdateVersions");

	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(Orient_Horizontal)
		+ SSplitter::Slot()
		.Value(0.4f)
		[
			CreateLeftPanel()
		]
		+ SSplitter::Slot()
		.Value(0.6f)
		[
			CreateRightPanel()
		]
	];
}

SHotUpdatePackagingPanel::~SHotUpdatePackagingPanel()
{
	if (PatchPackageBuilder.IsValid())
	{
		PatchPackageBuilder->OnProgress.RemoveAll(this);
		PatchPackageBuilder->OnComplete.RemoveAll(this);
		PatchPackageBuilder.Reset();
	}
}

TSharedRef<SWidget> SHotUpdatePackagingPanel::CreateLeftPanel()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(0)
		[
			SNew(SVerticalBox)
			// 标题栏
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
				.Padding(FMargin(12, 8))
				[
					SNew(SHorizontalBox)
					// 左侧装饰条
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
					// 标题
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("SettingsTitle", "打包设置"))
						.Font(FHotUpdateEditorStyle::GetSubtitleFont())
						.ColorAndOpacity(FHotUpdateEditorStyle::GetTextPrimaryColor())
					]
				]
			]
			// 滚动区域
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				.Padding(12, 12)
				[
					CreateBasicSettings()
				]
				+ SScrollBox::Slot()
				.Padding(12, 0)
				[
					SNew(SSeparator)
				]
				+ SScrollBox::Slot()
				.Padding(12, 12)
				[
					CreateIncrementalSettings()
				]
				+ SScrollBox::Slot()
				.Padding(12, 0)
				[
					SNew(SSeparator)
				]
				+ SScrollBox::Slot()
				.Padding(12, 12)
				[
					CreateAdvancedSettings()
				]
				+ SScrollBox::Slot()
				.Padding(12, 0)
				[
					SNew(SSeparator)
				]
				+ SScrollBox::Slot()
				.Padding(12, 12)
				[
					CreateChunkSettings()
				]
			]
		];
}

TSharedRef<SWidget> SHotUpdatePackagingPanel::CreateRightPanel()
{
	return SNew(SVerticalBox)
		// 信息提示
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Center)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(24)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("FromPackagingSettingsInfo", "将从项目打包配置读取资源列表"))
					.Font(FHotUpdateEditorStyle::GetSubtitleFont())
					.ColorAndOpacity(FHotUpdateEditorStyle::GetTextSecondaryColor())
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 8)
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("FromPackagingSettingsHint", "资源列表来源于项目打包设置中的 MapsToCook、DirectoriesToAlwaysCook 等配置"))
					.Font(FHotUpdateEditorStyle::GetSmallFont())
					.ColorAndOpacity(FLinearColor::Gray)
					.AutoWrapText(true)
				]
			]
		]
		// 分隔线
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSeparator)
		]
		// 进度和操作
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			CreateProgressAndActions()
		];
}

TSharedRef<SWidget> SHotUpdatePackagingPanel::CreateBasicSettings()
{

	return SNew(SVerticalBox)
		// 目标平台
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeSettingRow(
				LOCTEXT("PlatformLabel", "目标平台:"),
				SAssignNew(PlatformComboBox, SComboBox<TSharedPtr<EHotUpdatePlatform>>)
				.OptionsSource(&PlatformOptions)
				.OnGenerateWidget(this, &SHotUpdatePackagingPanel::GeneratePlatformComboBoxItem)
				.OnSelectionChanged(this, &SHotUpdatePackagingPanel::OnPlatformSelected)
				.InitiallySelectedItem(SelectedPlatform)
				[
					SNew(STextBlock)
					.Text(this, &SHotUpdatePackagingPanel::GetSelectedPlatformText)
					.Font(FHotUpdateEditorStyle::GetNormalFont())
				]
			)
		]
		// Android 纹理格式（仅 Android 平台可见）
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.Visibility(this, &SHotUpdatePackagingPanel::GetAndroidTextureFormatVisibility)
			.BorderBackgroundColor(FLinearColor::Transparent)
			.Padding(0)
			[
				MakeSettingRow(
					LOCTEXT("AndroidTextureFormatLabel", "纹理格式:"),
					SAssignNew(AndroidTextureFormatComboBox, SComboBox<TSharedPtr<EHotUpdateAndroidTextureFormat>>)
					.OptionsSource(&AndroidTextureFormatOptions)
					.OnGenerateWidget(this, &SHotUpdatePackagingPanel::GenerateAndroidTextureFormatComboBoxItem)
					.OnSelectionChanged(this, &SHotUpdatePackagingPanel::OnAndroidTextureFormatSelected)
					.InitiallySelectedItem(SelectedAndroidTextureFormat)
					[
						SNew(STextBlock)
						.Text(this, &SHotUpdatePackagingPanel::GetSelectedAndroidTextureFormatText)
						.Font(FHotUpdateEditorStyle::GetNormalFont())
					]
				)
			]
		]
		// 版本号
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeSettingRow(
				LOCTEXT("VersionLabel", "版本号:"),
				SAssignNew(VersionTextBox, SEditableText)
				.Text(FText::FromString(PackageConfig.VersionString))
				.HintText(LOCTEXT("VersionHint", "如 1.0.0，留空使用时间戳"))
				.Font(FHotUpdateEditorStyle::GetNormalFont())
			)
		]
		// 输出目录
		+ SVerticalBox::Slot()
		.AutoHeight()
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
				.Text(FText::FromString(PackageConfig.OutputDirectory.Path))
				.IsReadOnly(true)
				.Font(FHotUpdateEditorStyle::GetNormalFont())
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("Browse", "浏览"))
				.ToolTipText(LOCTEXT("BrowseTooltip", "选择输出目录"))
				.ButtonStyle(FAppStyle::Get(), "FlatButton")
				.OnClicked(this, &SHotUpdatePackagingPanel::OnBrowseOutputDirectory)
			]
		]
		// 分隔
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 10)
		[
			SNew(SSeparator)
			.Orientation(Orient_Horizontal)
		]
		// 输出格式
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
				.WidthOverride(90)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("FormatLabel", "输出格式:"))
					.Font(FHotUpdateEditorStyle::GetNormalFont())
					.ColorAndOpacity(FHotUpdateEditorStyle::GetTextSecondaryColor())
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8, 4)
			[
				SNew(SCheckBox)
				.IsChecked(PackageConfig.OutputFormat == EHotUpdateOutputFormat::Pak ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) {
					if (NewState == ECheckBoxState::Checked)
						PackageConfig.OutputFormat = EHotUpdateOutputFormat::Pak;
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("FormatPak", "Pak"))
					.Font(FHotUpdateEditorStyle::GetNormalFont())
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8, 4)
			[
				SNew(SCheckBox)
				.IsChecked(PackageConfig.OutputFormat == EHotUpdateOutputFormat::IoStore ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) {
					if (NewState == ECheckBoxState::Checked)
						PackageConfig.OutputFormat = EHotUpdateOutputFormat::IoStore;
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("FormatIoStore", "IoStore"))
					.Font(FHotUpdateEditorStyle::GetNormalFont())
				]
			]
		]
		// 选项
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 4)
		[
			SNew(SWrapBox)
			.UseAllottedSize(true)
			+ SWrapBox::Slot()
			.Padding(0, 2, 12, 2)
			[
				SNew(SCheckBox)
				.IsChecked(PackageConfig.bEnableCompression ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) {
					PackageConfig.bEnableCompression = (NewState == ECheckBoxState::Checked);
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("EnableCompression", "启用压缩"))
					.Font(FHotUpdateEditorStyle::GetNormalFont())
				]
			]
			+ SWrapBox::Slot()
			.Padding(0, 2, 12, 2)
			[
				SAssignNew(SkipCookCheckBox, SCheckBox)
				.IsChecked(ECheckBoxState::Unchecked)
				.ToolTipText(LOCTEXT("SkipCookTooltip", "跳过 Cook 步骤，使用已有的 cooked 文件打包。如不确定请勿勾选，否则热更可能不生效"))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("SkipCook", "跳过 Cook"))
					.Font(FHotUpdateEditorStyle::GetNormalFont())
				]
			]
			+ SWrapBox::Slot()
			.Padding(0, 2, 12, 2)
			[
				SAssignNew(IncrementalCookCheckBox, SCheckBox)
				.IsChecked(ECheckBoxState::Checked)
				.ToolTipText(LOCTEXT("IncrementalCookTooltip", "只 Cook 有变更的资源，大幅减少 Cook 时间。基于 Diff 结果确定变更资源，使用 -PACKAGE 只 Cook 指定资源。需要已有 Cooked 输出作为基准"))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("IncrementalCook", "增量 Cook"))
					.Font(FHotUpdateEditorStyle::GetNormalFont())
				]
			]
			+ SWrapBox::Slot()
			.Padding(0, 2, 12, 2)
			[
				SAssignNew(SkipBuildCheckBox, SCheckBox)
				.IsChecked(ECheckBoxState::Checked)
				.ToolTipText(LOCTEXT("SkipBuildTooltip", "跳过编译步骤。如果项目已编译，可以跳过以避免 Live Coding 冲突。如不确定请勿勾选，否则可能使用旧代码逻辑"))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("SkipBuild", "跳过编译"))
					.Font(FHotUpdateEditorStyle::GetNormalFont())
				]
			]
		];
}

TSharedRef<SWidget> SHotUpdatePackagingPanel::CreateAdvancedSettings()
{
	return SNew(SExpandableArea)
		.Style(FAppStyle::Get(), "ExpandableArea")
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.HeaderPadding(FMargin(0, 4))
		.AllowAnimatedTransition(true)
		.HeaderContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("AdvancedSettings", "高级设置"))
			.Font(FAppStyle::GetFontStyle("NormalFontBold"))
			.ColorAndOpacity(FLinearColor::Gray)
		]
		.BodyContent()
		[
			SNew(SVerticalBox)
			// Chunk ID
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 2)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ChunkIdLabel", "Chunk ID:"))
					.MinDesiredWidth(80)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(8, 0)
				[
					SNew(SSpinBox< float >)
					.Value(PackageConfig.ChunkId)
					.MinValue(-1)
					.MaxValue(9999)
					.OnValueChanged_Lambda([this](float NewValue) {
						PackageConfig.ChunkId = NewValue;
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(8, 0)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ChunkIdHint", "(-1=自动)"))
					.ColorAndOpacity(FLinearColor::Gray)
					.Font(FAppStyle::GetFontStyle("SmallFont"))
				]
			]
			// 压缩级别
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 2)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CompressionLevelLabel", "压缩级别:"))
					.MinDesiredWidth(80)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(8, 0)
				[
					SNew(SSpinBox< float >)
					.Value(PackageConfig.CompressionLevel)
					.MinValue(0)
					.MaxValue(9)
					.IsEnabled_Lambda([this]() { return PackageConfig.bEnableCompression; })
					.OnValueChanged_Lambda([this](float NewValue) {
						PackageConfig.CompressionLevel = NewValue;
					})
				]
			]
		];
}

TSharedRef<SWidget> SHotUpdatePackagingPanel::CreateIncrementalSettings()
{
	return SNew(SExpandableArea)
		.Style(FAppStyle::Get(), "ExpandableArea")
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.HeaderPadding(FMargin(0, 4))
		.AllowAnimatedTransition(true)
		.HeaderContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("HotfixSettings", "热更包设置"))
			.Font(FAppStyle::GetFontStyle("NormalFontBold"))
			.ColorAndOpacity(FLinearColor::Gray)
		]
		.BodyContent()
		[
			SNew(SVerticalBox)
			// 说明文字
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 4)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("HotfixModeHint", "热更包将包含基础包及之前所有热更包的内容（累加模式）"))
				.Font(FAppStyle::GetFontStyle("SmallFont"))
				.ColorAndOpacity(FLinearColor::Gray)
				.AutoWrapText(true)
			]
			// 基于版本选择
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
					.WidthOverride(90)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("BasedOnVersionLabel", "基于版本:"))
						.Font(FHotUpdateEditorStyle::GetNormalFont())
						.ColorAndOpacity(FHotUpdateEditorStyle::GetTextSecondaryColor())
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(8, 0)
				[
					SAssignNew(VersionSelectComboBox, SComboBox<TSharedPtr<FHotUpdateVersionSelectItem>>)
					.OptionsSource(&VersionSelectOptions)
					.OnGenerateWidget_Lambda([](TSharedPtr<FHotUpdateVersionSelectItem> InItem) {
						return SNew(STextBlock)
							.Text(FText::FromString(InItem->DisplayName))
							.Font(FHotUpdateEditorStyle::GetNormalFont())
							.Margin(FMargin(4, 2));
					})
					.OnSelectionChanged_Lambda([this](TSharedPtr<FHotUpdateVersionSelectItem> InItem, ESelectInfo::Type SelectInfo) {
						if (InItem.IsValid())
						{
							SelectedVersion = InItem;
							PackageConfig.BasedOnVersion = InItem->VersionString;
							PackageConfig.BaseFileManifestPath = InItem->FileManifestPath;
						}
					})
					[
						SNew(STextBlock)
						.Text_Lambda([this]() {
							return SelectedVersion.IsValid()
								? FText::FromString(SelectedVersion->DisplayName)
								: LOCTEXT("SelectVersion", "选择版本");
						})
						.Font(FHotUpdateEditorStyle::GetNormalFont())
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("Refresh", "刷新"))
					.ButtonStyle(FAppStyle::Get(), "FlatButton")
					.OnClicked_Lambda([this]() {
						RefreshVersionSelectOptions();
						return FReply::Handled();
					})
				]
			]
		];
}

void SHotUpdatePackagingPanel::UpdatePackageConfigFromUI()
{
	if (VersionTextBox.IsValid())
	{
		PackageConfig.VersionString = VersionTextBox->GetText().ToString();
	}

	if (OutputDirTextBox.IsValid())
	{
		PackageConfig.OutputDirectory.Path = OutputDirTextBox->GetText().ToString();
	}

	if (SelectedAndroidTextureFormat.IsValid())
	{
		PackageConfig.AndroidTextureFormat = *SelectedAndroidTextureFormat;
	}

	// 版本选择器的配置在 OnSelectionChanged 回调中已更新
}

TSharedRef<SWidget> SHotUpdatePackagingPanel::CreateProgressAndActions()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
		.Padding(FMargin(12, 10))
		[
			SNew(SVerticalBox)
			// 状态信息
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(StatusTextBlock, STextBlock)
				.Text(LOCTEXT("ReadyStatus", "准备就绪"))
				.ColorAndOpacity(FHotUpdateEditorStyle::GetSuccessColor())
				.Font(FHotUpdateEditorStyle::GetNormalFont())
			]
			// 进度信息
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 4)
			[
				SAssignNew(ProgressTextBlock, STextBlock)
				.Text(FText::GetEmpty())
				.Font(FHotUpdateEditorStyle::GetSmallFont())
				.ColorAndOpacity(FHotUpdateEditorStyle::GetTextSecondaryColor())
			]
			// 进度条
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 4)
			[
				SAssignNew(ProgressBar, SProgressBar)
				.Percent(0.0f)
				.FillColorAndOpacity(FHotUpdateEditorStyle::GetPrimaryColor())
			]
			// 操作按钮
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 10, 0, 0)
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
					SAssignNew(PackageButton, SButton)
					.Text(LOCTEXT("Package", "开始打包"))
					.ToolTipText(LOCTEXT("PackageTooltip", "从项目打包配置读取资源并打包"))
					.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
					.IsEnabled(this, &SHotUpdatePackagingPanel::IsPackagingEnabled)
					.OnClicked(this, &SHotUpdatePackagingPanel::OnPackageClicked)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4, 0)
				[
					SAssignNew(CancelButton, SButton)
					.Text(LOCTEXT("Cancel", "取消"))
					.ToolTipText(LOCTEXT("CancelTooltip", "取消当前打包操作"))
					.ButtonStyle(FAppStyle::Get(), "Button")
					.IsEnabled(this, &SHotUpdatePackagingPanel::IsPackaging)
					.Visibility_Lambda([this]() { return bIsPackaging ? EVisibility::Visible : EVisibility::Collapsed; })
					.OnClicked(this, &SHotUpdatePackagingPanel::OnCancelClicked)
				]
			]
		];
}

FReply SHotUpdatePackagingPanel::OnPackageClicked()
{
	UE_LOG(LogHotUpdateEditor, Log, TEXT("OnPackageClicked 开始"));

	if (bIsPackaging)
	{
		UE_LOG(LogHotUpdateEditor, Warning, TEXT("打包正在进行中，忽略重复点击"));
		return FReply::Handled();
	}

	// 更新配置
	UpdatePackageConfigFromUI();

	// 构建热更包
	FHotUpdatePatchPackageConfig PatchConfig;
	PatchConfig.PatchVersion = PackageConfig.VersionString;
	PatchConfig.BaseVersion = PackageConfig.BasedOnVersion;
	PatchConfig.Platform = PackageConfig.Platform;
	PatchConfig.BaseFileManifestPath.FilePath = PackageConfig.BaseFileManifestPath;
	PatchConfig.bIncludeDependencies = PackageConfig.bIncludeDependencies;
	PatchConfig.OutputDirectory = PackageConfig.OutputDirectory;
	PatchConfig.bSkipCook = SkipCookCheckBox.IsValid() && SkipCookCheckBox->IsChecked();
	PatchConfig.bIncrementalCook = IncrementalCookCheckBox.IsValid() && IncrementalCookCheckBox->IsChecked();
	PatchConfig.bSkipBuild = SkipBuildCheckBox.IsValid() && SkipBuildCheckBox->IsChecked();
	PatchConfig.IoStoreConfig.CompressionFormat = PackageConfig.bEnableCompression ? TEXT("Oodle") : TEXT("None");
	PatchConfig.IoStoreConfig.CompressionLevel = PackageConfig.CompressionLevel;
	UHotUpdateEditorSettings* EditorSettings = UHotUpdateEditorSettings::Get();
	PatchConfig.IoStoreConfig.EncryptionKey = EditorSettings->DefaultEncryptionKey;
	PatchConfig.IoStoreConfig.bEncryptIndex = EditorSettings->bDefaultEncryptIndex;
	PatchConfig.IoStoreConfig.bEncryptContent = EditorSettings->bDefaultEncryptContent;
	PatchConfig.IoStoreConfig.bUseIoStore = (PackageConfig.OutputFormat == EHotUpdateOutputFormat::IoStore);
	PatchConfig.bSynchronousMode = false;

	// 开始打包
	bIsPackaging = true;
	StatusTextBlock->SetText(LOCTEXT("PackagingStatus", "正在构建热更包..."));
	StatusTextBlock->SetColorAndOpacity(FHotUpdateEditorStyle::GetProcessingColor());
	ProgressTextBlock->SetText(FText::GetEmpty());
	ProgressBar->SetPercent(0.0f);

	PatchPackageBuilder->BuildPatchPackageAsync(PatchConfig);

	return FReply::Handled();
}

FReply SHotUpdatePackagingPanel::OnCancelClicked()
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

void SHotUpdatePackagingPanel::CancelCurrentBuild()
{
	if (PatchPackageBuilder && PatchPackageBuilder->IsBuilding())
	{
		PatchPackageBuilder->CancelBuild();
	}
}

FReply SHotUpdatePackagingPanel::OnBrowseOutputDirectory()
{
	return BrowseOutputDirectory(PackageConfig.OutputDirectory.Path, OutputDirTextBox, PackageConfig.OutputDirectory.Path);
}

void SHotUpdatePackagingPanel::OnPackagingComplete(const FHotUpdatePatchPackageResult& Result)
{
	HandlePackagingCompleteImpl(Result.bSuccess, Result.PatchUtocPath, Result.PatchSize, Result.ChangedAssetCount, Result.ErrorMessage);
}

void SHotUpdatePackagingPanel::OnPackagingProgress(const FHotUpdatePackageProgress& Progress)
{
	HandlePackagingProgress(Progress);
}

bool SHotUpdatePackagingPanel::IsPackagingEnabled() const
{
	return !bIsPackaging;
}

TSharedRef<SWidget> SHotUpdatePackagingPanel::GenerateChunkStrategyComboBoxItem(TSharedPtr<EHotUpdateChunkStrategy> InItem)
{
	return SNew(STextBlock)
		.Text(HotUpdateUtils::GetChunkStrategyDisplayName(*InItem))
		.Font(FHotUpdateEditorStyle::GetNormalFont())
		.Margin(FMargin(4, 2));
}

void SHotUpdatePackagingPanel::OnChunkStrategySelected(TSharedPtr<EHotUpdateChunkStrategy> InItem, ESelectInfo::Type SelectInfo)
{
	SelectedChunkStrategy = InItem;
	if (InItem.IsValid())
	{
		PackageConfig.ChunkStrategy = *InItem;
	}
}

FText SHotUpdatePackagingPanel::GetSelectedChunkStrategyText() const
{
	if (SelectedChunkStrategy.IsValid())
	{
		return HotUpdateUtils::GetChunkStrategyDisplayName(*SelectedChunkStrategy);
	}
	return HotUpdateUtils::GetChunkStrategyDisplayName(EHotUpdateChunkStrategy::Size);
}

void SHotUpdatePackagingPanel::UpdateProgressBar(float Percent)
{
	if (ProgressBar.IsValid())
	{
		ProgressBar->SetPercent(Percent);
	}
}

void SHotUpdatePackagingPanel::RefreshVersionSelectOptions()
{
	VersionSelectOptions.Empty();

	FHotUpdateVersionManager VersionManager;
	TArray<FHotUpdateVersionSelectItem> Versions = VersionManager.GetSelectableVersions(PackageConfig.Platform);

	for (const FHotUpdateVersionSelectItem& Version : Versions)
	{
		VersionSelectOptions.Add(MakeShareable(new FHotUpdateVersionSelectItem(Version)));
	}

	if (VersionSelectComboBox.IsValid())
	{
		VersionSelectComboBox->RefreshOptions();
	}

	if (VersionSelectOptions.Num() > 0)
	{
		SelectedVersion = VersionSelectOptions[0];
		PackageConfig.BasedOnVersion = SelectedVersion->VersionString;
		PackageConfig.BaseFileManifestPath = SelectedVersion->FileManifestPath;
	}
	else
	{
		SelectedVersion.Reset();
		PackageConfig.BasedOnVersion.Empty();
		PackageConfig.BaseFileManifestPath.Empty();
	}
}

void SHotUpdatePackagingPanel::RefreshSavedBaseVersions()
{
	RefreshVersionSelectOptions();
}

TSharedRef<SWidget> SHotUpdatePackagingPanel::CreateChunkSettings()
{
	return SNew(SExpandableArea)
		.Style(FAppStyle::Get(), "ExpandableArea")
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.HeaderPadding(FMargin(0, 4))
		.AllowAnimatedTransition(true)
		.HeaderContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ChunkSettings", "分包设置"))
			.Font(FAppStyle::GetFontStyle("NormalFontBold"))
			.ColorAndOpacity(FLinearColor::Gray)
		]
		.BodyContent()
		[
			SNew(SVerticalBox)
			// 分包策略选择
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
						.Text(LOCTEXT("ChunkStrategyLabel", "分包策略:"))
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(8, 0)
				[
					SAssignNew(ChunkStrategyComboBox, SComboBox< TSharedPtr<EHotUpdateChunkStrategy> >)
					.OptionsSource(&ChunkStrategyOptions)
					.OnGenerateWidget(this, &SHotUpdatePackagingPanel::GenerateChunkStrategyComboBoxItem)
					.OnSelectionChanged(this, &SHotUpdatePackagingPanel::OnChunkStrategySelected)
					.InitiallySelectedItem(SelectedChunkStrategy)
					[
						SNew(STextBlock)
						.Text(this, &SHotUpdatePackagingPanel::GetSelectedChunkStrategyText)
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
					return SelectedChunkStrategy.IsValid() && *SelectedChunkStrategy == EHotUpdateChunkStrategy::Size
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
					.Value(PackageConfig.MaxChunkSizeMB)
					.MinValue(1)
					.MaxValue(2048)
					.MinSliderValue(1)
					.MaxSliderValue(512)
					.SliderExponent(1.0f)
					.OnValueChanged_Lambda([this](float NewValue) {
						PackageConfig.MaxChunkSizeMB = FMath::RoundToInt(NewValue);
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
		];
}

#undef LOCTEXT_NAMESPACE