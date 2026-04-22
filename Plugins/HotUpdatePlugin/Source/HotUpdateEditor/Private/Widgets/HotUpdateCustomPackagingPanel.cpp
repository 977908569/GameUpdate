// Copyright czm. All Rights Reserved.

#include "Widgets/HotUpdateCustomPackagingPanel.h"
#include "HotUpdateEditor.h"
#include "HotUpdateEditorSettings.h"
#include "HotUpdateEditorStyle.h"
#include "HotUpdateNotificationHelper.h"
#include "HotUpdateCustomPackageBuilder.h"
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
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Styling/CoreStyle.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "HotUpdateCustomPackagingPanel"

void SHotUpdateCustomPackagingPanel::Construct(const FArguments& InArgs)
{
	ParentWindow = InArgs._ParentWindow;
	bIsPackaging = false;

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

	// 创建更新包构建器
	CustomPackageBuilder = MakeShareable(new FHotUpdateCustomPackageBuilder());
	CustomPackageBuilder->OnProgress.AddSP(this, &SHotUpdateCustomPackagingPanel::OnPackagingProgress);
	CustomPackageBuilder->OnComplete.AddSP(this, &SHotUpdateCustomPackagingPanel::OnPackagingComplete);

	// 默认输出目录
	PackageConfig.OutputDirectory.Path = FPaths::ProjectSavedDir() / TEXT("HotUpdateCustomPackages");

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

SHotUpdateCustomPackagingPanel::~SHotUpdateCustomPackagingPanel()
{
	if (CustomPackageBuilder.IsValid())
	{
		CustomPackageBuilder->OnProgress.RemoveAll(this);
		CustomPackageBuilder->OnComplete.RemoveAll(this);
		CustomPackageBuilder.Reset();
	}
}

TSharedRef<SWidget> SHotUpdateCustomPackagingPanel::CreateLeftPanel()
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
			]
		];
}

TSharedRef<SWidget> SHotUpdateCustomPackagingPanel::CreateRightPanel()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			CreateAssetList()
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSeparator)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			CreateProgressAndActions()
		];
}

TSharedRef<SWidget> SHotUpdateCustomPackagingPanel::CreateBasicSettings()
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
		// Pak优先级
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeSettingRow(
				LOCTEXT("PakPriorityLabel", "Pak优先级:"),
				SAssignNew(PakPrioritySpinBox, SSpinBox<float>)
				.Value(10)
				.MinValue(0)
				.MaxValue(9999)
				.Delta(1)
				.ToolTipText(LOCTEXT("PakPriorityTooltip", "数字越大优先级越高，默认10"))
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
				.OnGenerateWidget(this, &SHotUpdateCustomPackagingPanel::GeneratePlatformComboBoxItem)
				.OnSelectionChanged(this, &SHotUpdateCustomPackagingPanel::OnPlatformSelected)
				.InitiallySelectedItem(SelectedPlatform)
				[
					SNew(STextBlock)
					.Text(this, &SHotUpdateCustomPackagingPanel::GetSelectedPlatformText)
					.Font(FHotUpdateEditorStyle::GetNormalFont())
				]
			)
		]
		// Android 纹理格式
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.Visibility(this, &SHotUpdateCustomPackagingPanel::GetAndroidTextureFormatVisibility)
			.BorderBackgroundColor(FLinearColor::Transparent)
			.Padding(0)
			[
				MakeSettingRow(
					LOCTEXT("AndroidTextureFormatLabel", "纹理格式:"),
					SAssignNew(AndroidTextureFormatComboBox, SComboBox<TSharedPtr<EHotUpdateAndroidTextureFormat>>)
					.OptionsSource(&AndroidTextureFormatOptions)
					.OnGenerateWidget(this, &SHotUpdateCustomPackagingPanel::GenerateAndroidTextureFormatComboBoxItem)
					.OnSelectionChanged(this, &SHotUpdateCustomPackagingPanel::OnAndroidTextureFormatSelected)
					.InitiallySelectedItem(SelectedAndroidTextureFormat)
					[
						SNew(STextBlock)
						.Text(this, &SHotUpdateCustomPackagingPanel::GetSelectedAndroidTextureFormatText)
						.Font(FHotUpdateEditorStyle::GetNormalFont())
					]
				)
			]
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
				.OnClicked(this, &SHotUpdateCustomPackagingPanel::OnBrowseOutputDirectory)
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
				.ToolTipText(LOCTEXT("SkipCookTooltip", "跳过 Cook 步骤，使用已有的 cooked 文件打包"))
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
				.ToolTipText(LOCTEXT("SkipBuildTooltip", "跳过编译步骤"))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("SkipBuild", "跳过编译"))
					.Font(FHotUpdateEditorStyle::GetNormalFont())
				]
			]
		];
}

TSharedRef<SWidget> SHotUpdateCustomPackagingPanel::CreateAssetList()
{
	return SNew(SVerticalBox)
		// === uasset 文件区域 ===
		+ SVerticalBox::Slot()
		.FillHeight(0.5f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(0)
			[
				SNew(SVerticalBox)
				// uasset 标题栏
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
					.Padding(FMargin(12, 6))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Fill)
						.Padding(0, 0, 8, 0)
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
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("UassetListTitle", "uasset 文件"))
							.Font(FHotUpdateEditorStyle::GetSubtitleFont())
							.ColorAndOpacity(FHotUpdateEditorStyle::GetTextPrimaryColor())
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.Padding(8, 0)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(this, &SHotUpdateCustomPackagingPanel::GetUassetInfoText)
							.ColorAndOpacity(FHotUpdateEditorStyle::GetTextSecondaryColor())
							.Font(FHotUpdateEditorStyle::GetSmallFont())
						]
						// 添加 uasset 按钮
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(2, 0)
						[
							SNew(SButton)
							.Text(LOCTEXT("AddUasset", "添加"))
							.ToolTipText(LOCTEXT("AddUassetTooltip", "选择 uasset/umap 文件"))
							.ButtonStyle(FAppStyle::Get(), "FlatButton")
							.IsEnabled(!bIsPackaging)
							.OnClicked(this, &SHotUpdateCustomPackagingPanel::OnSelectUassetFilesClicked)
						]
						// 清空 uasset 按钮
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(2, 0)
						[
							SNew(SButton)
							.Text(LOCTEXT("ClearUasset", "清空"))
							.ToolTipText(LOCTEXT("ClearUassetTooltip", "清空 uasset 文件列表"))
							.ButtonStyle(FAppStyle::Get(), "FlatButton")
							.IsEnabled_Lambda([this]() { return UassetFilePaths.Num() > 0 && !bIsPackaging; })
							.OnClicked(this, &SHotUpdateCustomPackagingPanel::OnClearUassetClicked)
						]
					]
				]
				// uasset 列表
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				.Padding(4)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
					.Padding(4)
					[
						SAssignNew(UassetListView, SListView<TSharedPtr<FString>>)
						.ListItemsSource(&UassetListItems)
						.OnGenerateRow(this, &SHotUpdateCustomPackagingPanel::GenerateUassetListItem)
						.SelectionMode(ESelectionMode::Single)
						.HeaderRow
						(
							SNew(SHeaderRow)
							+ SHeaderRow::Column("Path")
							.DefaultLabel(LOCTEXT("UassetPathColumn", "文件路径"))
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
			]
		]
		// 分隔符
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSeparator)
		]
		// === 非资产文件区域 ===
		+ SVerticalBox::Slot()
		.FillHeight(0.5f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(0)
			[
				SNew(SVerticalBox)
				// 非资产标题栏
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
					.Padding(FMargin(12, 6))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Fill)
						.Padding(0, 0, 8, 0)
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
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("NonAssetListTitle", "非资产文件"))
							.Font(FHotUpdateEditorStyle::GetSubtitleFont())
							.ColorAndOpacity(FHotUpdateEditorStyle::GetTextPrimaryColor())
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.Padding(8, 0)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(this, &SHotUpdateCustomPackagingPanel::GetNonAssetInfoText)
							.ColorAndOpacity(FHotUpdateEditorStyle::GetTextSecondaryColor())
							.Font(FHotUpdateEditorStyle::GetSmallFont())
						]
						// 添加非资产按钮
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(2, 0)
						[
							SNew(SButton)
							.Text(LOCTEXT("AddNonAsset", "添加"))
							.ToolTipText(LOCTEXT("AddNonAssetTooltip", "选择非资产文件"))
							.ButtonStyle(FAppStyle::Get(), "FlatButton")
							.IsEnabled(!bIsPackaging)
							.OnClicked(this, &SHotUpdateCustomPackagingPanel::OnSelectNonAssetFilesClicked)
						]
						// 清空非资产按钮
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(2, 0)
						[
							SNew(SButton)
							.Text(LOCTEXT("ClearNonAsset", "清空"))
							.ToolTipText(LOCTEXT("ClearNonAssetTooltip", "清空非资产文件列表"))
							.ButtonStyle(FAppStyle::Get(), "FlatButton")
							.IsEnabled_Lambda([this]() { return NonAssetFilePaths.Num() > 0 && !bIsPackaging; })
							.OnClicked(this, &SHotUpdateCustomPackagingPanel::OnClearNonAssetClicked)
						]
					]
				]
				// 非资产列表
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				.Padding(4)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
					.Padding(4)
					[
						SAssignNew(NonAssetListView, SListView<TSharedPtr<FString>>)
						.ListItemsSource(&NonAssetListItems)
						.OnGenerateRow(this, &SHotUpdateCustomPackagingPanel::GenerateNonAssetListItem)
						.SelectionMode(ESelectionMode::Single)
						.HeaderRow
						(
							SNew(SHeaderRow)
							+ SHeaderRow::Column("Path")
							.DefaultLabel(LOCTEXT("NonAssetPathColumn", "文件路径"))
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
			]
		];
}

TSharedRef<SWidget> SHotUpdateCustomPackagingPanel::CreateProgressAndActions()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
		.Padding(FMargin(12, 10))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(StatusTextBlock, STextBlock)
				.Text(LOCTEXT("ReadyStatus", "准备就绪"))
				.ColorAndOpacity(FHotUpdateEditorStyle::GetSuccessColor())
				.Font(FHotUpdateEditorStyle::GetNormalFont())
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 4)
			[
				SAssignNew(ProgressTextBlock, STextBlock)
				.Text(FText::GetEmpty())
				.Font(FHotUpdateEditorStyle::GetSmallFont())
				.ColorAndOpacity(FHotUpdateEditorStyle::GetTextSecondaryColor())
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 4)
			[
				SAssignNew(ProgressBar, SProgressBar)
				.Percent(0.0f)
				.FillColorAndOpacity(FHotUpdateEditorStyle::GetPrimaryColor())
			]
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
					.ToolTipText(LOCTEXT("PackageTooltip", "打包选中的资源"))
					.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
					.IsEnabled(this, &SHotUpdateCustomPackagingPanel::IsPackagingEnabled)
					.OnClicked(this, &SHotUpdateCustomPackagingPanel::OnPackageClicked)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4, 0)
				[
					SAssignNew(CancelButton, SButton)
					.Text(LOCTEXT("Cancel", "取消"))
					.ToolTipText(LOCTEXT("CancelTooltip", "取消当前打包操作"))
					.ButtonStyle(FAppStyle::Get(), "Button")
					.IsEnabled(this, &SHotUpdateCustomPackagingPanel::IsPackaging)
					.Visibility_Lambda([this]() { return bIsPackaging ? EVisibility::Visible : EVisibility::Collapsed; })
					.OnClicked(this, &SHotUpdateCustomPackagingPanel::OnCancelClicked)
				]
			]
		];
}

// ===== 资源信息 =====

FText SHotUpdateCustomPackagingPanel::GetUassetInfoText() const
{
	if (UassetFilePaths.Num() == 0)
	{
		return LOCTEXT("NoUassetSelected", "未选择");
	}
	return FText::FromString(FString::Printf(TEXT("%d 项"), UassetFilePaths.Num()));
}

FText SHotUpdateCustomPackagingPanel::GetNonAssetInfoText() const
{
	if (NonAssetFilePaths.Num() == 0)
	{
		return LOCTEXT("NoNonAssetSelected", "未选择");
	}
	return FText::FromString(FString::Printf(TEXT("%d 项"), NonAssetFilePaths.Num()));
}

// ===== 打包操作 =====

void SHotUpdateCustomPackagingPanel::UpdatePackageConfigFromUI()
{
	if (OutputDirTextBox.IsValid())
	{
		PackageConfig.OutputDirectory.Path = OutputDirTextBox->GetText().ToString();
	}

	if (SelectedAndroidTextureFormat.IsValid())
	{
		PackageConfig.AndroidTextureFormat = *SelectedAndroidTextureFormat;
	}
}

FReply SHotUpdateCustomPackagingPanel::OnPackageClicked()
{
	if (bIsPackaging)
	{
		return FReply::Handled();
	}

	UpdatePackageConfigFromUI();

	// 使用时间戳作为版本号
	FString VersionStr = FString::Printf(TEXT("custom_%s"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));

	// 构建热更包配置
	FHotUpdateCustomPackageConfig CustomConfig;
	CustomConfig.PatchVersion = VersionStr;
	CustomConfig.Platform = PackageConfig.Platform;
	CustomConfig.UAssetFilePaths = UassetFilePaths;
	CustomConfig.NonAssetFilePaths = NonAssetFilePaths;
	CustomConfig.OutputDirectory = PackageConfig.OutputDirectory;
	CustomConfig.bSkipCook = SkipCookCheckBox.IsValid() && SkipCookCheckBox->IsChecked();
	CustomConfig.bSkipBuild = SkipBuildCheckBox.IsValid() && SkipBuildCheckBox->IsChecked();
	CustomConfig.PakPriority = PakPrioritySpinBox.IsValid() ? FMath::RoundToInt(PakPrioritySpinBox.Get()->GetValue()) : 10;
	CustomConfig.IoStoreConfig.CompressionFormat = PackageConfig.bEnableCompression ? TEXT("Oodle") : TEXT("None");
	CustomConfig.IoStoreConfig.CompressionLevel = PackageConfig.CompressionLevel;
	UHotUpdateEditorSettings* EditorSettings = UHotUpdateEditorSettings::Get();
	CustomConfig.IoStoreConfig.EncryptionKey = EditorSettings->DefaultEncryptionKey;
	CustomConfig.IoStoreConfig.bEncryptIndex = EditorSettings->bDefaultEncryptIndex;
	CustomConfig.IoStoreConfig.bEncryptContent = EditorSettings->bDefaultEncryptContent;
	CustomConfig.IoStoreConfig.bUseIoStore = (PackageConfig.OutputFormat == EHotUpdateOutputFormat::IoStore);
	CustomConfig.bSynchronousMode = false;

	CustomPackageBuilder->BuildCustomPackageAsync(CustomConfig);

	return FReply::Handled();
}

FReply SHotUpdateCustomPackagingPanel::OnCancelClicked()
{
	if (bIsPackaging)
	{
		if (CustomPackageBuilder && CustomPackageBuilder->IsBuilding())
		{
			CustomPackageBuilder->CancelBuild();
		}
		bIsPackaging = false;
		StatusTextBlock->SetText(LOCTEXT("CancelledStatus", "已取消"));
		StatusTextBlock->SetColorAndOpacity(FHotUpdateEditorStyle::GetWarningColor());
		ProgressBar->SetPercent(0.0f);
	}
	return FReply::Handled();
}

FReply SHotUpdateCustomPackagingPanel::OnBrowseOutputDirectory()
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

bool SHotUpdateCustomPackagingPanel::IsPackagingEnabled() const
{
	if (bIsPackaging)
	{
		return false;
	}
	return UassetFilePaths.Num() > 0 || NonAssetFilePaths.Num() > 0;
}

void SHotUpdateCustomPackagingPanel::OnPackagingComplete(const FHotUpdateCustomPackageResult& Result)
{
	bIsPackaging = false;

	if (ProgressNotification.IsValid())
	{
		ProgressNotification->ExpireAndFadeout();
		ProgressNotification.Reset();
	}

	if (Result.bSuccess)
	{
		FString SuccessMsg = FString::Printf(
			TEXT("打包成功! 文件: %s, 大小: %.2f MB, 资源数: %d"),
			*Result.PatchUtocPath,
			Result.PatchSize / (1024.0 * 1024.0),
			Result.AssetCount
		);
		StatusTextBlock->SetText(FText::FromString(SuccessMsg));
		StatusTextBlock->SetColorAndOpacity(FHotUpdateEditorStyle::GetSuccessColor());
		ProgressBar->SetPercent(1.0f);

		FHotUpdateNotificationHelper::ShowSuccessNotification(FText::FromString(SuccessMsg), FPaths::GetPath(Result.PatchUtocPath));
	}
	else
	{
		StatusTextBlock->SetText(FText::FromString(Result.ErrorMessage));
		StatusTextBlock->SetColorAndOpacity(FHotUpdateEditorStyle::GetErrorColor());

		FHotUpdateNotificationHelper::ShowErrorNotification(FText::FromString(Result.ErrorMessage));
	}

	ProgressTextBlock->SetText(FText::GetEmpty());
}

void SHotUpdateCustomPackagingPanel::OnPackagingProgress(const FHotUpdatePackageProgress& Progress)
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

// ===== uasset 文件选择 =====

FReply SHotUpdateCustomPackagingPanel::OnSelectUassetFilesClicked()
{
	TSharedPtr<SWindow> ParentWindowPtr = ParentWindow.IsValid() ? ParentWindow : FSlateApplication::Get().FindBestParentWindowForDialogs(nullptr);

	void* ParentWindowHandle = ParentWindowPtr.IsValid() ? ParentWindowPtr->GetNativeWindow()->GetOSWindowHandle() : nullptr;

	TArray<FString> SelectedFiles;
	if (FDesktopPlatformModule::Get()->OpenFileDialog(
		ParentWindowHandle,
		LOCTEXT("SelectUassetFiles", "选择 uasset 文件").ToString(),
		FPaths::ProjectContentDir(),
		TEXT(""),
		TEXT("UE Asset Files (*.uasset;*.umap)|*.uasset;*.umap|All Files (*.*)|*.*"),
		EFileDialogFlags::Multiple,
		SelectedFiles))
	{
		for (const FString& FilePath : SelectedFiles)
		{
			FString Ext = FPaths::GetExtension(FilePath);
			if ((Ext == TEXT("uasset") || Ext == TEXT("umap")) && !UassetFilePaths.Contains(FilePath))
			{
				UassetFilePaths.Add(FilePath);
			}
		}
		RefreshUassetList();
	}

	return FReply::Handled();
}

FReply SHotUpdateCustomPackagingPanel::OnClearUassetClicked()
{
	UassetFilePaths.Empty();
	RefreshUassetList();
	return FReply::Handled();
}

TSharedRef<ITableRow> SHotUpdateCustomPackagingPanel::GenerateUassetListItem(TSharedPtr<FString> InItem, const TSharedRef<STableViewBase>& OwnerTable)
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
				.OnClicked(this, &SHotUpdateCustomPackagingPanel::OnRemoveUassetClicked, InItem)
			]
		];
}

FReply SHotUpdateCustomPackagingPanel::OnRemoveUassetClicked(TSharedPtr<FString> InItem)
{
	UassetFilePaths.Remove(*InItem);
	RefreshUassetList();
	return FReply::Handled();
}

void SHotUpdateCustomPackagingPanel::RefreshUassetList()
{
	UassetListItems.Empty();
	for (const FString& Path : UassetFilePaths)
	{
		UassetListItems.Add(MakeShareable(new FString(Path)));
	}

	if (UassetListView.IsValid())
	{
		UassetListView->RequestListRefresh();
	}
}

// ===== 非资产文件选择 =====

FReply SHotUpdateCustomPackagingPanel::OnSelectNonAssetFilesClicked()
{
	TSharedPtr<SWindow> ParentWindowPtr = ParentWindow.IsValid() ? ParentWindow : FSlateApplication::Get().FindBestParentWindowForDialogs(nullptr);

	void* ParentWindowHandle = ParentWindowPtr.IsValid() ? ParentWindowPtr->GetNativeWindow()->GetOSWindowHandle() : nullptr;

	TArray<FString> SelectedFiles;
	if (FDesktopPlatformModule::Get()->OpenFileDialog(
		ParentWindowHandle,
		LOCTEXT("SelectNonAssetFiles", "选择非资产文件").ToString(),
		FPaths::ProjectDir(),
		TEXT(""),
		TEXT("非资产文件 (*.txt;*.json;*.xml;*.csv;*.ini;*.png;*.jpg;*.bmp;*.wav;*.mp3;*.ogg;*.mp4;*.avi;*.mov)|*.txt;*.json;*.xml;*.csv;*.ini;*.png;*.jpg;*.bmp;*.wav;*.mp3;*.ogg;*.mp4;*.avi;*.mov|所有文件 (*.*)|*.*"),
		EFileDialogFlags::Multiple,
		SelectedFiles))
	{
		for (const FString& FilePath : SelectedFiles)
		{
			// 排除 uasset/umap 文件
			FString Ext = FPaths::GetExtension(FilePath);
			if (Ext == TEXT("uasset") || Ext == TEXT("umap"))
			{
				continue;
			}
			if (!NonAssetFilePaths.Contains(FilePath))
			{
				NonAssetFilePaths.Add(FilePath);
			}
		}
		RefreshNonAssetList();
	}

	return FReply::Handled();
}

FReply SHotUpdateCustomPackagingPanel::OnClearNonAssetClicked()
{
	NonAssetFilePaths.Empty();
	RefreshNonAssetList();
	return FReply::Handled();
}

TSharedRef<ITableRow> SHotUpdateCustomPackagingPanel::GenerateNonAssetListItem(TSharedPtr<FString> InItem, const TSharedRef<STableViewBase>& OwnerTable)
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
				.OnClicked(this, &SHotUpdateCustomPackagingPanel::OnRemoveNonAssetClicked, InItem)
			]
		];
}

FReply SHotUpdateCustomPackagingPanel::OnRemoveNonAssetClicked(TSharedPtr<FString> InItem)
{
	NonAssetFilePaths.Remove(*InItem);
	RefreshNonAssetList();
	return FReply::Handled();
}

void SHotUpdateCustomPackagingPanel::RefreshNonAssetList()
{
	NonAssetListItems.Empty();
	for (const FString& Path : NonAssetFilePaths)
	{
		NonAssetListItems.Add(MakeShareable(new FString(Path)));
	}

	if (NonAssetListView.IsValid())
	{
		NonAssetListView->RequestListRefresh();
	}
}

// ===== 平台选择 =====

TSharedRef<SWidget> SHotUpdateCustomPackagingPanel::GeneratePlatformComboBoxItem(TSharedPtr<EHotUpdatePlatform> InItem)
{
	FText PlatformText;
	switch (*InItem)
	{
	case EHotUpdatePlatform::Windows: PlatformText = LOCTEXT("PlatformWindows", "Windows"); break;
	case EHotUpdatePlatform::Android: PlatformText = LOCTEXT("PlatformAndroid", "Android"); break;
	case EHotUpdatePlatform::IOS:     PlatformText = LOCTEXT("PlatformIOS", "iOS"); break;
	}
	return SNew(STextBlock).Text(PlatformText).Font(FHotUpdateEditorStyle::GetNormalFont()).Margin(FMargin(4, 2));
}

void SHotUpdateCustomPackagingPanel::OnPlatformSelected(TSharedPtr<EHotUpdatePlatform> InItem, ESelectInfo::Type SelectInfo)
{
	SelectedPlatform = InItem;
	if (InItem.IsValid())
	{
		PackageConfig.Platform = *InItem;
	}
}

FText SHotUpdateCustomPackagingPanel::GetSelectedPlatformText() const
{
	if (SelectedPlatform.IsValid())
	{
		switch (*SelectedPlatform)
		{
		case EHotUpdatePlatform::Windows: return LOCTEXT("PlatformWindows", "Windows");
		case EHotUpdatePlatform::Android: return LOCTEXT("PlatformAndroid", "Android");
		case EHotUpdatePlatform::IOS:     return LOCTEXT("PlatformIOS", "iOS");
		}
	}
	return LOCTEXT("PlatformWindows", "Windows");
}

// ===== Android 纹理格式 =====

TSharedRef<SWidget> SHotUpdateCustomPackagingPanel::GenerateAndroidTextureFormatComboBoxItem(TSharedPtr<EHotUpdateAndroidTextureFormat> InItem)
{
	FText FormatText;
	switch (*InItem)
	{
	case EHotUpdateAndroidTextureFormat::ETC2: FormatText = LOCTEXT("TextureFormatETC2", "ETC2"); break;
	case EHotUpdateAndroidTextureFormat::ASTC: FormatText = LOCTEXT("TextureFormatASTC", "ASTC"); break;
	case EHotUpdateAndroidTextureFormat::DXT:  FormatText = LOCTEXT("TextureFormatDXT", "DXT"); break;
	case EHotUpdateAndroidTextureFormat::Multi: FormatText = LOCTEXT("TextureFormatMulti", "Multi"); break;
	}
	return SNew(STextBlock).Text(FormatText).Font(FHotUpdateEditorStyle::GetNormalFont()).Margin(FMargin(4, 2));
}

void SHotUpdateCustomPackagingPanel::OnAndroidTextureFormatSelected(TSharedPtr<EHotUpdateAndroidTextureFormat> InItem, ESelectInfo::Type SelectInfo)
{
	SelectedAndroidTextureFormat = InItem;
	if (InItem.IsValid())
	{
		PackageConfig.AndroidTextureFormat = *InItem;
	}
}

FText SHotUpdateCustomPackagingPanel::GetSelectedAndroidTextureFormatText() const
{
	if (SelectedAndroidTextureFormat.IsValid())
	{
		switch (*SelectedAndroidTextureFormat)
		{
		case EHotUpdateAndroidTextureFormat::ETC2: return LOCTEXT("TextureFormatETC2", "ETC2");
		case EHotUpdateAndroidTextureFormat::ASTC: return LOCTEXT("TextureFormatASTC", "ASTC");
		case EHotUpdateAndroidTextureFormat::DXT:  return LOCTEXT("TextureFormatDXT", "DXT");
		case EHotUpdateAndroidTextureFormat::Multi: return LOCTEXT("TextureFormatMulti", "Multi");
		}
	}
	return LOCTEXT("TextureFormatETC2", "ETC2");
}

EVisibility SHotUpdateCustomPackagingPanel::GetAndroidTextureFormatVisibility() const
{
	if (SelectedPlatform.IsValid() && *SelectedPlatform == EHotUpdatePlatform::Android)
	{
		return EVisibility::Visible;
	}
	return EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE