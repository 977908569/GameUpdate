// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "HotUpdateEditorTypes.h"
#include "HotUpdateVersionManager.h"

template<typename ItemType> class SListView;
class ITableRow;
class STableViewBase;

/**
 * 版本管理面板
 * 显示所有已注册版本，支持删除操作
 */
class HOTUPDATEEDITOR_API SHotUpdateVersionManagementPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SHotUpdateVersionManagementPanel) {}
		SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** 刷新版本列表 */
	void RefreshVersionList();

	/** 删除指定版本 */
	void DeleteVersion(TSharedPtr<FHotUpdateVersionSelectItem> VersionItem);

private:
	/** 创建工具栏 */
	TSharedRef<SWidget> CreateToolBar();

	/** 创建版本列表 */
	TSharedRef<SWidget> CreateVersionList();

	/** 创建状态栏 */
	TSharedRef<SWidget> CreateStatusBar();

	/** 生成版本列表行 */
	TSharedRef<ITableRow> OnGenerateVersionListRow(TSharedPtr<FHotUpdateVersionSelectItem> InItem, const TSharedRef<STableViewBase>& OwnerTable);

	/** 版本列表选择变化 */
	void OnVersionListSelectionChanged(TSharedPtr<FHotUpdateVersionSelectItem> SelectedItem, ESelectInfo::Type SelectInfo);

	/** 平台选择回调 */
	void OnPlatformSelected(TSharedPtr<EHotUpdatePlatform> InItem, ESelectInfo::Type SelectInfo);

	/** 生成平台下拉选项 */
	TSharedRef<SWidget> GeneratePlatformComboBoxItem(TSharedPtr<EHotUpdatePlatform> InItem);

	/** 获取当前选中的平台文本 */
	FText GetSelectedPlatformText() const;

	/** 刷新按钮回调 */
	FReply OnRefreshClicked();

	/** 显示删除确认对话框 */
	bool ShowDeleteConfirmationDialog(const FString& VersionString);

	/** 更新统计信息 */
	void UpdateStatistics();

private:
	/** 父窗口 */
	TSharedPtr<SWindow> ParentWindow;

	/** 版本管理器 */
	FHotUpdateVersionManager VersionManager;

	/** 版本列表数据 */
	TArray<TSharedPtr<FHotUpdateVersionSelectItem>> VersionListItems;

	/** 版本列表视图 */
	TSharedPtr<SListView<TSharedPtr<FHotUpdateVersionSelectItem>>> VersionListView;

	/** 平台选择下拉框 */
	TSharedPtr<SComboBox<TSharedPtr<EHotUpdatePlatform>>> PlatformComboBox;

	/** 平台选项列表 */
	TArray<TSharedPtr<EHotUpdatePlatform>> PlatformOptions;

	/** 当前选中的平台 */
	TSharedPtr<EHotUpdatePlatform> SelectedPlatform;

	/** 统计信息文本 */
	TSharedPtr<STextBlock> StatisticsText;

	/** 当前选中的版本 */
	TSharedPtr<FHotUpdateVersionSelectItem> SelectedVersion;
};