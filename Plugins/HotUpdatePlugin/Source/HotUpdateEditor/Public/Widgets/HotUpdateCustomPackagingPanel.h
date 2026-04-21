// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Input/SSpinBox.h"
#include "HotUpdateEditorTypes.h"

class UHotUpdateCustomPackageBuilder;
class SProgressBar;

/**
 * 自定义打包面板
 * 支持选择 uasset 文件和非资产文件两种模式，快速打出热更包进行功能测试
 */
class HOTUPDATEEDITOR_API SHotUpdateCustomPackagingPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SHotUpdateCustomPackagingPanel) {}
		SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	~SHotUpdateCustomPackagingPanel();

	/** 打包完成回调 */
	void OnPackagingComplete(const FHotUpdateCustomPackageResult& Result);

	/** 打包进度回调 */
	void OnPackagingProgress(const FHotUpdatePackageProgress& Progress);

private:
	/** 创建左侧配置面板 */
	TSharedRef<SWidget> CreateLeftPanel();

	/** 创建右侧资源和进度面板 */
	TSharedRef<SWidget> CreateRightPanel();

	/** 创建基础配置区域 */
	TSharedRef<SWidget> CreateBasicSettings();

	/** 创建资源列表区域 */
	TSharedRef<SWidget> CreateAssetList();

	/** 创建进度和操作区域 */
	TSharedRef<SWidget> CreateProgressAndActions();

	/** 开始打包 */
	FReply OnPackageClicked();

	/** 取消打包 */
	FReply OnCancelClicked();

	/** 选择输出目录 */
	FReply OnBrowseOutputDirectory();

	/** 更新打包按钮状态 */
	bool IsPackagingEnabled() const;

	/** 是否正在打包 */
	bool IsPackaging() const { return bIsPackaging; }

	/** 平台选择相关 */
	TSharedRef<SWidget> GeneratePlatformComboBoxItem(TSharedPtr<EHotUpdatePlatform> InItem);
	void OnPlatformSelected(TSharedPtr<EHotUpdatePlatform> InItem, ESelectInfo::Type SelectInfo);
	FText GetSelectedPlatformText() const;

	/** Android 纹理格式选择相关 */
	TSharedRef<SWidget> GenerateAndroidTextureFormatComboBoxItem(TSharedPtr<EHotUpdateAndroidTextureFormat> InItem);
	void OnAndroidTextureFormatSelected(TSharedPtr<EHotUpdateAndroidTextureFormat> InItem, ESelectInfo::Type SelectInfo);
	FText GetSelectedAndroidTextureFormatText() const;
	EVisibility GetAndroidTextureFormatVisibility() const;

	/** uasset 文件选择相关 */
	FReply OnSelectUassetFilesClicked();
	FReply OnClearUassetClicked();
	TSharedRef<ITableRow> GenerateUassetListItem(TSharedPtr<FString> InItem, const TSharedRef<STableViewBase>& OwnerTable);
	FReply OnRemoveUassetClicked(TSharedPtr<FString> InItem);
	void RefreshUassetList();
	FText GetUassetInfoText() const;

	/** 非资产文件选择相关 */
	FReply OnSelectNonAssetFilesClicked();
	FReply OnClearNonAssetClicked();
	TSharedRef<ITableRow> GenerateNonAssetListItem(TSharedPtr<FString> InItem, const TSharedRef<STableViewBase>& OwnerTable);
	FReply OnRemoveNonAssetClicked(TSharedPtr<FString> InItem);
	void RefreshNonAssetList();
	FText GetNonAssetInfoText() const;

	/** 从UI更新打包配置 */
	void UpdatePackageConfigFromUI();

private:
	/** 父窗口 */
	TSharedPtr<SWindow> ParentWindow;

	/** 打包配置 */
	FHotUpdatePackageConfig PackageConfig;

	/** uasset 文件路径列表 */
	TArray<FString> UassetFilePaths;

	/** 非资产文件路径列表 */
	TArray<FString> NonAssetFilePaths;

	/** 更新包构建器 */
	TObjectPtr<UHotUpdateCustomPackageBuilder> CustomPackageBuilder;

	/** 是否正在打包 */
	bool bIsPackaging;

	/** 进度通知 */
	TSharedPtr<SNotificationItem> ProgressNotification;

	// ===== UI控件 =====
	/** 输出目录输入框 */
	TSharedPtr<SEditableText> OutputDirTextBox;

	/** 状态文本 */
	TSharedPtr<STextBlock> StatusTextBlock;

	/** 进度文本 */
	TSharedPtr<STextBlock> ProgressTextBlock;

	/** 进度条 */
	TSharedPtr<SProgressBar> ProgressBar;

	/** 平台选择下拉框 */
	TSharedPtr<SComboBox<TSharedPtr<EHotUpdatePlatform>>> PlatformComboBox;

	/** 平台选项列表 */
	TArray<TSharedPtr<EHotUpdatePlatform>> PlatformOptions;

	/** 当前选择的平台 */
	TSharedPtr<EHotUpdatePlatform> SelectedPlatform;

	/** Android 纹理格式下拉框 */
	TSharedPtr<SComboBox<TSharedPtr<EHotUpdateAndroidTextureFormat>>> AndroidTextureFormatComboBox;

	/** Android 纹理格式选项列表 */
	TArray<TSharedPtr<EHotUpdateAndroidTextureFormat>> AndroidTextureFormatOptions;

	/** 当前选择的 Android 纹理格式 */
	TSharedPtr<EHotUpdateAndroidTextureFormat> SelectedAndroidTextureFormat;

	/** uasset 列表视图 */
	TSharedPtr<SListView<TSharedPtr<FString>>> UassetListView;

	/** uasset 列表项 */
	TArray<TSharedPtr<FString>> UassetListItems;

	/** 非资产列表视图 */
	TSharedPtr<SListView<TSharedPtr<FString>>> NonAssetListView;

	/** 非资产列表项 */
	TArray<TSharedPtr<FString>> NonAssetListItems;

	/** 打包按钮 */
	TSharedPtr<SButton> PackageButton;

	/** 取消按钮 */
	TSharedPtr<SButton> CancelButton;

	/** 跳过 Cook 复选框 */
	TSharedPtr<SCheckBox> SkipCookCheckBox;

	/** 跳过编译复选框 */
	TSharedPtr<SCheckBox> SkipBuildCheckBox;

	/** Pak 优先级输入 */
	TSharedPtr<SSpinBox<float>> PakPrioritySpinBox;
};