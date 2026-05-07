// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/HotUpdatePackagingPanelBase.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "HotUpdateEditorTypes.h"
#include "HotUpdateBaseVersionBuilder.h"

class FHotUpdateBaseVersionBuilder;

/**
 * 基础版本构建面板
 */
class HOTUPDATEEDITOR_API SHotUpdateBaseVersionPanel : public SHotUpdatePackagingPanelBase
{
public:
	SLATE_BEGIN_ARGS(SHotUpdateBaseVersionPanel) {}
		SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	~SHotUpdateBaseVersionPanel();

private:
	/** 创建配置区域 */
	TSharedRef<SWidget> CreateConfigSection();

	/** 创建进度区域 */
	TSharedRef<SWidget> CreateProgressSection();

	/** 开始构建 */
	FReply OnBuildClicked();

	/** 取消构建 */
	FReply OnCancelClicked();

	/** 是否可以构建 */
	bool CanBuild() const;

	/** 构建进度回调 */
	void OnBuildProgress(const FHotUpdateBaseVersionBuildProgress& Progress);

	/** 构建完成回调 */
	void OnBuildComplete(const FHotUpdateBaseVersionBuildResult& Result);

	/** 选择输出目录 */
	FReply OnBrowseOutputDirectory();

	/** 取消当前构建 */
	virtual void CancelCurrentBuild() override;

	/** 配置字段访问 */
	virtual EHotUpdatePlatform& GetTargetPlatformRef() override { return BuildConfig.Platform; }
	virtual EHotUpdateAndroidTextureFormat& GetTargetTextureFormatRef() override { return BuildConfig.AndroidTextureFormat; }

	/** 构建配置选择 */
	TSharedRef<SWidget> GenerateBuildConfigComboBoxItem(TSharedPtr<EHotUpdateBuildConfiguration> InItem);
	void OnBuildConfigSelected(TSharedPtr<EHotUpdateBuildConfiguration> InItem, ESelectInfo::Type SelectInfo);
	FText GetSelectedBuildConfigText() const;


	/** 最小包配置项可见性（启用最小包时才显示子配置） */
	EVisibility GetMinimalPackageSettingsVisibility() const;

	/** 创建最小包配置区域 */
	TSharedRef<SWidget> CreateMinimalPackageSettings();

	/** 最小包配置相关方法 */
	TSharedRef<ITableRow> GenerateWhitelistDirectoryRow(TSharedPtr<FDirectoryPath> InItem, const TSharedRef<STableViewBase>& OwnerTable);
	FReply OnAddWhitelistDirectoryClicked();
	FReply OnRemoveWhitelistDirectoryClicked(TSharedPtr<FDirectoryPath> InItem);
	TSharedRef<SWidget> GenerateDependencyStrategyComboBoxItem(TSharedPtr<EHotUpdateDependencyStrategy> InItem);
	void OnDependencyStrategySelected(TSharedPtr<EHotUpdateDependencyStrategy> InItem, ESelectInfo::Type SelectInfo);
	FText GetSelectedDependencyStrategyText() const;

	/** 分包策略相关方法 */
	TSharedRef<SWidget> GeneratePatchChunkStrategyComboBoxItem(TSharedPtr<EHotUpdateChunkStrategy> InItem);
	void OnPatchChunkStrategySelected(TSharedPtr<EHotUpdateChunkStrategy> InItem, ESelectInfo::Type SelectInfo);
	FText GetSelectedPatchChunkStrategyText() const;
	float GetMaxChunkSizeValue() const;

private:
	/** 构建器 */
	TSharedPtr<FHotUpdateBaseVersionBuilder> Builder;

	/** 构建配置 */
	FHotUpdateBaseVersionBuildConfig BuildConfig;

	// UI 控件
	TSharedPtr<SEditableText> VersionTextBox;
	TSharedPtr<SComboBox<TSharedPtr<EHotUpdateBuildConfiguration>>> BuildConfigComboBox;
	TArray<TSharedPtr<EHotUpdateBuildConfiguration>> BuildConfigOptions;
	TSharedPtr<EHotUpdateBuildConfiguration> SelectedBuildConfig;

	// UI 控件
	TSharedPtr<SButton> BuildButton;
	TSharedPtr<SButton> CancelButton;

	// ===== 最小包配置 UI 控件 =====
	/** 启用最小包模式复选框 */
	TSharedPtr<SCheckBox> EnableMinimalPackageCheckBox;

	/** 依赖策略下拉框 */
	TSharedPtr<SComboBox<TSharedPtr<EHotUpdateDependencyStrategy>>> DependencyStrategyComboBox;

	/** 依赖策略选项列表 */
	TArray<TSharedPtr<EHotUpdateDependencyStrategy>> DependencyStrategyOptions;

	/** 当前选择的依赖策略 */
	TSharedPtr<EHotUpdateDependencyStrategy> SelectedDependencyStrategy;

	/** 必须包含的目录列表视图 */
	TSharedPtr<SListView<TSharedPtr<FDirectoryPath>>> WhitelistDirectoryListView;

	/** 必须包含的目录列表项 */
	TArray<TSharedPtr<FDirectoryPath>> WhitelistDirectoryItems;

	/** 分包策略下拉框 */
	TSharedPtr<SComboBox<TSharedPtr<EHotUpdateChunkStrategy>>> PatchChunkStrategyComboBox;

	/** 分包策略选项列表 */
	TArray<TSharedPtr<EHotUpdateChunkStrategy>> PatchChunkStrategyOptions;

	/** 当前选择的分包策略 */
	TSharedPtr<EHotUpdateChunkStrategy> SelectedPatchChunkStrategy;

	/** 按大小分包的最大 Chunk 大小输入框 */
	TSharedPtr<SSpinBox<float>> MaxChunkSizeSpinBox;
};