// Copyright czm. All Rights Reserved.

#include "Widgets/HotUpdateVersionDiffPanel.h"
#include "HotUpdateEditor.h"
#include "HotUpdateEditorStyle.h"
#include "HotUpdateDiffTool.h"
#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Dialogs/Dialogs.h"
#include "Misc/FileHelper.h"
#include "JsonObjectConverter.h"
#include "Editor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "HotUpdateVersionDiffPanel"

void SHotUpdateVersionDiffPanel::Construct(const FArguments& InArgs)
{
	ParentWindow = InArgs._ParentWindow;
	bIsLoaded = false;

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			CreateToolbar()
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 8)
		[
			CreateDirectorySelector()
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)
			+ SSplitter::Slot()
			.Value(0.7f)
			[
				CreateDiffTreeView()
			]
			+ SSplitter::Slot()
			.Value(0.3f)
			[
				CreateDetailsPanel()
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 4, 4, 8)
		[
			CreateStatisticsPanel()
		]
	];
}

TSharedRef<SWidget> SHotUpdateVersionDiffPanel::CreateToolbar()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(4, 2))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("LoadVersions", "加载版本"))
				.ToolTipText(LOCTEXT("LoadVersionsTooltip", "加载并比较两个版本的资源"))
				.OnClicked(this, &SHotUpdateVersionDiffPanel::OnLoadVersionsClicked)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("Refresh", "刷新"))
				.ToolTipText(LOCTEXT("RefreshTooltip", "重新扫描并比较资源"))
				.OnClicked(this, &SHotUpdateVersionDiffPanel::OnRefreshClicked)
				.IsEnabled_Lambda([this]() { return bIsLoaded; })
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("ExportReport", "导出报告"))
				.ToolTipText(LOCTEXT("ExportReportTooltip", "将差异报告导出为JSON文件"))
				.OnClicked(this, &SHotUpdateVersionDiffPanel::OnExportReportClicked)
				.IsEnabled_Lambda([this]() { return bIsLoaded; })
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.HAlign(HAlign_Right)
			.Padding(8, 0)
			[
				SNew(SEditableText)
				.HintText(LOCTEXT("FilterHint", "过滤资源..."))
				.OnTextChanged_Lambda([this](const FText& Text)
				{
					CurrentFilter = Text.ToString();
					ApplyFilter(CurrentFilter);
				})
			]
		];
}

TSharedRef<SWidget> SHotUpdateVersionDiffPanel::CreateDirectorySelector()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(8)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 0, 0, 4)
					[
						SNew(STextBlock).Text(LOCTEXT("BaseVersion", "基础版本目录:"))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						[
							SAssignNew(BasePathTextBox, SEditableText)
							.Text(FText::FromString(BaseVersionPath))
							.OnTextChanged_Lambda([this](const FText& Text) { BaseVersionPath = Text.ToString(); })
							.HintText(LOCTEXT("BasePathHint", "选择基础版本目录..."))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(4, 0, 0, 0)
						[
							SNew(SButton)
							.Text(LOCTEXT("Browse", "..."))
							.OnClicked(this, &SHotUpdateVersionDiffPanel::OnSelectBaseDirectoryClicked)
						]
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(16, 0, 0, 0)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 0, 0, 4)
					[
						SNew(STextBlock).Text(LOCTEXT("TargetVersion", "目标版本目录:"))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						[
							SAssignNew(TargetPathTextBox, SEditableText)
							.Text(FText::FromString(TargetVersionPath))
							.OnTextChanged_Lambda([this](const FText& Text) { TargetVersionPath = Text.ToString(); })
							.HintText(LOCTEXT("TargetPathHint", "选择目标版本目录..."))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(4, 0, 0, 0)
						[
							SNew(SButton)
							.Text(LOCTEXT("Browse", "..."))
							.OnClicked(this, &SHotUpdateVersionDiffPanel::OnSelectTargetDirectoryClicked)
						]
					]
				]
			]
		];
}

TSharedRef<SWidget> SHotUpdateVersionDiffPanel::CreateDiffTreeView()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SAssignNew(DiffTreeView, STreeView<TSharedPtr<FDiffTreeNode>>)
				.TreeItemsSource(&RootNodes)
				.OnGenerateRow(this, &SHotUpdateVersionDiffPanel::OnGenerateTreeRow)
				.OnGetChildren(this, &SHotUpdateVersionDiffPanel::OnGetTreeChildren)
				.OnSelectionChanged(this, &SHotUpdateVersionDiffPanel::OnTreeSelectionChanged)
				.OnMouseButtonDoubleClick(this, &SHotUpdateVersionDiffPanel::OnTreeViewDoubleClick)
				.SelectionMode(ESelectionMode::Single)
				.HeaderRow
				(
					SNew(SHeaderRow)
					+ SHeaderRow::Column("Name")
					.DefaultLabel(LOCTEXT("ColumnName", "名称"))
					.FillWidth(0.5f)
					+ SHeaderRow::Column("Type")
					.DefaultLabel(LOCTEXT("ColumnType", "类型"))
					.FillWidth(0.15f)
					+ SHeaderRow::Column("Change")
					.DefaultLabel(LOCTEXT("ColumnChange", "变更"))
					.FillWidth(0.15f)
					+ SHeaderRow::Column("Size")
					.DefaultLabel(LOCTEXT("ColumnSize", "大小"))
					.FillWidth(0.2f)
				)
			]
		];
}

TSharedRef<SWidget> SHotUpdateVersionDiffPanel::CreateDetailsPanel()
{
	DetailsBox = SNew(SVerticalBox);

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DetailsTitle", "详细信息"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(8, 0)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					DetailsBox.ToSharedRef()
				]
			]
		];
}

TSharedRef<SWidget> SHotUpdateVersionDiffPanel::CreateStatisticsPanel()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(8)
		[
			SAssignNew(StatisticsText, STextBlock)
			.Text(LOCTEXT("NoDataLoaded", "尚未加载数据"))
		];
}

FReply SHotUpdateVersionDiffPanel::OnLoadVersionsClicked()
{
	UE_LOG(LogHotUpdateEditor, Log, TEXT("OnLoadVersionsClicked: BaseVersionPath='%s', TargetVersionPath='%s'"),
		*BaseVersionPath, *TargetVersionPath);

	if (BaseVersionPath.IsEmpty() || TargetVersionPath.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("PleaseSelectDirectories", "请先选择基础版本和目标版本目录"));
		return FReply::Handled();
	}

	if (!FPaths::DirectoryExists(BaseVersionPath))
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(TEXT("基础版本目录不存在: %s"), *BaseVersionPath)));
		return FReply::Handled();
	}

	if (!FPaths::DirectoryExists(TargetVersionPath))
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(TEXT("目标版本目录不存在: %s"), *TargetVersionPath)));
		return FReply::Handled();
	}

	// 在版本目录中查找 filemanifest.json
	FString BaseManifestPath = FHotUpdateDiffTool::FindFileManifestPath(BaseVersionPath);
	FString TargetManifestPath = FHotUpdateDiffTool::FindFileManifestPath(TargetVersionPath);

	if (BaseManifestPath.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(TEXT("在基础版本目录中找不到 filemanifest.json: %s"), *BaseVersionPath)));
		return FReply::Handled();
	}

	if (TargetManifestPath.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(TEXT("在目标版本目录中找不到 filemanifest.json: %s"), *TargetVersionPath)));
		return FReply::Handled();
	}

	// 基于 filemanifest.json 进行版本比较
	FHotUpdateDiffTool DiffTool;
		DiffReport = DiffTool.CompareManifests(BaseManifestPath, TargetManifestPath);

	GenerateTreeNodes();
	bIsLoaded = true;

	FString StatsText = FString::Printf(
		TEXT("新增: %d | 修改: %d | 删除: %d | 未变更: %d | 总大小变化: %s"),
		DiffReport.AddedAssets.Num(),
		DiffReport.ModifiedAssets.Num(),
		DiffReport.DeletedAssets.Num(),
		DiffReport.UnchangedAssets.Num(),
		*FHotUpdateDiffTool::FormatFileSize(DiffReport.GetTotalSizeDifference())
	);
	StatisticsText->SetText(FText::FromString(StatsText));

	DiffTreeView->RequestTreeRefresh();

		// 自动展开所有文件夹节点
		for (const auto& Pair : AllNodes)
		{
			if (Pair.Value.IsValid() && Pair.Value->bIsFolder)
			{
				DiffTreeView->SetItemExpansion(Pair.Value, true);
			}
		}

		return FReply::Handled();
	}

FReply SHotUpdateVersionDiffPanel::OnRefreshClicked()
{
	return OnLoadVersionsClicked();
}

FReply SHotUpdateVersionDiffPanel::OnExportReportClicked()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return FReply::Handled();
	}

	TArray<FString> SaveFilenames;
	TSharedPtr<SWindow> ParentWindowPtr = FSlateApplication::Get().FindBestParentWindowForDialogs(nullptr);
	void* ParentWindowHandle = ParentWindowPtr.IsValid() ? ParentWindowPtr->GetNativeWindow()->GetOSWindowHandle() : nullptr;

	if (DesktopPlatform->SaveFileDialog(
		ParentWindowHandle,
		LOCTEXT("ExportReportTitle", "导出差异报告").ToString(),
		FPaths::ProjectSavedDir(),
		TEXT("DiffReport.json"),
		TEXT("JSON文件 (*.json)|*.json"),
		EFileDialogFlags::None,
		SaveFilenames))
	{
		if (SaveFilenames.Num() > 0)
		{
			FString OutputString;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);

			Writer->WriteObjectStart();
			Writer->WriteValue(TEXT("baseVersion"), DiffReport.BaseVersion);
			Writer->WriteValue(TEXT("targetVersion"), DiffReport.TargetVersion);
			Writer->WriteValue(TEXT("totalChanged"), DiffReport.GetTotalChangedCount());
			Writer->WriteValue(TEXT("totalSizeDifference"), DiffReport.GetTotalSizeDifference());

			auto WriteAssetArray = [&Writer](const FString& Name, const TArray<FHotUpdateAssetDiff>& Assets)
			{
				Writer->WriteArrayStart(Name);
				for (const FHotUpdateAssetDiff& Diff : Assets)
				{
					Writer->WriteObjectStart();
					Writer->WriteValue(TEXT("path"), Diff.AssetPath);
					Writer->WriteValue(TEXT("type"), Diff.AssetType);
					Writer->WriteValue(TEXT("oldSize"), Diff.OldSize);
					Writer->WriteValue(TEXT("newSize"), Diff.NewSize);
					Writer->WriteValue(TEXT("oldHash"), Diff.OldHash);
					Writer->WriteValue(TEXT("newHash"), Diff.NewHash);
					Writer->WriteValue(TEXT("description"), Diff.ChangeDescription);
					Writer->WriteObjectEnd();
				}
				Writer->WriteArrayEnd();
			};

			WriteAssetArray(TEXT("added"), DiffReport.AddedAssets);
			WriteAssetArray(TEXT("modified"), DiffReport.ModifiedAssets);
			WriteAssetArray(TEXT("deleted"), DiffReport.DeletedAssets);

			Writer->WriteObjectEnd();
			Writer->Close();

			FFileHelper::SaveStringToFile(OutputString, *SaveFilenames[0], FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}
	}

	return FReply::Handled();
}

FReply SHotUpdateVersionDiffPanel::OnSelectBaseDirectoryClicked()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return FReply::Handled();
	}

	TSharedPtr<SWindow> ParentWindowPtr = FSlateApplication::Get().FindBestParentWindowForDialogs(nullptr);
	void* ParentWindowHandle = ParentWindowPtr.IsValid() ? ParentWindowPtr->GetNativeWindow()->GetOSWindowHandle() : nullptr;

	FString SelectedDirectory;
	if (DesktopPlatform->OpenDirectoryDialog(
		ParentWindowHandle,
		LOCTEXT("SelectBaseDirectory", "选择基础版本目录").ToString(),
		BaseVersionPath,
		SelectedDirectory))
	{
		BaseVersionPath = SelectedDirectory;
		if (BasePathTextBox.IsValid())
		{
			BasePathTextBox->SetText(FText::FromString(BaseVersionPath));
		}

		// 基础和目标路径都已设置时自动加载
		if (!BaseVersionPath.IsEmpty() && !TargetVersionPath.IsEmpty())
		{
			OnLoadVersionsClicked();
		}
	}

	return FReply::Handled();
}

FReply SHotUpdateVersionDiffPanel::OnSelectTargetDirectoryClicked()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return FReply::Handled();
	}

	TSharedPtr<SWindow> ParentWindowPtr = FSlateApplication::Get().FindBestParentWindowForDialogs(nullptr);
	void* ParentWindowHandle = ParentWindowPtr.IsValid() ? ParentWindowPtr->GetNativeWindow()->GetOSWindowHandle() : nullptr;

	FString SelectedDirectory;
	if (DesktopPlatform->OpenDirectoryDialog(
		ParentWindowHandle,
		LOCTEXT("SelectTargetDirectory", "选择目标版本目录").ToString(),
		TargetVersionPath,
		SelectedDirectory))
	{
		TargetVersionPath = SelectedDirectory;
		if (TargetPathTextBox.IsValid())
		{
			TargetPathTextBox->SetText(FText::FromString(TargetVersionPath));
		}

		// 基础和目标路径都已设置时自动加载
		if (!BaseVersionPath.IsEmpty() && !TargetVersionPath.IsEmpty())
		{
			OnLoadVersionsClicked();
		}
	}

	return FReply::Handled();
}

void SHotUpdateVersionDiffPanel::GenerateTreeNodes()
{
	RootNodes.Empty();
	AllNodes.Empty();

	TMap<FString, TSharedPtr<FDiffTreeNode>> PathToNode;

	auto AddDiffToTree = [&](const FHotUpdateAssetDiff& Diff, EHotUpdateFileChangeType ChangeType)
	{
		FString Path = Diff.AssetPath;

		// 防御性规范化：将反斜杠替换为正斜杠，确保路径分割正确
		FString NormalizedPath = Path;
		NormalizedPath.ReplaceInline(TEXT("\\"), TEXT("/"));

		TArray<FString> PathParts;
		NormalizedPath.ParseIntoArray(PathParts, TEXT("/"));
			// 过滤掉空字符串（路径以 / 开头时会产生空元素）
			PathParts.RemoveAll([](const FString& Part) { return Part.IsEmpty(); });

		if (PathParts.Num() == 0) return;

		FString CurrentPath;
		TSharedPtr<FDiffTreeNode> CurrentNode;

		for (int32 i = 0; i < PathParts.Num() - 1; i++)
		{
			CurrentPath = CurrentPath.IsEmpty() ? PathParts[i] : CurrentPath + TEXT("/") + PathParts[i];

			if (!PathToNode.Contains(CurrentPath))
			{
				TSharedPtr<FDiffTreeNode> FolderNode = MakeShareable(new FDiffTreeNode);
				FolderNode->Name = PathParts[i];
				FolderNode->FullPath = CurrentPath;
				FolderNode->bIsFolder = true;
				FolderNode->ChangeType = EHotUpdateFileChangeType::Unchanged;
				PathToNode.Add(CurrentPath, FolderNode);
				AllNodes.Add(CurrentPath, FolderNode);

				if (CurrentNode.IsValid())
				{
					FolderNode->Parent = CurrentNode;
					CurrentNode->Children.Add(FolderNode);
				}
				else
				{
					RootNodes.Add(FolderNode);
				}
			}

			CurrentNode = PathToNode[CurrentPath];
		}

		TSharedPtr<FDiffTreeNode> FileNode = MakeShareable(new FDiffTreeNode);
		FileNode->Name = PathParts.Last();
		FileNode->FullPath = Path;
		FileNode->bIsFolder = false;
		FileNode->ChangeType = ChangeType;
		FileNode->DiffInfo = Diff;

		if (CurrentNode.IsValid())
		{
			FileNode->Parent = CurrentNode;
			CurrentNode->Children.Add(FileNode);
		}
		else
		{
			RootNodes.Add(FileNode);
		}

		PathToNode.Add(Path, FileNode);
		AllNodes.Add(Path, FileNode);
	};

	for (const auto& Diff : DiffReport.AddedAssets)
	{
		AddDiffToTree(Diff, EHotUpdateFileChangeType::Added);
	}
	for (const auto& Diff : DiffReport.ModifiedAssets)
	{
		AddDiffToTree(Diff, EHotUpdateFileChangeType::Modified);
	}
	for (const auto& Diff : DiffReport.DeletedAssets)
	{
		AddDiffToTree(Diff, EHotUpdateFileChangeType::Deleted);
	}
	for (const auto& Diff : DiffReport.UnchangedAssets)
	{
		AddDiffToTree(Diff, EHotUpdateFileChangeType::Unchanged);
	}
	}

const FSlateBrush* SHotUpdateVersionDiffPanel::GetTreeNodeIcon(TSharedPtr<FDiffTreeNode> Node) const
{
	if (!Node.IsValid()) return nullptr;

	if (Node->bIsFolder)
	{
		return FAppStyle::GetBrush("ContentBrowser.AssetTreeFolderClosed");
	}

	return FAppStyle::GetBrush(FHotUpdateDiffTool::GetAssetIconName(Node->DiffInfo.AssetPath));
}

FSlateColor SHotUpdateVersionDiffPanel::GetTreeNodeColor(TSharedPtr<FDiffTreeNode> Node) const
{
	if (!Node.IsValid()) return FSlateColor::UseForeground();

	switch (Node->ChangeType)
	{
	case EHotUpdateFileChangeType::Added:
		return FHotUpdateEditorStyle::GetAddedColor();
	case EHotUpdateFileChangeType::Modified:
		return FHotUpdateEditorStyle::GetModifiedColor();
	case EHotUpdateFileChangeType::Deleted:
		return FHotUpdateEditorStyle::GetDeletedColor();
	default:
		return FSlateColor::UseForeground();
	}
}

TSharedRef<ITableRow> SHotUpdateVersionDiffPanel::OnGenerateTreeRow(TSharedPtr<FDiffTreeNode> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SHotUpdateDiffTreeItem, OwnerTable).Item(Item);
}

void SHotUpdateVersionDiffPanel::OnGetTreeChildren(TSharedPtr<FDiffTreeNode> InParent, TArray<TSharedPtr<FDiffTreeNode>>& OutChildren)
{
	if (InParent.IsValid())
	{
		OutChildren = InParent->Children;
	}
	else
	{
		OutChildren = RootNodes;
	}
}

void SHotUpdateVersionDiffPanel::OnTreeSelectionChanged(TSharedPtr<FDiffTreeNode> SelectedItem, ESelectInfo::Type SelectInfo)
{
	SelectedNode = SelectedItem;
	UpdateDetailsPanel(SelectedItem);
}

void SHotUpdateVersionDiffPanel::OnTreeViewDoubleClick(TSharedPtr<FDiffTreeNode> Item)
{
	if (Item.IsValid() && !Item->bIsFolder)
	{
		OpenSelectedAsset(Item);
	}
}

void SHotUpdateVersionDiffPanel::OpenSelectedAsset(TSharedPtr<FDiffTreeNode> Node)
{
	if (!Node.IsValid() || Node->bIsFolder) return;

	FString AssetPath = Node->DiffInfo.AssetPath;

	if (AssetPath.StartsWith(TEXT("/Game/")))
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(AssetPath);
	}
	else
	{
		FPlatformProcess::LaunchFileInDefaultExternalApplication(*AssetPath);
	}
}

void SHotUpdateVersionDiffPanel::UpdateDetailsPanel(TSharedPtr<FDiffTreeNode> Node)
{
	DetailsBox->ClearChildren();

	if (!Node.IsValid()) return;

	auto AddDetailRow = [this](const FString& Label, const FString& Value)
	{
		DetailsBox->AddSlot()
		.AutoHeight()
		.Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(0.4f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Label + ":"))
				.ColorAndOpacity(FLinearColor::Gray)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(0.6f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Value))
			]
		];
	};

	AddDetailRow(TEXT("名称"), Node->Name);
	AddDetailRow(TEXT("完整路径"), Node->FullPath);
	AddDetailRow(TEXT("类型"), Node->bIsFolder ? TEXT("文件夹") : Node->DiffInfo.AssetType);

	if (!Node->bIsFolder)
	{
		static TMap<EHotUpdateFileChangeType, FString> ChangeTypeNames = {
			{ EHotUpdateFileChangeType::Added, TEXT("新增") },
			{ EHotUpdateFileChangeType::Modified, TEXT("修改") },
			{ EHotUpdateFileChangeType::Deleted, TEXT("删除") },
			{ EHotUpdateFileChangeType::Unchanged, TEXT("未变更") }
		};

		AddDetailRow(TEXT("变更类型"), ChangeTypeNames.Contains(Node->ChangeType) ? ChangeTypeNames[Node->ChangeType] : TEXT("未知"));

		if (Node->ChangeType != EHotUpdateFileChangeType::Added)
		{
			AddDetailRow(TEXT("原大小"), FHotUpdateDiffTool::FormatFileSize(Node->DiffInfo.OldSize));
			AddDetailRow(TEXT("原Hash"), Node->DiffInfo.OldHash);
		}

		if (Node->ChangeType != EHotUpdateFileChangeType::Deleted)
		{
			AddDetailRow(TEXT("新大小"), FHotUpdateDiffTool::FormatFileSize(Node->DiffInfo.NewSize));
			AddDetailRow(TEXT("新Hash"), Node->DiffInfo.NewHash);
		}

		if (!Node->DiffInfo.ChangeDescription.IsEmpty())
		{
			AddDetailRow(TEXT("描述"), Node->DiffInfo.ChangeDescription);
		}
	}
}

void SHotUpdateVersionDiffPanel::ApplyFilter(const FString& FilterText)
{
	// TODO: 实现实际的过滤逻辑，当前仅刷新树视图
	DiffTreeView->RequestTreeRefresh();
}

void SHotUpdateVersionDiffPanel::SetDiffReport(const FHotUpdateDiffReport& InReport)
{
	DiffReport = InReport;
	GenerateTreeNodes();
	bIsLoaded = true;

	if (DiffTreeView.IsValid())
	{
		DiffTreeView->RequestTreeRefresh();

		// 自动展开所有文件夹节点
		for (const auto& Pair : AllNodes)
		{
			if (Pair.Value.IsValid() && Pair.Value->bIsFolder)
			{
				DiffTreeView->SetItemExpansion(Pair.Value, true);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE