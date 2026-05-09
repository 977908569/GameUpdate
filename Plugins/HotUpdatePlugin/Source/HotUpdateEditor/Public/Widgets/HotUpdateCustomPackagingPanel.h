// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/HotUpdatePackagingPanelBase.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Input/SSpinBox.h"
#include "HotUpdateEditorTypes.h"

class FHotUpdateCustomPackageBuilder;

/**
 * 自定义打包面板
 * 支持选择 uasset 文件和非资产文件两种模式，快速打出热更包进行功能测试
 */
class HOTUPDATEEDITOR_API SHotUpdateCustomPackagingPanel : public SHotUpdatePackagingPanelBase
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

	/** 取消当前构建（基类纯虚函数实现） */
	virtual void CancelCurrentBuild() override;

	/** 配置字段访问 */
	virtual EHotUpdatePlatform& GetTargetPlatformRef() override { return PackageConfig.Platform; }
	virtual EHotUpdateAndroidTextureFormat& GetTargetTextureFormatRef() override { return PackageConfig.AndroidTextureFormat; }

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
	/** 打包配置 */
	FHotUpdatePackageConfig PackageConfig;

	/** uasset 文件路径列表 */
	TArray<FString> UassetFilePaths;

	/** 非资产文件路径列表 */
	TArray<FString> NonAssetFilePaths;

	/** 更新包构建器 */
	TSharedPtr<FHotUpdateCustomPackageBuilder> CustomPackageBuilder;

	/** uasset 列表视图 */
	TSharedPtr<SListView<TSharedPtr<FString>>> UassetListView;

	/** uasset 列表项 */
	TArray<TSharedPtr<FString>> UassetListItems;

	/** 非资产列表视图 */
	TSharedPtr<SListView<TSharedPtr<FString>>> NonAssetListView;

	/** 非资产列表项 */
	TArray<TSharedPtr<FString>> NonAssetListItems;

	/** 增量 Cook 复选框 */
	TSharedPtr<SCheckBox> IncrementalCookCheckBox;

	/** 跳过编译复选框 */
	TSharedPtr<SCheckBox> SkipBuildCheckBox;

	/** Pak 优先级输入 */
	TSharedPtr<SSpinBox<float>> PakPrioritySpinBox;

	/** 自定义 Pak 名称输入 */
	TSharedPtr<SEditableTextBox> CustomPakNameTextBox;
};