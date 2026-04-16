// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "HotUpdateEditorTypes.h"

class UHotUpdateBasePackageBuilder;
class UHotUpdatePatchPackageBuilder;
class UHotUpdatePackagingCallbackHandler;
class SProgressBar;

/**
 * 热更新打包面板
 * 优化后的更新版本界面
 */
class HOTUPDATEEDITOR_API SHotUpdatePackagingPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SHotUpdatePackagingPanel) {}
		SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** 清理 Root 引用 */
	void CleanupRootReferences();

	/** 设置要打包的资源路径 */
	void SetAssetPaths(const TArray<FString>& InPaths);

	/** 设置打包类型 */
	void SetPackageType(EHotUpdatePackageType InType);

	/** 打包完成回调 */
	void OnPackagingComplete(const FHotUpdatePackageResult& Result);

	/** 打包进度回调 */
	void OnPackagingProgress(const FHotUpdatePackageProgress& Progress);

	/** 刷新已保存的基础版本列表 */
	void RefreshSavedBaseVersions();

private:
	/** 创建左侧配置面板 */
	TSharedRef<SWidget> CreateLeftPanel();

	/** 创建右侧资源和进度面板 */
	TSharedRef<SWidget> CreateRightPanel();

	/** 创建基础配置区域 */
	TSharedRef<SWidget> CreateBasicSettings();

	/** 创建高级配置区域 */
	TSharedRef<SWidget> CreateAdvancedSettings();

	/** 创建增量打包配置区域 */
	TSharedRef<SWidget> CreateIncrementalSettings();

	/** 创建资源列表区域 */
	TSharedRef<SWidget> CreateAssetList();

	/** 创建进度和操作区域 */
	TSharedRef<SWidget> CreateProgressAndActions();

	/** 关闭窗口 */
	FReply OnCloseClicked();

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

	/** 获取资源信息文本 */
	FText GetAssetInfoText() const;

	/** 平台选择相关 */
	TSharedRef<SWidget> GeneratePlatformComboBoxItem(TSharedPtr<EHotUpdatePlatform> InItem);
	void OnPlatformSelected(TSharedPtr<EHotUpdatePlatform> InItem, ESelectInfo::Type SelectInfo);
	FText GetSelectedPlatformText() const;

	/** Android 纹理格式选择相关 */
	TSharedRef<SWidget> GenerateAndroidTextureFormatComboBoxItem(TSharedPtr<EHotUpdateAndroidTextureFormat> InItem);
	void OnAndroidTextureFormatSelected(TSharedPtr<EHotUpdateAndroidTextureFormat> InItem, ESelectInfo::Type SelectInfo);
	FText GetSelectedAndroidTextureFormatText() const;
	EVisibility GetAndroidTextureFormatVisibility() const;

	/** 打包类型选择相关 */
	TSharedRef<SWidget> GeneratePackageTypeComboBoxItem(TSharedPtr<EHotUpdatePackageType> InItem);
	void OnPackageTypeSelected(TSharedPtr<EHotUpdatePackageType> InItem, ESelectInfo::Type SelectInfo);
	FText GetSelectedPackageTypeText() const;

	/** 分包策略选择相关 */
	TSharedRef<SWidget> GenerateChunkStrategyComboBoxItem(TSharedPtr<EHotUpdateChunkStrategy> InItem);
	void OnChunkStrategySelected(TSharedPtr<EHotUpdateChunkStrategy> InItem, ESelectInfo::Type SelectInfo);
	FText GetSelectedChunkStrategyText() const;

	/** 创建分包配置区域 */
	TSharedRef<SWidget> CreateChunkSettings();

	/** 创建打包模式设置区域 */
	TSharedRef<SWidget> CreatePackagingModeSettings();

	/** 资源选择相关 */
	FReply OnSelectAssetsClicked();
	FReply OnSelectDirectoriesClicked();
	FReply OnSelectNonAssetFilesClicked();
	FReply OnClearSelectionClicked();
	TSharedRef<ITableRow> GenerateAssetListItem(TSharedPtr<FString> InItem, const TSharedRef<STableViewBase>& OwnerTable);
	FReply OnRemoveAssetClicked(TSharedPtr<FString> InItem);

	/** 更新资源列表显示 */
	void RefreshAssetList();
	bool CanSelectAssets() const;
	bool CanSelectDirectories() const;
	bool CanSelectNonAssetFiles() const;

	/** 更新进度条 */
	void UpdateProgressBar(float Percent);

	/** 从UI更新打包配置 */
	void UpdatePackageConfigFromUI();

	/** 显示通知 */
	void ShowNotification(const FText& Message, SNotificationItem::ECompletionState State);

	/** 显示成功通知（带超链接） */
	void ShowSuccessNotification(const FText& Message, const FString& OutputPath);

	/** 显示错误通知（带按钮） */
	void ShowErrorNotification(const FText& Message);

	/** 显示进度通知 */
	void ShowProgressNotification(const FText& Message, bool bShowCancelButton);

private:
	/** 父窗口 */
	TSharedPtr<SWindow> ParentWindow;

	/** 打包配置 */
	FHotUpdatePackageConfig PackageConfig;

	/** 资源路径列表 */
	TArray<FString> AssetPaths;

	/** 基础包构建器 */
	TObjectPtr<UHotUpdateBasePackageBuilder> BasePackageBuilder;

	/** 更新包构建器 */
	TObjectPtr<UHotUpdatePatchPackageBuilder> PatchPackageBuilder;

	/** 回调处理器 */
	TObjectPtr<UHotUpdatePackagingCallbackHandler> CallbackHandler;

	/** 是否正在打包 */
	bool bIsPackaging;

	/** 进度通知 */
	TSharedPtr<SNotificationItem> ProgressNotification;

	// ===== UI控件 =====
	/** 版本号输入框 */
	TSharedPtr<SEditableText> VersionTextBox;

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

	/** 打包类型下拉框 */
	TSharedPtr<SComboBox<TSharedPtr<EHotUpdatePackageType>>> PackageTypeComboBox;

	/** 打包类型选项列表 */
	TArray<TSharedPtr<EHotUpdatePackageType>> PackageTypeOptions;

	/** 当前选择的打包类型 */
	TSharedPtr<EHotUpdatePackageType> SelectedPackageType;

	/** 资源列表视图 */
	TSharedPtr<SListView<TSharedPtr<FString>>> AssetListView;

	/** 资源列表项（用于显示） */
	TArray<TSharedPtr<FString>> AssetListItems;

	/** 打包按钮 */
	TSharedPtr<SButton> PackageButton;

	/** 取消按钮 */
	TSharedPtr<SButton> CancelButton;

	/** 分包策略下拉框 */
	TSharedPtr<SComboBox<TSharedPtr<EHotUpdateChunkStrategy>>> ChunkStrategyComboBox;

	/** 分包策略选项列表 */
	TArray<TSharedPtr<EHotUpdateChunkStrategy>> ChunkStrategyOptions;

	/** 当前选择的分包策略 */
	TSharedPtr<EHotUpdateChunkStrategy> SelectedChunkStrategy;

	/** 按大小分包的最大 Chunk 大小输入框 */
	TSharedPtr<SSpinBox<float>> MaxChunkSizeSpinBox;

	/** 打包模式下拉框 */
	TSharedPtr<SComboBox<TSharedPtr<EHotUpdatePackagingMode>>> PackagingModeComboBox;
	/** 跳过 Cook 复选框 */
	TSharedPtr<SCheckBox> SkipCookCheckBox;

	/** 打包模式选项列表 */
	TArray<TSharedPtr<EHotUpdatePackagingMode>> PackagingModeOptions;

	/** 当前选择的打包模式 */
	TSharedPtr<EHotUpdatePackagingMode> SelectedPackagingMode;

	/** 版本选择下拉框 */
	TSharedPtr<SComboBox<TSharedPtr<FHotUpdateVersionSelectItem>>> VersionSelectComboBox;

	/** 版本选择选项列表 */
	TArray<TSharedPtr<FHotUpdateVersionSelectItem>> VersionSelectOptions;

	/** 当前选择的版本 */
	TSharedPtr<FHotUpdateVersionSelectItem> SelectedVersion;

private:
	/** 刷新版本选择选项 */
	void RefreshVersionSelectOptions();
};