// Copyright czm. All Rights Reserved.

#include "Widgets/HotUpdateVersionManagementPanel.h"
#include "HotUpdateVersionManager.h"
#include "HotUpdateNotificationHelper.h"
#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/MessageDialog.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Layout/SSeparator.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Misc/MessageDialog.h"

#define LOCTEXT_NAMESPACE "HotUpdateVersionManagementPanel"

// 列名定义
static const FName ColumnVersion("Version");
static const FName ColumnKind("Kind");
static const FName ColumnBaseVersion("BaseVersion");
static const FName ColumnCreatedTime("CreatedTime");
static const FName ColumnAction("Action");

/**
 * 版本列表行控件
 */
class SHotUpdateVersionListItem : public SMultiColumnTableRow<TSharedPtr<FHotUpdateVersionSelectItem>>
{
public:
	SLATE_BEGIN_ARGS(SHotUpdateVersionListItem) {}
		SLATE_ARGUMENT(TSharedPtr<FHotUpdateVersionSelectItem>, Item)
		SLATE_ARGUMENT(TSharedPtr<SHotUpdateVersionManagementPanel>, OwnerPanel)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView)
	{
		Item = InArgs._Item;
		OwnerPanel = InArgs._OwnerPanel;

		SMultiColumnTableRow<TSharedPtr<FHotUpdateVersionSelectItem>>::Construct(
			FSuperRowType::FArguments(),
			InOwnerTableView
		);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		if (!Item.IsValid())
		{
			return SNew(STextBlock).Text(LOCTEXT("InvalidItem", "无效项"));
		}

		if (ColumnName == ColumnVersion)
		{
			return SNew(STextBlock)
				.Text(FText::FromString(Item->VersionString))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10));
		}
		else if (ColumnName == ColumnKind)
		{
			FText KindText = Item->PackageKind == EHotUpdatePackageKind::Base
				? LOCTEXT("BasePackage", "基础包")
				: LOCTEXT("PatchPackage", "热更包");

			FLinearColor KindColor = Item->PackageKind == EHotUpdatePackageKind::Base
				? FLinearColor(0.2f, 0.6f, 0.2f, 1.0f)  // 绿色
				: FLinearColor(0.6f, 0.4f, 0.2f, 1.0f); // 橙色

			return SNew(STextBlock)
				.Text(KindText)
				.ColorAndOpacity(KindColor);
		}
		else if (ColumnName == ColumnBaseVersion)
		{
			FText BaseText = Item->PackageKind == EHotUpdatePackageKind::Base
				? LOCTEXT("None", "-")
				: FText::FromString(Item->BaseVersion);

			return SNew(STextBlock).Text(BaseText);
		}
		else if (ColumnName == ColumnCreatedTime)
		{
			FText TimeText = FText::FromString(Item->CreatedTime.ToString());

			return SNew(STextBlock).Text(TimeText);
		}
		else if (ColumnName == ColumnAction)
		{
			return SNew(SButton)
				.Text(LOCTEXT("Delete", "删除"))
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Danger")
				.OnClicked_Lambda([this]()
				{
					if (OwnerPanel.IsValid() && Item.IsValid())
					{
						OwnerPanel->DeleteVersion(Item);
					}
					return FReply::Handled();
				})
				.ToolTipText(LOCTEXT("DeleteTooltip", "移除版本注册"));
		}

		return SNew(STextBlock).Text(FText::FromString(ColumnName.ToString()));
	}

private:
	TSharedPtr<FHotUpdateVersionSelectItem> Item;
	TSharedPtr<SHotUpdateVersionManagementPanel> OwnerPanel;
};

void SHotUpdateVersionManagementPanel::Construct(const FArguments& InArgs)
{
	ParentWindow = InArgs._ParentWindow;

	// 初始化平台选项
	PlatformOptions.Add(MakeShareable(new EHotUpdatePlatform(EHotUpdatePlatform::Windows)));
	PlatformOptions.Add(MakeShareable(new EHotUpdatePlatform(EHotUpdatePlatform::Android)));
	PlatformOptions.Add(MakeShareable(new EHotUpdatePlatform(EHotUpdatePlatform::IOS)));
	SelectedPlatform = PlatformOptions[0]; // 默认 Windows

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f)
		[
			CreateToolBar()
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(4.0f)
		[
			CreateVersionList()
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f)
		[
			CreateStatusBar()
		]
	];

	// 初始刷新版本列表
	RefreshVersionList();
}

TSharedRef<SWidget> SHotUpdateVersionManagementPanel::CreateToolBar()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("Refresh", "刷新"))
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
			.OnClicked(this, &SHotUpdateVersionManagementPanel::OnRefreshClicked)
			.ToolTipText(LOCTEXT("RefreshTooltip", "刷新版本列表"))
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.HAlign(HAlign_Right)
		.Padding(2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PlatformLabel", "平台:"))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2.0f)
			[
				SAssignNew(PlatformComboBox, SComboBox<TSharedPtr<EHotUpdatePlatform>>)
				.OptionsSource(&PlatformOptions)
				.OnGenerateWidget(this, &SHotUpdateVersionManagementPanel::GeneratePlatformComboBoxItem)
				.OnSelectionChanged(this, &SHotUpdateVersionManagementPanel::OnPlatformSelected)
				.InitiallySelectedItem(SelectedPlatform)
				[
					SNew(STextBlock)
					.Text(this, &SHotUpdateVersionManagementPanel::GetSelectedPlatformText)
				]
			]
		];
}

TSharedRef<SWidget> SHotUpdateVersionManagementPanel::CreateVersionList()
{
	// 创建列表头部
	TSharedPtr<SHeaderRow> HeaderRow = SNew(SHeaderRow)
		+ SHeaderRow::Column(ColumnVersion)
		.DefaultLabel(LOCTEXT("VersionHeader", "版本号"))
		.FixedWidth(120.0f)

		+ SHeaderRow::Column(ColumnKind)
		.DefaultLabel(LOCTEXT("KindHeader", "类型"))
		.FixedWidth(80.0f)

		+ SHeaderRow::Column(ColumnBaseVersion)
		.DefaultLabel(LOCTEXT("BaseVersionHeader", "基础版本"))
		.FixedWidth(100.0f)

		+ SHeaderRow::Column(ColumnCreatedTime)
		.DefaultLabel(LOCTEXT("CreatedTimeHeader", "创建时间"))
		.FixedWidth(160.0f)

		+ SHeaderRow::Column(ColumnAction)
		.DefaultLabel(LOCTEXT("ActionHeader", "操作"))
		.FixedWidth(80.0f);

	return SAssignNew(VersionListView, SListView<TSharedPtr<FHotUpdateVersionSelectItem>>)
		.ListItemsSource(&VersionListItems)
		.OnGenerateRow(this, &SHotUpdateVersionManagementPanel::OnGenerateVersionListRow)
		.OnSelectionChanged(this, &SHotUpdateVersionManagementPanel::OnVersionListSelectionChanged)
		.HeaderRow(HeaderRow)
		.SelectionMode(ESelectionMode::Single);
}

TSharedRef<SWidget> SHotUpdateVersionManagementPanel::CreateStatusBar()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("TotalLabel", "总版本数: "))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2.0f)
		[
			SAssignNew(StatisticsText, STextBlock)
			.Text(FText::FromString(TEXT("0")))
		];
}

TSharedRef<ITableRow> SHotUpdateVersionManagementPanel::OnGenerateVersionListRow(TSharedPtr<FHotUpdateVersionSelectItem> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SHotUpdateVersionListItem, OwnerTable)
		.Item(InItem)
		.OwnerPanel(SharedThis(this));
}

void SHotUpdateVersionManagementPanel::OnVersionListSelectionChanged(TSharedPtr<FHotUpdateVersionSelectItem> SelectedItem, ESelectInfo::Type SelectInfo)
{
	SelectedVersion = SelectedItem;
}

TSharedRef<SWidget> SHotUpdateVersionManagementPanel::GeneratePlatformComboBoxItem(TSharedPtr<EHotUpdatePlatform> InItem)
{
	FText PlatformText;
	if (InItem.IsValid())
	{
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
	}

	return SNew(STextBlock).Text(PlatformText);
}

void SHotUpdateVersionManagementPanel::OnPlatformSelected(TSharedPtr<EHotUpdatePlatform> InItem, ESelectInfo::Type SelectInfo)
{
	if (InItem.IsValid())
	{
		SelectedPlatform = InItem;
		RefreshVersionList();
	}
}

FText SHotUpdateVersionManagementPanel::GetSelectedPlatformText() const
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
	return LOCTEXT("PlatformUnknown", "未知");
}

FReply SHotUpdateVersionManagementPanel::OnRefreshClicked()
{
	RefreshVersionList();
	return FReply::Handled();
}

void SHotUpdateVersionManagementPanel::RefreshVersionList()
{
	if (!SelectedPlatform.IsValid())
	{
		return;
	}

	// 获取版本列表
	TArray<FHotUpdateVersionSelectItem> Versions = VersionManager.GetSelectableVersions(*SelectedPlatform);

	// 清空并重建列表
	VersionListItems.Empty();
	for (const FHotUpdateVersionSelectItem& Version : Versions)
	{
		VersionListItems.Add(MakeShareable(new FHotUpdateVersionSelectItem(Version)));
	}

	// 更新列表视图
	if (VersionListView.IsValid())
	{
		VersionListView->RequestListRefresh();
	}

	// 更新统计信息
	UpdateStatistics();
}

void SHotUpdateVersionManagementPanel::DeleteVersion(TSharedPtr<FHotUpdateVersionSelectItem> VersionItem)
{
	if (!VersionItem.IsValid() || !SelectedPlatform.IsValid())
	{
		return;
	}

	// 显示确认对话框
	if (!ShowDeleteConfirmationDialog(VersionItem->VersionString))
	{
		return;
	}

	// 执行删除
	bool bSuccess = VersionManager.UnregisterVersion(VersionItem->VersionString, *SelectedPlatform);

	if (bSuccess)
	{
		// 刷新列表
		RefreshVersionList();

		// 显示成功通知
		FHotUpdateNotificationHelper::ShowNotification(
			FText::FromString(FString::Printf(TEXT("版本 '%s' 已从注册表中移除"), *VersionItem->VersionString)),
			SNotificationItem::CS_Success
		);
	}
	else
	{
		// 显示失败通知
		FHotUpdateNotificationHelper::ShowErrorNotification(
			LOCTEXT("DeleteFailed", "删除版本失败")
		);
	}
}

bool SHotUpdateVersionManagementPanel::ShowDeleteConfirmationDialog(const FString& VersionString)
{
	FText DialogText = FText::FromString(
		FString::Printf(TEXT("确定要移除版本 '%s' 的注册吗？\n\n此操作仅移除注册表条目，不会删除实际文件。"),
			*VersionString)
	);

	EAppReturnType::Type ReturnType = FMessageDialog::Open(EAppMsgType::YesNo, DialogText);
	return ReturnType == EAppReturnType::Yes;
}

void SHotUpdateVersionManagementPanel::UpdateStatistics()
{
	if (StatisticsText.IsValid())
	{
		StatisticsText->SetText(FText::FromString(FString::Printf(TEXT("%d"), VersionListItems.Num())));
	}
}

#undef LOCTEXT_NAMESPACE