// Copyright czm. All Rights Reserved.

#include "Widgets/HotUpdatePackagingPanel.h"
#include "Widgets/HotUpdatePackagingCallbackHandler.h"
#include "HotUpdateEditor.h"
#include "HotUpdateEditorSettings.h"
#include "HotUpdateEditorStyle.h"
#include "HotUpdateBasePackageBuilder.h"
#include "HotUpdatePatchPackageBuilder.h"
#include "HotUpdateVersionManager.h"
#include "Styling/AppStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Dialogs/Dialogs.h"
#include "HAL/PlatformProcess.h"
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
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Styling/CoreStyle.h"
#include "Misc/Paths.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"

#define LOCTEXT_NAMESPACE "HotUpdatePackagingPanel"

// 使用统一的样式类
using FStatusColors = FHotUpdateEditorStyle::FStatusColors;
using FDiffColors = FHotUpdateEditorStyle::FDiffColors;

void SHotUpdatePackagingPanel::Construct(const FArguments& InArgs)
{
	UE_LOG(LogHotUpdateEditor, Log, TEXT("SHotUpdatePackagingPanel::Construct 开始"));

	ParentWindow = InArgs._ParentWindow;
	bIsPackaging = false;

	UE_LOG(LogHotUpdateEditor, Log, TEXT("  创建 CallbackHandler 和 Builder..."));

	// 初始化平台选项
	PlatformOptions.Add(MakeShareable(new EHotUpdatePlatform(EHotUpdatePlatform::Windows)));
	PlatformOptions.Add(MakeShareable(new EHotUpdatePlatform(EHotUpdatePlatform::Android)));
	PlatformOptions.Add(MakeShareable(new EHotUpdatePlatform(EHotUpdatePlatform::IOS)));
	SelectedPlatform = PlatformOptions[0];

	// 初始化 Android 纹理格式选项
	AndroidTextureFormatOptions.Add(MakeShareable(new EHotUpdateAndroidTextureFormat(EHotUpdateAndroidTextureFormat::ETC2)));
	AndroidTextureFormatOptions.Add(MakeShareable(new EHotUpdateAndroidTextureFormat(EHotUpdateAndroidTextureFormat::ASTC)));
	AndroidTextureFormatOptions.Add(MakeShareable(new EHotUpdateAndroidTextureFormat(EHotUpdateAndroidTextureFormat::DXT)));
	AndroidTextureFormatOptions.Add(MakeShareable(new EHotUpdateAndroidTextureFormat(EHotUpdateAndroidTextureFormat::Multi)));
	SelectedAndroidTextureFormat = AndroidTextureFormatOptions[0];

	// 初始化打包类型选项
	PackageTypeOptions.Add(MakeShareable(new EHotUpdatePackageType(EHotUpdatePackageType::FromPackagingSettings)));
	PackageTypeOptions.Add(MakeShareable(new EHotUpdatePackageType(EHotUpdatePackageType::Asset)));
	PackageTypeOptions.Add(MakeShareable(new EHotUpdatePackageType(EHotUpdatePackageType::Directory)));
	SelectedPackageType = PackageTypeOptions[0];

	// 初始化分包策略选项
	ChunkStrategyOptions.Add(MakeShareable(new EHotUpdateChunkStrategy(EHotUpdateChunkStrategy::None)));
	ChunkStrategyOptions.Add(MakeShareable(new EHotUpdateChunkStrategy(EHotUpdateChunkStrategy::Size)));
	ChunkStrategyOptions.Add(MakeShareable(new EHotUpdateChunkStrategy(EHotUpdateChunkStrategy::Directory)));
	ChunkStrategyOptions.Add(MakeShareable(new EHotUpdateChunkStrategy(EHotUpdateChunkStrategy::AssetType)));
	ChunkStrategyOptions.Add(MakeShareable(new EHotUpdateChunkStrategy(EHotUpdateChunkStrategy::PrimaryAsset)));
	ChunkStrategyOptions.Add(MakeShareable(new EHotUpdateChunkStrategy(EHotUpdateChunkStrategy::Hybrid)));
	SelectedChunkStrategy = ChunkStrategyOptions[4]; // 默认 PrimaryAsset

	// 初始化版本选择选项
	RefreshVersionSelectOptions();

	// 创建回调处理器并绑定到 Panel
	CallbackHandler = NewObject<UHotUpdatePackagingCallbackHandler>();
	CallbackHandler->AddToRoot();
	CallbackHandler->OwnerPanel = SharedThis(this);

	// 创建基础包构建器
	BasePackageBuilder = NewObject<UHotUpdateBasePackageBuilder>();
	BasePackageBuilder->AddToRoot();
	BasePackageBuilder->OnProgress.AddDynamic(CallbackHandler, &UHotUpdatePackagingCallbackHandler::OnPackagingProgress);
	BasePackageBuilder->OnComplete.AddDynamic(CallbackHandler, &UHotUpdatePackagingCallbackHandler::OnBasePackageComplete);
	UE_LOG(LogHotUpdateEditor, Log, TEXT("  BasePackageBuilder 创建完成, IsBuilding: %s"),
		BasePackageBuilder->IsBuilding() ? TEXT("true") : TEXT("false"));

	// 创建更新包构建器
	PatchPackageBuilder = NewObject<UHotUpdatePatchPackageBuilder>();
	PatchPackageBuilder->AddToRoot();
	PatchPackageBuilder->OnProgress.AddDynamic(CallbackHandler, &UHotUpdatePackagingCallbackHandler::OnPackagingProgress);
	PatchPackageBuilder->OnComplete.AddDynamic(CallbackHandler, &UHotUpdatePackagingCallbackHandler::OnPatchPackageComplete);
	UE_LOG(LogHotUpdateEditor, Log, TEXT("  PatchPackageBuilder 创建完成, IsBuilding: %s"),
		PatchPackageBuilder->IsBuilding() ? TEXT("true") : TEXT("false"));

	UE_LOG(LogHotUpdateEditor, Log, TEXT("SHotUpdatePackagingPanel::Construct 完成"));

	// 默认输出目录
	PackageConfig.OutputDirectory.Path = FPaths::ProjectSavedDir() / TEXT("HotUpdatePackages");

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
		// 资源列表
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			CreateAssetList()
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
	auto MakeSettingRow = [](const FText& Label, TSharedRef<SWidget> ValueWidget) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(90)
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
	// 打包类型
	+ SVerticalBox::Slot()
	.AutoHeight()
	[
		MakeSettingRow(
			LOCTEXT("PackageTypeLabel", "打包类型:"),
			SAssignNew(PackageTypeComboBox, SComboBox<TSharedPtr<EHotUpdatePackageType>>)
			.OptionsSource(&PackageTypeOptions)
			.OnGenerateWidget(this, &SHotUpdatePackagingPanel::GeneratePackageTypeComboBoxItem)
			.OnSelectionChanged(this, &SHotUpdatePackagingPanel::OnPackageTypeSelected)
			.InitiallySelectedItem(SelectedPackageType)
			[
				SNew(STextBlock)
				.Text(this, &SHotUpdatePackagingPanel::GetSelectedPackageTypeText)
				.Font(FHotUpdateEditorStyle::GetNormalFont())
			]
		)
	]
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
				SAssignNew(SkipBuildCheckBox, SCheckBox)
				.IsChecked(ECheckBoxState::Unchecked)
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
	// 使用 SExpandableArea 替代手动实现
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
	// 更新版本固定为热更包模式（基础版本打包请使用"基础版本"Tab）
	PackageConfig.PackagingMode = EHotUpdatePackagingMode::HotfixPackage;

	// 使用 SExpandableArea 替代手动实现
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
							PackageConfig.BaseManifestPath = InItem->ManifestPath;
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
	PackageConfig.AssetPaths = AssetPaths;
}

TSharedRef<SWidget> SHotUpdatePackagingPanel::CreateAssetList()
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
						.Text(LOCTEXT("AssetListTitle", "已选择资源"))
						.Font(FHotUpdateEditorStyle::GetSubtitleFont())
						.ColorAndOpacity(FHotUpdateEditorStyle::GetTextPrimaryColor())
					]
					// 资源计数
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.Padding(12, 0)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(this, &SHotUpdatePackagingPanel::GetAssetInfoText)
						.ColorAndOpacity(FHotUpdateEditorStyle::GetTextSecondaryColor())
						.Font(FHotUpdateEditorStyle::GetSmallFont())
					]
					// 添加资源按钮
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(2, 0)
					[
						SNew(SButton)
						.Text(LOCTEXT("AddAssets", "添加资源"))
						.ToolTipText(LOCTEXT("AddAssetsTooltip", "添加要打包的资源"))
						.ButtonStyle(FAppStyle::Get(), "FlatButton")
						.IsEnabled(this, &SHotUpdatePackagingPanel::CanSelectAssets)
						.OnClicked(this, &SHotUpdatePackagingPanel::OnSelectAssetsClicked)
					]
					// 添加非资产文件按钮
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(2, 0)
					[
						SNew(SButton)
						.Text(LOCTEXT("AddNonAsset", "添加文件"))
						.ToolTipText(LOCTEXT("AddNonAssetTooltip", "选择非资产文件（如文本、配置、图片等）"))
						.ButtonStyle(FAppStyle::Get(), "FlatButton")
						.IsEnabled(this, &SHotUpdatePackagingPanel::CanSelectNonAssetFiles)
						.OnClicked(this, &SHotUpdatePackagingPanel::OnSelectNonAssetFilesClicked)
					]
					// 添加目录按钮
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(2, 0)
					[
						SNew(SButton)
						.Text(LOCTEXT("AddDirectory", "添加目录"))
						.ToolTipText(LOCTEXT("AddDirectoryTooltip", "添加要打包的目录"))
						.ButtonStyle(FAppStyle::Get(), "FlatButton")
						.IsEnabled(this, &SHotUpdatePackagingPanel::CanSelectDirectories)
						.OnClicked(this, &SHotUpdatePackagingPanel::OnSelectDirectoriesClicked)
					]
					// 清空按钮
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(2, 0)
					[
						SNew(SButton)
						.Text(LOCTEXT("ClearAll", "清空"))
						.ToolTipText(LOCTEXT("ClearAllTooltip", "清空已选择的资源列表"))
						.ButtonStyle(FAppStyle::Get(), "FlatButton")
						.IsEnabled_Lambda([this]() { return AssetPaths.Num() > 0 && !bIsPackaging; })
						.OnClicked(this, &SHotUpdatePackagingPanel::OnClearSelectionClicked)
					]
				]
			]
			// 资源列表
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(4)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
				.Padding(4)
				[
					SAssignNew(AssetListView, SListView<TSharedPtr<FString>>)
					.ListItemsSource(&AssetListItems)
					.OnGenerateRow(this, &SHotUpdatePackagingPanel::GenerateAssetListItem)
					.SelectionMode(ESelectionMode::Single)
					.HeaderRow
					(
						SNew(SHeaderRow)
						+ SHeaderRow::Column("Path")
						.DefaultLabel(LOCTEXT("PathColumn", "资源路径"))
						.FillWidth(0.8f)
						.HeaderContentPadding(FMargin(8, 4))
						+ SHeaderRow::Column("Actions")
						.DefaultLabel(LOCTEXT("ActionsColumn", "操作"))
						.FillWidth(0.2f)
						.HAlignCell(HAlign_Center)
						.HeaderContentPadding(FMargin(8, 4))
					)
				]
			]
		];
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
					.ToolTipText(LOCTEXT("PackageTooltip", "开始打包选中的资源"))
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

FText SHotUpdatePackagingPanel::GetAssetInfoText() const
{
	if (PackageConfig.PackageType == EHotUpdatePackageType::FromPackagingSettings)
	{
		return LOCTEXT("FromPackagingSettings", "将从项目打包配置读取资源列表");
	}

	if (AssetPaths.Num() == 0)
	{
		return LOCTEXT("NoAssetsSelected", "未选择资源");
	}

	return FText::FromString(FString::Printf(TEXT("%d 项"), AssetPaths.Num()));
}

void SHotUpdatePackagingPanel::SetAssetPaths(const TArray<FString>& InPaths)
{
	AssetPaths = InPaths;
	RefreshAssetList();
}

void SHotUpdatePackagingPanel::SetPackageType(EHotUpdatePackageType InType)
{
	PackageConfig.PackageType = InType;

	for (int32 i = 0; i < PackageTypeOptions.Num(); i++)
	{
		if (*PackageTypeOptions[i] == InType)
		{
			SelectedPackageType = PackageTypeOptions[i];
			if (PackageTypeComboBox.IsValid())
			{
				PackageTypeComboBox->SetSelectedItem(SelectedPackageType);
			}
			break;
		}
	}
}

FReply SHotUpdatePackagingPanel::OnCloseClicked()
{
	if (bIsPackaging)
	{
		FText Title = LOCTEXT("ConfirmCancelTitle", "确认关闭");
		FText Message = LOCTEXT("ConfirmCancelMessage", "打包正在进行中，确定要取消并关闭窗口吗？");

		if (FMessageDialog::Open(EAppMsgType::YesNo, Message, Title) != EAppReturnType::Yes)
		{
			return FReply::Handled();
		}

		if (BasePackageBuilder && BasePackageBuilder->IsBuilding())
		{
			BasePackageBuilder->CancelBuild();
		}
		if (PatchPackageBuilder && PatchPackageBuilder->IsBuilding())
		{
			PatchPackageBuilder->CancelBuild();
		}
	}

	// 停靠模式下无 SWindow 可关闭，由标签栏的关闭按钮处理
	// 仅在独立窗口模式下关闭窗口
	if (ParentWindow.IsValid())
	{
		ParentWindow->RequestDestroyWindow();
	}

	return FReply::Handled();
}

FReply SHotUpdatePackagingPanel::OnPackageClicked()
{
	UE_LOG(LogHotUpdateEditor, Log, TEXT("OnPackageClicked 开始"));
	UE_LOG(LogHotUpdateEditor, Log, TEXT("  bIsPackaging: %s"), bIsPackaging ? TEXT("true") : TEXT("false"));
	UE_LOG(LogHotUpdateEditor, Log, TEXT("  PatchPackageBuilder: %s"), PatchPackageBuilder ? TEXT("valid") : TEXT("null"));

	if (bIsPackaging)
	{
		UE_LOG(LogHotUpdateEditor, Warning, TEXT("打包正在进行中，忽略重复点击"));
		return FReply::Handled();
	}

	// 检查 Builder 状态
	if (PatchPackageBuilder)
	{
		UE_LOG(LogHotUpdateEditor, Log, TEXT("  PatchPackageBuilder->IsBuilding(): %s"), PatchPackageBuilder->IsBuilding() ? TEXT("true") : TEXT("false"));
	}

	// 更新配置
	UpdatePackageConfigFromUI();

	UE_LOG(LogHotUpdateEditor, Log, TEXT("  PackageConfig.VersionString: %s"), *PackageConfig.VersionString);
	UE_LOG(LogHotUpdateEditor, Log, TEXT("  PackageConfig.AssetPaths.Num(): %d"), PackageConfig.AssetPaths.Num());

	// 设置资源路径
	PackageConfig.AssetPaths = AssetPaths;

	// 构建热更包
	FHotUpdatePatchPackageConfig PatchConfig;
	PatchConfig.PatchVersion = PackageConfig.VersionString;
	PatchConfig.BaseVersion = PackageConfig.BasedOnVersion;
	PatchConfig.Platform = PackageConfig.Platform;
	PatchConfig.BaseManifestPath.FilePath = PackageConfig.BaseManifestPath;
	PatchConfig.AssetPaths = PackageConfig.AssetPaths;
	PatchConfig.bIncludeDependencies = PackageConfig.bIncludeDependencies;
	PatchConfig.OutputDirectory = PackageConfig.OutputDirectory;
	PatchConfig.bSkipCook = SkipCookCheckBox.IsValid() && SkipCookCheckBox->IsChecked();
	PatchConfig.bSkipBuild = SkipBuildCheckBox.IsValid() && SkipBuildCheckBox->IsChecked();
	PatchConfig.PackageType = PackageConfig.PackageType;
	PatchConfig.IoStoreConfig.CompressionFormat = PackageConfig.bEnableCompression ? TEXT("Oodle") : TEXT("None");
	PatchConfig.IoStoreConfig.CompressionLevel = PackageConfig.CompressionLevel;
		UHotUpdateEditorSettings* EditorSettings = UHotUpdateEditorSettings::Get();
		PatchConfig.IoStoreConfig.EncryptionKey = EditorSettings->DefaultEncryptionKey;
		PatchConfig.IoStoreConfig.bEncryptIndex = EditorSettings->bDefaultEncryptIndex;
		PatchConfig.IoStoreConfig.bEncryptContent = EditorSettings->bDefaultEncryptContent;
		PatchConfig.IoStoreConfig.bUseIoStore = (PackageConfig.OutputFormat == EHotUpdateOutputFormat::IoStore);

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
		if (BasePackageBuilder && BasePackageBuilder->IsBuilding())
		{
			BasePackageBuilder->CancelBuild();
		}
		if (PatchPackageBuilder && PatchPackageBuilder->IsBuilding())
		{
			PatchPackageBuilder->CancelBuild();
		}
		bIsPackaging = false;
		StatusTextBlock->SetText(LOCTEXT("CancelledStatus", "已取消"));
		StatusTextBlock->SetColorAndOpacity(FHotUpdateEditorStyle::GetWarningColor());
		ProgressBar->SetPercent(0.0f);
	}
	return FReply::Handled();
}

FReply SHotUpdatePackagingPanel::OnBrowseOutputDirectory()
{
	TSharedPtr<SWindow> ParentWindowPtr = ParentWindow.IsValid() ? ParentWindow : FSlateApplication::Get().FindBestParentWindowForDialogs(nullptr);

	void* ParentWindowHandle = ParentWindowPtr.IsValid() ? ParentWindowPtr->GetNativeWindow()->GetOSWindowHandle() : nullptr;

	FString SelectedDirectory;
	if (FDesktopPlatformModule::Get()->OpenDirectoryDialog(
		ParentWindowHandle,
		LOCTEXT("SelectOutputDir", "选择输出目录").ToString(),
		PackageConfig.OutputDirectory.Path,
		SelectedDirectory))
	{
		PackageConfig.OutputDirectory.Path = SelectedDirectory;
		if (OutputDirTextBox.IsValid())
		{
			OutputDirTextBox->SetText(FText::FromString(SelectedDirectory));
		}
	}

	return FReply::Handled();
}

void SHotUpdatePackagingPanel::OnPackagingComplete(const FHotUpdatePackageResult& Result)
{
	bIsPackaging = false;

	// 清除进度通知
	if (ProgressNotification.IsValid())
	{
		ProgressNotification->ExpireAndFadeout();
		ProgressNotification.Reset();
	}

	if (Result.bSuccess)
	{

		FString SuccessMsg = FString::Printf(
			TEXT("打包成功! 文件: %s, 大小: %.2f MB, 资源数: %d"),
			*Result.OutputFilePath,
			Result.FileSize / (1024.0 * 1024.0),
			Result.AssetCount
		);
		StatusTextBlock->SetText(FText::FromString(SuccessMsg));
		StatusTextBlock->SetColorAndOpacity(FHotUpdateEditorStyle::GetSuccessColor());
		ProgressBar->SetPercent(1.0f);

		// 使用增强的成功通知
		ShowSuccessNotification(FText::FromString(SuccessMsg), FPaths::GetPath(Result.OutputFilePath));
	}
	else
	{
		StatusTextBlock->SetText(FText::FromString(Result.ErrorMessage));
		StatusTextBlock->SetColorAndOpacity(FHotUpdateEditorStyle::GetErrorColor());

		// 使用增强的错误通知
		ShowErrorNotification(FText::FromString(Result.ErrorMessage));
	}

	ProgressTextBlock->SetText(FText::GetEmpty());
}

void SHotUpdatePackagingPanel::OnPackagingProgress(const FHotUpdatePackageProgress& Progress)
{
	FString ProgressMsg = FString::Printf(
		TEXT("%s - %d/%d 文件"),
		*Progress.StageDescription.ToString(),
		Progress.ProcessedFiles,
		Progress.TotalFiles
	);
	ProgressTextBlock->SetText(FText::FromString(ProgressMsg));

	float Percent = Progress.GetProgressPercent() / 100.0f;
	ProgressBar->SetPercent(Percent);
}

bool SHotUpdatePackagingPanel::IsPackagingEnabled() const
{
	if (bIsPackaging)
	{
		return false;
	}

	if (PackageConfig.PackageType == EHotUpdatePackageType::FromPackagingSettings)
	{
		return true;
	}

	return AssetPaths.Num() > 0;
}

TSharedRef<SWidget> SHotUpdatePackagingPanel::GeneratePlatformComboBoxItem(TSharedPtr<EHotUpdatePlatform> InItem)
{
	FText PlatformText;
	switch (*InItem)
	{
	case EHotUpdatePlatform::Windows:
		PlatformText = LOCTEXT("PlatformWindows", "Windows");
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

void SHotUpdatePackagingPanel::OnPlatformSelected(TSharedPtr<EHotUpdatePlatform> InItem, ESelectInfo::Type SelectInfo)
{
	SelectedPlatform = InItem;
	if (InItem.IsValid())
	{
		PackageConfig.Platform = *InItem;
	}
}

FText SHotUpdatePackagingPanel::GetSelectedPlatformText() const
{
	if (SelectedPlatform.IsValid())
	{
		switch (*SelectedPlatform)
		{
		case EHotUpdatePlatform::Windows:
			return LOCTEXT("PlatformWindows", "Windows");
		case EHotUpdatePlatform::Android:
			return LOCTEXT("PlatformAndroid", "Android");
		case EHotUpdatePlatform::IOS:
			return LOCTEXT("PlatformIOS", "iOS");
		}
	}
	return LOCTEXT("PlatformWindows", "Windows");
}

TSharedRef<SWidget> SHotUpdatePackagingPanel::GeneratePackageTypeComboBoxItem(TSharedPtr<EHotUpdatePackageType> InItem)
{
	FText TypeText;
	switch (*InItem)
	{
	case EHotUpdatePackageType::Asset:
		TypeText = LOCTEXT("PackageTypeAsset", "单个资源");
		break;
	case EHotUpdatePackageType::Directory:
		TypeText = LOCTEXT("PackageTypeDirectory", "目录");
		break;
	case EHotUpdatePackageType::FromPackagingSettings:
		TypeText = LOCTEXT("PackageTypeFromPackagingSettings", "从项目配置读取");
		break;
	}

	return SNew(STextBlock)
		.Text(TypeText)
		.Font(FHotUpdateEditorStyle::GetNormalFont())
		.Margin(FMargin(4, 2));
}

void SHotUpdatePackagingPanel::OnPackageTypeSelected(TSharedPtr<EHotUpdatePackageType> InItem, ESelectInfo::Type SelectInfo)
{
	SelectedPackageType = InItem;
	if (InItem.IsValid())
	{
		PackageConfig.PackageType = *InItem;
		AssetPaths.Empty();
		RefreshAssetList();
	}
}

FText SHotUpdatePackagingPanel::GetSelectedPackageTypeText() const
{
	if (SelectedPackageType.IsValid())
	{
		switch (*SelectedPackageType)
		{
		case EHotUpdatePackageType::Asset:
			return LOCTEXT("PackageTypeAsset", "单个资源");
		case EHotUpdatePackageType::Directory:
			return LOCTEXT("PackageTypeDirectory", "目录");
		case EHotUpdatePackageType::FromPackagingSettings:
			return LOCTEXT("PackageTypeFromPackagingSettings", "从项目配置读取");
		}
	}
	return LOCTEXT("PackageTypeAsset", "单个资源");
}

bool SHotUpdatePackagingPanel::CanSelectAssets() const
{
	if (bIsPackaging) return false;
	return PackageConfig.PackageType == EHotUpdatePackageType::Asset;
}

bool SHotUpdatePackagingPanel::CanSelectDirectories() const
{
	if (bIsPackaging) return false;
	return PackageConfig.PackageType == EHotUpdatePackageType::Directory;
}

bool SHotUpdatePackagingPanel::CanSelectNonAssetFiles() const
{
	if (bIsPackaging) return false;
	return PackageConfig.PackageType == EHotUpdatePackageType::Asset;
}

FReply SHotUpdatePackagingPanel::OnSelectAssetsClicked()
{
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

	FAssetPickerConfig AssetPickerConfig;
	AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateLambda([this](const FAssetData& AssetData)
	{
		FString AssetPath = AssetData.PackageName.ToString();
		if (!AssetPaths.Contains(AssetPath))
		{
			AssetPaths.Add(AssetPath);
			RefreshAssetList();
		}
	});
	AssetPickerConfig.bAllowNullSelection = false;
	AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
	AssetPickerConfig.ThumbnailScale = 0.3f;

	TSharedRef<SWindow> PickerWindow = SNew(SWindow)
		.Title(LOCTEXT("AssetPickerTitle", "选择资源"))
		.ClientSize(FVector2D(600, 400))
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	TSharedPtr<SVerticalBox> ContentBox;
	PickerWindow->SetContent(
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(8)
		[
			SAssignNew(ContentBox, SVerticalBox)
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 8, 0, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SSpacer)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("Close", "关闭"))
					.OnClicked_Lambda([PickerWindow]() {
						PickerWindow->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
			]
		]
	);

	FSlateApplication::Get().AddWindow(PickerWindow);

	return FReply::Handled();
}

FReply SHotUpdatePackagingPanel::OnSelectDirectoriesClicked()
{
	TSharedPtr<SWindow> ParentWindowPtr = ParentWindow.IsValid() ? ParentWindow : FSlateApplication::Get().FindBestParentWindowForDialogs(nullptr);

	void* ParentWindowHandle = ParentWindowPtr.IsValid() ? ParentWindowPtr->GetNativeWindow()->GetOSWindowHandle() : nullptr;

	FString SelectedDirectory;
	if (FDesktopPlatformModule::Get()->OpenDirectoryDialog(
		ParentWindowHandle,
		LOCTEXT("SelectDirectory", "选择目录").ToString(),
		FPaths::ProjectContentDir(),
		SelectedDirectory))
	{
		FString GamePath;
		if (SelectedDirectory.StartsWith(FPaths::ProjectContentDir()))
		{
			GamePath = TEXT("/Game/") + SelectedDirectory.RightChop(FPaths::ProjectContentDir().Len());
		}
		else if (SelectedDirectory.StartsWith(FPaths::EngineContentDir()))
		{
			GamePath = TEXT("/Engine/") + SelectedDirectory.RightChop(FPaths::EngineContentDir().Len());
		}
		else
		{
			GamePath = SelectedDirectory;
		}

		GamePath.RemoveFromEnd(TEXT("/"));
		GamePath.RemoveFromEnd(TEXT("\\"));

		if (!AssetPaths.Contains(GamePath))
		{
			AssetPaths.Add(GamePath);
			RefreshAssetList();
		}
	}

	return FReply::Handled();
}

FReply SHotUpdatePackagingPanel::OnSelectNonAssetFilesClicked()
{
	TSharedPtr<SWindow> ParentWindowPtr = ParentWindow.IsValid() ? ParentWindow : FSlateApplication::Get().FindBestParentWindowForDialogs(nullptr);

	void* ParentWindowHandle = ParentWindowPtr.IsValid() ? ParentWindowPtr->GetNativeWindow()->GetOSWindowHandle() : nullptr;

	TArray<FString> SelectedFiles;
	if (FDesktopPlatformModule::Get()->OpenFileDialog(
		ParentWindowHandle,
		LOCTEXT("SelectNonAssetFiles", "选择非资产文件").ToString(),
		FPaths::ProjectDir(),
		TEXT(""),
		TEXT("所有文件 (*.*)|*.*|文本文件 (*.txt;*.json;*.xml;*.csv;*.ini)|*.txt;*.json;*.xml;*.csv;*.ini|图片文件 (*.png;*.jpg;*.bmp)|*.png;*.jpg;*.bmp|音频文件 (*.wav;*.mp3;*.ogg)|*.wav;*.mp3;*.ogg|视频文件 (*.mp4;*.avi;*.mov)|*.mp4;*.avi;*.mov"),
		EFileDialogFlags::Multiple,
		SelectedFiles))
	{
		for (const FString& FilePath : SelectedFiles)
		{
			if (!AssetPaths.Contains(FilePath))
			{
				AssetPaths.Add(FilePath);
			}
		}
		RefreshAssetList();
	}

	return FReply::Handled();
}

FReply SHotUpdatePackagingPanel::OnClearSelectionClicked()
{
	AssetPaths.Empty();
	RefreshAssetList();
	return FReply::Handled();
}

TSharedRef<ITableRow> SHotUpdatePackagingPanel::GenerateAssetListItem(TSharedPtr<FString> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
		.Padding(2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(0.8f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(*InItem))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(0.2f)
			.HAlign(HAlign_Right)
			[
				SNew(SButton)
				.Text(LOCTEXT("Remove", "移除"))
				.ButtonStyle(FAppStyle::Get(), "FlatButton")
				.ContentPadding(FMargin(4, 2))
				.IsEnabled(!bIsPackaging)
				.OnClicked(this, &SHotUpdatePackagingPanel::OnRemoveAssetClicked, InItem)
			]
		];
}

FReply SHotUpdatePackagingPanel::OnRemoveAssetClicked(TSharedPtr<FString> InItem)
{
	AssetPaths.Remove(*InItem);
	RefreshAssetList();
	return FReply::Handled();
}

void SHotUpdatePackagingPanel::RefreshAssetList()
{
	AssetListItems.Empty();
	for (const FString& Path : AssetPaths)
	{
		AssetListItems.Add(MakeShareable(new FString(Path)));
	}

	if (AssetListView.IsValid())
	{
		AssetListView->RequestListRefresh();
	}
}

void SHotUpdatePackagingPanel::UpdateProgressBar(float Percent)
{
	if (ProgressBar.IsValid())
	{
		ProgressBar->SetPercent(Percent);
	}
}

void SHotUpdatePackagingPanel::ShowNotification(const FText& Message, SNotificationItem::ECompletionState State)
{
	// 使用通知管理器显示通知
	TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(
		FNotificationInfo(Message)
	);

	if (NotificationItem.IsValid())
	{
		NotificationItem->SetCompletionState(State);
	}
}

void SHotUpdatePackagingPanel::ShowSuccessNotification(const FText& Message, const FString& OutputPath)
{
	FNotificationInfo Info(Message);
	Info.ExpireDuration = 5.0f;
	Info.bUseSuccessFailIcons = true;
	Info.Image = FAppStyle::GetBrush("Icons.SuccessWithColor");

	// 添加超链接打开输出目录
	Info.Hyperlink = FSimpleDelegate::CreateLambda([OutputPath]() {
		FPlatformProcess::ExploreFolder(*OutputPath);
	});
	Info.HyperlinkText = LOCTEXT("OpenOutputDir", "打开输出目录");

	TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
	if (NotificationItem.IsValid())
	{
		NotificationItem->SetCompletionState(SNotificationItem::CS_Success);
	}
}

void SHotUpdatePackagingPanel::ShowErrorNotification(const FText& Message)
{
	FNotificationInfo Info(Message);
	Info.ExpireDuration = 8.0f;
	Info.bUseSuccessFailIcons = true;
	Info.Image = FAppStyle::GetBrush("Icons.ErrorWithColor");

	// 添加按钮查看日志
	Info.ButtonDetails.Add(FNotificationButtonInfo(
		LOCTEXT("ViewLog", "查看日志"),
		LOCTEXT("ViewLogTooltip", "打开输出日志窗口"),
		FSimpleDelegate::CreateLambda([]() {
			FGlobalTabmanager::Get()->TryInvokeTab(FName("OutputLog"));
		}),
		SNotificationItem::ECompletionState::CS_Fail
	));

	TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
	if (NotificationItem.IsValid())
	{
		NotificationItem->SetCompletionState(SNotificationItem::CS_Fail);
	}
}

void SHotUpdatePackagingPanel::ShowProgressNotification(const FText& Message, bool bShowCancelButton)
{
	// 取消之前的进度通知
	if (ProgressNotification.IsValid())
	{
		ProgressNotification->ExpireAndFadeout();
		ProgressNotification.Reset();
	}

	FNotificationInfo Info(Message);
	Info.bFireAndForget = false; // 手动控制消失
	Info.ExpireDuration = 0.0f;

	if (bShowCancelButton)
	{
		Info.ButtonDetails.Add(FNotificationButtonInfo(
			LOCTEXT("Cancel", "取消"),
			LOCTEXT("CancelTooltip", "取消当前打包操作"),
			FSimpleDelegate::CreateLambda([this]() {
				OnCancelClicked();
			}),
			SNotificationItem::ECompletionState::CS_Pending
		));
	}

	ProgressNotification = FSlateNotificationManager::Get().AddNotification(Info);
	if (ProgressNotification.IsValid())
	{
		ProgressNotification->SetCompletionState(SNotificationItem::CS_Pending);
	}
}

void SHotUpdatePackagingPanel::RefreshVersionSelectOptions()
{
	VersionSelectOptions.Empty();

	UHotUpdateVersionManager* VersionManager = NewObject<UHotUpdateVersionManager>();
	TArray<FHotUpdateVersionSelectItem> Versions = VersionManager->GetSelectableVersions(PackageConfig.Platform);

	for (const FHotUpdateVersionSelectItem& Version : Versions)
	{
		VersionSelectOptions.Add(MakeShareable(new FHotUpdateVersionSelectItem(Version)));
	}

	// 更新下拉框
	if (VersionSelectComboBox.IsValid())
	{
		VersionSelectComboBox->RefreshOptions();
	}

	// 默认选择最新的版本
	if (VersionSelectOptions.Num() > 0)
	{
		SelectedVersion = VersionSelectOptions[0];
		PackageConfig.BasedOnVersion = SelectedVersion->VersionString;
		PackageConfig.BaseManifestPath = SelectedVersion->ManifestPath;
	}
	else
	{
		SelectedVersion.Reset();
		PackageConfig.BasedOnVersion.Empty();
		PackageConfig.BaseManifestPath.Empty();
	}
}

void SHotUpdatePackagingPanel::RefreshSavedBaseVersions()
{
	// 保留旧方法以兼容，但调用新方法
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

TSharedRef<SWidget> SHotUpdatePackagingPanel::GenerateChunkStrategyComboBoxItem(TSharedPtr<EHotUpdateChunkStrategy> InItem)
{
	FText StrategyText;
	switch (*InItem)
	{
	case EHotUpdateChunkStrategy::None:
		StrategyText = LOCTEXT("StrategyNone", "不分包");
		break;
	case EHotUpdateChunkStrategy::Size:
		StrategyText = LOCTEXT("StrategySize", "按大小分包");
		break;
	case EHotUpdateChunkStrategy::Directory:
		StrategyText = LOCTEXT("StrategyDirectory", "按目录分包");
		break;
	case EHotUpdateChunkStrategy::AssetType:
		StrategyText = LOCTEXT("StrategyAssetType", "按类型分包");
		break;
	case EHotUpdateChunkStrategy::PrimaryAsset:
		StrategyText = LOCTEXT("StrategyPrimaryAsset", "UE5标准分包");
		break;
	case EHotUpdateChunkStrategy::Hybrid:
		StrategyText = LOCTEXT("StrategyHybrid", "混合模式");
		break;
	}
	return SNew(STextBlock)
		.Text(StrategyText)
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
		switch (*SelectedChunkStrategy)
		{
		case EHotUpdateChunkStrategy::None:
			return LOCTEXT("StrategyNone", "不分包");
		case EHotUpdateChunkStrategy::Size:
			return LOCTEXT("StrategySize", "按大小分包");
		case EHotUpdateChunkStrategy::Directory:
			return LOCTEXT("StrategyDirectory", "按目录分包");
		case EHotUpdateChunkStrategy::AssetType:
			return LOCTEXT("StrategyAssetType", "按类型分包");
		case EHotUpdateChunkStrategy::PrimaryAsset:
			return LOCTEXT("StrategyPrimaryAsset", "UE5标准分包");
		case EHotUpdateChunkStrategy::Hybrid:
			return LOCTEXT("StrategyHybrid", "混合模式");
		}
	}
	return LOCTEXT("StrategyPrimaryAsset", "UE5标准分包");
}

TSharedRef<SWidget> SHotUpdatePackagingPanel::GenerateAndroidTextureFormatComboBoxItem(TSharedPtr<EHotUpdateAndroidTextureFormat> InItem)
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

void SHotUpdatePackagingPanel::OnAndroidTextureFormatSelected(TSharedPtr<EHotUpdateAndroidTextureFormat> InItem, ESelectInfo::Type SelectInfo)
{
	SelectedAndroidTextureFormat = InItem;
	if (InItem.IsValid())
	{
		PackageConfig.AndroidTextureFormat = *InItem;
	}
}

FText SHotUpdatePackagingPanel::GetSelectedAndroidTextureFormatText() const
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

EVisibility SHotUpdatePackagingPanel::GetAndroidTextureFormatVisibility() const
{
	if (SelectedPlatform.IsValid() && *SelectedPlatform == EHotUpdatePlatform::Android)
	{
		return EVisibility::Visible;
	}
	return EVisibility::Collapsed;
}


void SHotUpdatePackagingPanel::CleanupRootReferences()
{
	if (BasePackageBuilder)
	{
		BasePackageBuilder->RemoveFromRoot();
	}
	if (PatchPackageBuilder)
	{
		PatchPackageBuilder->RemoveFromRoot();
	}
	if (CallbackHandler)
	{
		CallbackHandler->RemoveFromRoot();
	}
}

#undef LOCTEXT_NAMESPACE