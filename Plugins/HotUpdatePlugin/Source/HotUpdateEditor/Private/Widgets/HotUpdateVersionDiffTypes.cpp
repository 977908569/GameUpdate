// Copyright czm. All Rights Reserved.

#include "Widgets/HotUpdateVersionDiffTypes.h"
#include "HotUpdateDiffTool.h"
#include "EditorStyleSet.h"

#define LOCTEXT_NAMESPACE "HotUpdateVersionDiff"

// ==================== SHotUpdateDiffTreeItem ====================

void SHotUpdateDiffTreeItem::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView)
{
	Item = InArgs._Item;

	SMultiColumnTableRow<TSharedPtr<FDiffTreeNode>>::Construct(
		FSuperRowType::FArguments().Padding(FMargin(4, 2)),
		InOwnerTableView
	);
}

TSharedRef<SWidget> SHotUpdateDiffTreeItem::GenerateWidgetForColumn(const FName& ColumnName)
{
	if (ColumnName == "Name")
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SExpanderArrow, SharedThis(this))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 4, 0)
			[
				SNew(SImage)
				.Image(GetIcon())
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(STextBlock)
				.Text(GetDisplayText())
				.ColorAndOpacity(GetColorAndOpacity())
			];
	}
	else if (ColumnName == "Type")
	{
		return SNew(STextBlock)
			.Text(FText::FromString(Item.IsValid() ? Item->DiffInfo.AssetType : TEXT("")))
			.ColorAndOpacity(FLinearColor::Gray);
	}
	else if (ColumnName == "Change")
	{
		return SNew(STextBlock)
			.Text(GetChangeTypeText())
			.ColorAndOpacity(GetChangeTypeColor());
	}
	else if (ColumnName == "Size")
	{
		return SNew(STextBlock)
			.Text(GetSizeText())
			.ColorAndOpacity(FLinearColor::Gray);
	}

	return SNullWidget::NullWidget;
}

const FSlateBrush* SHotUpdateDiffTreeItem::GetIcon() const
{
	if (!Item.IsValid()) return nullptr;

	if (Item->bIsFolder)
	{
		return FAppStyle::GetBrush("ContentBrowser.AssetTreeFolderClosed");
	}

	return FAppStyle::GetBrush(FHotUpdateDiffTool::GetAssetIconName(Item->DiffInfo.AssetPath));
}

FSlateColor SHotUpdateDiffTreeItem::GetColorAndOpacity() const
{
	if (!Item.IsValid()) return FSlateColor::UseForeground();

	switch (Item->ChangeType)
	{
	case EHotUpdateFileChangeType::Added:
		return FLinearColor(0.2f, 0.8f, 0.2f);
	case EHotUpdateFileChangeType::Modified:
		return FLinearColor(0.8f, 0.6f, 0.2f);
	case EHotUpdateFileChangeType::Deleted:
		return FLinearColor(0.8f, 0.2f, 0.2f);
	default:
		return FSlateColor::UseForeground();
	}
}

FText SHotUpdateDiffTreeItem::GetDisplayText() const
{
	return Item.IsValid() ? FText::FromString(Item->Name) : FText::GetEmpty();
}

FText SHotUpdateDiffTreeItem::GetChangeTypeText() const
{
	if (!Item.IsValid() || Item->bIsFolder) return FText::GetEmpty();

	static TMap<EHotUpdateFileChangeType, FText> ChangeTypeTexts = {
		{ EHotUpdateFileChangeType::Added, LOCTEXT("Added", "新增") },
		{ EHotUpdateFileChangeType::Modified, LOCTEXT("Modified", "修改") },
		{ EHotUpdateFileChangeType::Deleted, LOCTEXT("Deleted", "删除") },
		{ EHotUpdateFileChangeType::Unchanged, LOCTEXT("Unchanged", "未变更") }
	};

	return ChangeTypeTexts.Contains(Item->ChangeType) ? ChangeTypeTexts[Item->ChangeType] : FText::GetEmpty();
}

FSlateColor SHotUpdateDiffTreeItem::GetChangeTypeColor() const
{
	if (!Item.IsValid()) return FSlateColor::UseForeground();

	switch (Item->ChangeType)
	{
	case EHotUpdateFileChangeType::Added:
		return FLinearColor(0.2f, 0.8f, 0.2f);
	case EHotUpdateFileChangeType::Modified:
		return FLinearColor(0.8f, 0.6f, 0.2f);
	case EHotUpdateFileChangeType::Deleted:
		return FLinearColor(0.8f, 0.2f, 0.2f);
	default:
		return FLinearColor::Gray;
	}
}

FText SHotUpdateDiffTreeItem::GetSizeText() const
{
	if (!Item.IsValid() || Item->bIsFolder) return FText::GetEmpty();

	int64 Size = Item->DiffInfo.NewSize > 0 ? Item->DiffInfo.NewSize : Item->DiffInfo.OldSize;
	return FText::FromString(FHotUpdateDiffTool::FormatFileSize(Size));
}

#undef LOCTEXT_NAMESPACE