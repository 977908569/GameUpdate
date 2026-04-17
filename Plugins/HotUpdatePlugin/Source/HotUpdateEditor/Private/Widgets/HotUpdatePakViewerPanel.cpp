// Copyright czm. All Rights Reserved.

#include "Widgets/HotUpdatePakViewerPanel.h"
#include "HotUpdateDiffTool.h"
#include "HotUpdateEditor.h"
#include "HotUpdateEditorStyle.h"
#include "HotUpdatePakManager.h"
#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Styling/CoreStyle.h"
#include "Styling/AppStyle.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dialogs/Dialogs.h"
#include "MainFrame.h"

#define LOCTEXT_NAMESPACE "HotUpdatePakViewer"

void SHotUpdatePakViewerPanel::Construct(const FArguments& InArgs)
{
	ParentWindow = InArgs._ParentWindow;
	CurrentFileCount = 0;
	CurrentTotalSize = 0;

	ChildSlot
	[
		SNew(SVerticalBox)
		// 顶部工具栏
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			CreateToolBar()
		]
		// 主内容区域
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(FMargin(0, 1, 0, 0))
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)
			.Style(FAppStyle::Get(), "SplitterDark")
			+ SSplitter::Slot()
			.Value(0.25f)
			[
				CreatePakListSection()
			]
			+ SSplitter::Slot()
			.Value(0.75f)
			[
				CreateContentListSection()
			]
		]
		// 底部状态栏
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			CreateStatusBar()
		]
	];
}

TSharedRef<SWidget> SHotUpdatePakViewerPanel::CreateToolBar()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
		.Padding(FMargin(12, 6))
		[
			SNew(SHorizontalBox)
			// 标题
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 16, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Title", "Pak 查看器"))
				.Font(FHotUpdateEditorStyle::GetSubtitleFont())
			]
			// 分隔线
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Fill)
			.Padding(0, 0, 16, 0)
			[
				SNew(SSeparator)
				.Orientation(Orient_Vertical)
				.Thickness(1.0f)
			]
			// 浏览 Pak 文件按钮
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("BrowsePak", "浏览 Pak 文件"))
				.ToolTipText(LOCTEXT("BrowsePakTooltip", "选择一个 Pak 文件查看其内容"))
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
				.OnClicked(this, &SHotUpdatePakViewerPanel::OnBrowsePakFile)
			]
			// 刷新按钮
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("Refresh", "刷新"))
				.ToolTipText(LOCTEXT("RefreshTooltip", "刷新 Pak 文件列表"))
				.ButtonStyle(FAppStyle::Get(), "FlatButton")
				.OnClicked_Lambda([this]() {
					RefreshPakList();
					return FReply::Handled();
				})
			]
			// 导出按钮
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("ExportContent", "导出列表"))
				.ToolTipText(LOCTEXT("ExportContentTooltip", "导出当前 Pak 内容列表到文件"))
				.ButtonStyle(FAppStyle::Get(), "FlatButton")
				.OnClicked(this, &SHotUpdatePakViewerPanel::OnExportContentList)
			]
		];
}

TSharedRef<SWidget> SHotUpdatePakViewerPanel::CreatePakListSection()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(0)
		[
			SNew(SVerticalBox)
			// 标题栏
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(12, 8)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PakFiles", "Pak 文件"))
				.Font(FHotUpdateEditorStyle::GetBoldFont())
				.ColorAndOpacity(FHotUpdateEditorStyle::GetTextPrimaryColor())
			]
			// 分隔线
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SSeparator)
				.Thickness(1.0f)
			]
			// Pak 列表
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(4)
			[
				SAssignNew(PakListView, SListView<TSharedPtr<FString>>)
				.ListItemsSource(&PakList)
				.OnGenerateRow(this, &SHotUpdatePakViewerPanel::OnGeneratePakListRow)
				.OnSelectionChanged(this, &SHotUpdatePakViewerPanel::OnPakSelected)
				.SelectionMode(ESelectionMode::Single)
			]
		];
}

TSharedRef<SWidget> SHotUpdatePakViewerPanel::CreateContentListSection()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(0)
		[
			SNew(SVerticalBox)
			// 标题和搜索框
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(12, 8)
			[
				SNew(SHorizontalBox)
				// 标题
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 16, 0)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("PakContents", "Pak 内容"))
					.Font(FHotUpdateEditorStyle::GetBoldFont())
					.ColorAndOpacity(FHotUpdateEditorStyle::GetTextPrimaryColor())
				]
				// 搜索框
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SAssignNew(SearchBox, SSearchBox)
					.HintText(LOCTEXT("SearchHint", "搜索文件..."))
					.OnTextChanged(this, &SHotUpdatePakViewerPanel::OnSearchChanged)
				]
			]
			// 分隔线
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SSeparator)
				.Thickness(1.0f)
			]
			// 内容列表
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(4)
			[
				SAssignNew(ContentListView, SListView<TSharedPtr<FHotUpdatePakEntry>>)
				.ListItemsSource(&FilteredContentEntries)
				.OnGenerateRow(this, &SHotUpdatePakViewerPanel::OnGenerateContentListRow)
				.SelectionMode(ESelectionMode::Multi)
				.HeaderRow
				(
					SNew(SHeaderRow)
					+ SHeaderRow::Column("FileName")
					.FillWidth(0.55f)
					.DefaultLabel(LOCTEXT("FileName", "文件名"))
					.HeaderContentPadding(FMargin(8, 4))

					+ SHeaderRow::Column("UncompressedSize")
					.FillWidth(0.15f)
					.DefaultLabel(LOCTEXT("UncompressedSize", "原始大小"))
					.HAlignCell(HAlign_Right)
					.HeaderContentPadding(FMargin(8, 4))

					+ SHeaderRow::Column("CompressedSize")
					.FillWidth(0.15f)
					.DefaultLabel(LOCTEXT("CompressedSize", "压缩大小"))
					.HAlignCell(HAlign_Right)
					.HeaderContentPadding(FMargin(8, 4))

					+ SHeaderRow::Column("Offset")
					.FillWidth(0.1f)
					.DefaultLabel(LOCTEXT("Offset", "偏移"))
					.HAlignCell(HAlign_Right)
					.HeaderContentPadding(FMargin(8, 4))

					+ SHeaderRow::Column("Flags")
					.FillWidth(0.05f)
					.DefaultLabel(LOCTEXT("Flags", "标志"))
					.HAlignCell(HAlign_Center)
					.HeaderContentPadding(FMargin(8, 4))
				)
			]
		];
}

TSharedRef<SWidget> SHotUpdatePakViewerPanel::CreateStatusBar()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
		.Padding(FMargin(12, 6))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() {
					if (SelectedPakPath.IsEmpty())
					{
						return LOCTEXT("NoPakSelected", "未选择 Pak 文件");
					}
					return FText::Format(
						LOCTEXT("PakStats", "文件数: {0} | 总大小: {1}"),
						FText::AsNumber(CurrentFileCount),
						FText::FromString(UHotUpdateDiffTool::FormatFileSize(CurrentTotalSize))
					);
				})
				.Font(FHotUpdateEditorStyle::GetSmallFont())
				.ColorAndOpacity(FHotUpdateEditorStyle::GetTextSecondaryColor())
			]
		];
}

FReply SHotUpdatePakViewerPanel::OnBrowsePakFile()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("DesktopPlatform not available"));
		return FReply::Handled();
	}

	TSharedPtr<SWindow> BestParentWindow = ParentWindow.IsValid() ? ParentWindow : FSlateApplication::Get().FindBestParentWindowForDialogs(nullptr);
	void* WindowHandle = (BestParentWindow.IsValid() && BestParentWindow->GetNativeWindow().IsValid()) ? BestParentWindow->GetNativeWindow()->GetOSWindowHandle() : nullptr;

	TArray<FString> OpenFilenames;
	bool bOpened = DesktopPlatform->OpenFileDialog(
		WindowHandle,
		LOCTEXT("BrowsePakTitle", "选择 Pak 文件").ToString(),
		TEXT(""),
		TEXT(""),
		TEXT("Pak 文件 (*.pak)|*.pak|所有文件 (*.*)|*.*"),
		EFileDialogFlags::None,
		OpenFilenames
	);

	UE_LOG(LogHotUpdateEditor, Log, TEXT("OpenFileDialog result: %d, Files: %d"), bOpened ? 1 : 0, OpenFilenames.Num());

	if (bOpened && OpenFilenames.Num() > 0)
	{
		SelectedPakPath = OpenFilenames[0];

		PakList.Empty();
		PakList.Add(MakeShareable(new FString(SelectedPakPath)));

		if (PakListView.IsValid())
		{
			PakListView->RequestListRefresh();
		}

		UpdateContentList();
	}

	if (ParentWindow.IsValid())
	{
		ParentWindow->BringToFront(true);
	}

	return FReply::Handled();
}

void SHotUpdatePakViewerPanel::OnPakSelected(TSharedPtr<FString> InPakPath, ESelectInfo::Type SelectInfo)
{
	if (InPakPath.IsValid())
	{
		SelectedPakPath = *InPakPath;
		UpdateContentList();
	}
}

TSharedRef<ITableRow> SHotUpdatePakViewerPanel::OnGeneratePakListRow(TSharedPtr<FString> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	FString DisplayName = InItem.IsValid() ? *InItem : TEXT("");
	FString FileName = FPaths::GetCleanFilename(DisplayName);
	FString Directory = FPaths::GetPath(DisplayName);

	return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
		.Padding(FMargin(4, 2))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::FromString(FileName))
				.ToolTipText(FText::FromString(DisplayName))
				.Font(FHotUpdateEditorStyle::GetNormalFont())
				.ColorAndOpacity(FHotUpdateEditorStyle::GetTextPrimaryColor())
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 2, 0, 0)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Directory))
				.Font(FHotUpdateEditorStyle::GetSmallFont())
				.ColorAndOpacity(FHotUpdateEditorStyle::GetTextSecondaryColor())
			]
		];
}

TSharedRef<ITableRow> SHotUpdatePakViewerPanel::OnGenerateContentListRow(TSharedPtr<FHotUpdatePakEntry> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	if (!InItem.IsValid())
	{
		return SNew(STableRow<TSharedPtr<FHotUpdatePakEntry>>, OwnerTable);
	}

	const FHotUpdatePakEntry& Entry = *InItem;

	FString FlagsStr;
	if (Entry.bIsCompressed)
	{
		FlagsStr += TEXT("C");
	}
	if (Entry.bIsEncrypted)
	{
		FlagsStr += TEXT("E");
	}
	if (FlagsStr.IsEmpty())
	{
		FlagsStr = TEXT("-");
	}

	return SNew(STableRow<TSharedPtr<FHotUpdatePakEntry>>, OwnerTable)
		.Padding(FMargin(4, 2))
		[
			SNew(SHorizontalBox)
			// 文件名
			+ SHorizontalBox::Slot()
			.FillWidth(0.55f)
			.VAlign(VAlign_Center)
			.Padding(4, 2)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Entry.FileName))
				.ToolTipText(FText::FromString(Entry.FileName))
				.Font(FHotUpdateEditorStyle::GetSmallFont())
			]
			// 原始大小
			+ SHorizontalBox::Slot()
			.FillWidth(0.15f)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			.Padding(4, 2)
			[
				SNew(STextBlock)
				.Text(FText::FromString(UHotUpdateDiffTool::FormatFileSize(Entry.UncompressedSize)))
				.Font(FHotUpdateEditorStyle::GetSmallFont())
				.ColorAndOpacity(FHotUpdateEditorStyle::GetTextSecondaryColor())
			]
			// 压缩大小
			+ SHorizontalBox::Slot()
			.FillWidth(0.15f)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			.Padding(4, 2)
			[
				SNew(STextBlock)
				.Text(FText::FromString(UHotUpdateDiffTool::FormatFileSize(Entry.CompressedSize)))
				.Font(FHotUpdateEditorStyle::GetSmallFont())
				.ColorAndOpacity(Entry.bIsCompressed ? FHotUpdateEditorStyle::GetAddedColor() : FHotUpdateEditorStyle::GetTextSecondaryColor())
			]
			// 偏移
			+ SHorizontalBox::Slot()
			.FillWidth(0.1f)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			.Padding(4, 2)
			[
				SNew(STextBlock)
				.Text(FText::AsNumber(Entry.Offset))
				.Font(FHotUpdateEditorStyle::GetSmallFont())
				.ColorAndOpacity(FHotUpdateEditorStyle::GetTextSecondaryColor())
			]
			// 标志
			+ SHorizontalBox::Slot()
			.FillWidth(0.05f)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			.Padding(4, 2)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FlagsStr))
				.Font(FHotUpdateEditorStyle::GetSmallFont())
				.ColorAndOpacity(FHotUpdateEditorStyle::GetModifiedColor())
			]
		];
}

void SHotUpdatePakViewerPanel::OnSearchChanged(const FText& InSearchText)
{
	SearchText = InSearchText.ToString().ToLower();
	UpdateContentList();
}

bool SHotUpdatePakViewerPanel::DoesEntryMatchSearch(const FHotUpdatePakEntry& Entry, const FString& InSearchText) const
{
	if (InSearchText.IsEmpty())
	{
		return true;
	}

	return Entry.FileName.ToLower().Contains(InSearchText);
}

void SHotUpdatePakViewerPanel::UpdateContentList()
{
	FilteredContentEntries.Empty();
	CurrentFileCount = 0;
	CurrentTotalSize = 0;

	if (SelectedPakPath.IsEmpty())
	{
		if (ContentListView.IsValid())
		{
			ContentListView->RequestListRefresh();
		}
		return;
	}

	UHotUpdatePakManager* PakManager = NewObject<UHotUpdatePakManager>();
	AllContentEntries = PakManager->GetPakEntries(SelectedPakPath);

	CurrentFileCount = AllContentEntries.Num();
	for (const FHotUpdatePakEntry& Entry : AllContentEntries)
	{
		CurrentTotalSize += Entry.UncompressedSize;

		if (DoesEntryMatchSearch(Entry, SearchText))
		{
			FilteredContentEntries.Add(MakeShareable(new FHotUpdatePakEntry(Entry)));
		}
	}

	if (ContentListView.IsValid())
	{
		ContentListView->RequestListRefresh();
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("Loaded %d entries from Pak: %s (Filtered: %d)"),
		AllContentEntries.Num(), *SelectedPakPath, FilteredContentEntries.Num());
}

FReply SHotUpdatePakViewerPanel::OnExportContentList()
{
	if (FilteredContentEntries.Num() == 0)
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoContentToExport", "没有内容可以导出"));
		return FReply::Handled();
	}

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return FReply::Handled();
	}

	TSharedPtr<SWindow> BestParentWindow = ParentWindow.IsValid() ? ParentWindow : FSlateApplication::Get().FindBestParentWindowForDialogs(nullptr);
	void* WindowHandle = (BestParentWindow.IsValid() && BestParentWindow->GetNativeWindow().IsValid()) ? BestParentWindow->GetNativeWindow()->GetOSWindowHandle() : nullptr;

	FString DefaultPath = FPaths::ProjectSavedDir() / TEXT("PakContentList.csv");

	TArray<FString> SaveFilenames;
	bool bSaved = DesktopPlatform->SaveFileDialog(
		WindowHandle,
		LOCTEXT("ExportTitle", "导出 Pak 内容列表").ToString(),
		*DefaultPath,
		TEXT(""),
		TEXT("CSV 文件 (*.csv)|*.csv|文本文件 (*.txt)|*.txt"),
		EFileDialogFlags::None,
		SaveFilenames
	);

	if (bSaved && SaveFilenames.Num() > 0)
	{
		FString CsvContent;
		CsvContent += TEXT("文件名,原始大小,压缩大小,偏移,压缩,加密,Hash\n");

		for (const TSharedPtr<FHotUpdatePakEntry>& EntryPtr : FilteredContentEntries)
		{
			if (EntryPtr.IsValid())
			{
				const FHotUpdatePakEntry& Entry = *EntryPtr;
				CsvContent += FString::Printf(TEXT("\"%s\",%lld,%lld,%lld,%s,%s,%s\n"),
					*Entry.FileName,
					Entry.UncompressedSize,
					Entry.CompressedSize,
					Entry.Offset,
					Entry.bIsCompressed ? TEXT("是") : TEXT("否"),
					Entry.bIsEncrypted ? TEXT("是") : TEXT("否"),
					*Entry.FileHash
				);
			}
		}

		if (FFileHelper::SaveStringToFile(CsvContent, *SaveFilenames[0], FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			UE_LOG(LogHotUpdateEditor, Log, TEXT("Exported Pak content list to: %s"), *SaveFilenames[0]);
			FMessageDialog::Open(EAppMsgType::Ok,
				FText::Format(LOCTEXT("ExportSuccess", "导出成功！\n文件: {0}"), FText::FromString(SaveFilenames[0])));
		}
		else
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("ExportFailed", "导出失败"));
		}
	}

	if (ParentWindow.IsValid())
	{
		ParentWindow->BringToFront(true);
	}

	return FReply::Handled();
}

void SHotUpdatePakViewerPanel::RefreshPakList()
{
	PakList.Empty();
	SelectedPakPath.Empty();
	FilteredContentEntries.Empty();
	CurrentFileCount = 0;
	CurrentTotalSize = 0;

	if (PakListView.IsValid())
	{
		PakListView->RequestListRefresh();
	}

	if (ContentListView.IsValid())
	{
		ContentListView->RequestListRefresh();
	}
}

#undef LOCTEXT_NAMESPACE