// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SSearchBox.h"
#include "Core/HotUpdateTypes.h"

template<typename ItemType> class SListView;
class ITableRow;
class STableViewBase;

/**
 * Pak 文件查看器面板
 */
class HOTUPDATEEDITOR_API SHotUpdatePakViewerPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SHotUpdatePakViewerPanel) {}
		SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** 刷新 Pak 文件列表 */
	void RefreshPakList();

private:
	/** 创建顶部工具栏 */
	TSharedRef<SWidget> CreateToolBar();

	/** 创建 Pak 列表区域 */
	TSharedRef<SWidget> CreatePakListSection();

	/** 创建内容列表区域 */
	TSharedRef<SWidget> CreateContentListSection();

	/** 创建底部状态栏 */
	TSharedRef<SWidget> CreateStatusBar();

	/** 浏览 Pak 文件 */
	FReply OnBrowsePakFile();

	/** 选择 Pak 文件 */
	void OnPakSelected(TSharedPtr<FString> InPakPath, ESelectInfo::Type SelectInfo);

	/** 获取 Pak 列表行 */
	TSharedRef<ITableRow> OnGeneratePakListRow(TSharedPtr<FString> InItem, const TSharedRef<STableViewBase>& OwnerTable);

	/** 获取内容列表行 */
	TSharedRef<ITableRow> OnGenerateContentListRow(TSharedPtr<FHotUpdatePakEntry> InItem, const TSharedRef<STableViewBase>& OwnerTable);

	/** 搜索过滤 */
	void OnSearchChanged(const FText& InSearchText);

	/** 判断条目是否匹配搜索 */
	bool DoesEntryMatchSearch(const FHotUpdatePakEntry& Entry, const FString& InSearchText) const;

	/** 更新内容列表 */
	void UpdateContentList();

	/** 导出内容列表 */
	FReply OnExportContentList();

private:
	/** 父窗口 */
	TSharedPtr<SWindow> ParentWindow;

	/** 当前选中的 Pak 路径 */
	FString SelectedPakPath;

	/** Pak 文件列表 */
	TArray<TSharedPtr<FString>> PakList;

	/** Pak 列表视图 */
	TSharedPtr<SListView<TSharedPtr<FString>>> PakListView;

	/** 完整的内容列表 */
	TArray<FHotUpdatePakEntry> AllContentEntries;

	/** 过滤后的内容列表 */
	TArray<TSharedPtr<FHotUpdatePakEntry>> FilteredContentEntries;

	/** 内容列表视图 */
	TSharedPtr<SListView<TSharedPtr<FHotUpdatePakEntry>>> ContentListView;

	/** 搜索框 */
	TSharedPtr<SSearchBox> SearchBox;

	/** 搜索文本 */
	FString SearchText;

	/** 当前 Pak 文件数量 */
	int32 CurrentFileCount;

	/** 当前 Pak 总大小 */
	int64 CurrentTotalSize;
};