// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Framework/Docking/TabManager.h"
#include "HotUpdateEditorTypes.h"

class SHotUpdatePackagingPanel;
class SHotUpdateCustomPackagingPanel;
class SHotUpdateVersionDiffPanel;
class SHotUpdatePakViewerPanel;
class SHotUpdateBaseVersionPanel;

/** 子标签 ID 命名空间 */
namespace HotUpdateTabIds
{
	static const FName BaseVersion("HotUpdate_BaseVersion");
	static const FName Packaging("HotUpdate_Packaging");
	static const FName CustomPackaging("HotUpdate_CustomPackaging");
	static const FName VersionDiff("HotUpdate_VersionDiff");
	static const FName PakViewer("HotUpdate_PakViewer");
}

/**
 * 热更新工具主窗口
 * 整合更新版本、自定义打包、版本比较工具和 Pak 查看器的统一入口
 * 使用 FTabManager 管理子标签，支持停靠和拖拽拆分
 */
class HOTUPDATEEDITOR_API SHotUpdateMainWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SHotUpdateMainWindow) {}
	SLATE_END_ARGS()

	/** 构造函数，需要传入 MajorTab 和所属窗口（参考 Session Frontend 模式） */
	void Construct(const FArguments& InArgs, const TSharedRef<SDockTab>& InMajorTab, const TSharedPtr<SWindow>& InParentWindow);

	/** 设置初始要显示的 Tab */
	void SetInitialTab(int32 TabIndex);

	/** 创建默认标签布局 */
	static TSharedRef<FTabManager::FLayout> CreateDefaultLayout();

	/** Window 菜单回调，用于列出可恢复的标签页 */
	static void FillWindowMenu(FMenuBuilder& MenuBuilder, const TSharedPtr<FTabManager> TabManager);

private:
	/** 注册子标签生成器 */
	void RegisterTabSpawners(const TSharedPtr<FTabManager>& InTabManager);

	/** 子标签生成回调 */
	TSharedRef<SDockTab> OnSpawnBaseVersionTab(const FSpawnTabArgs& Args) const;
	TSharedRef<SDockTab> OnSpawnPackagingTab(const FSpawnTabArgs& Args) const;
	TSharedRef<SDockTab> OnSpawnCustomPackagingTab(const FSpawnTabArgs& Args) const;
	TSharedRef<SDockTab> OnSpawnVersionDiffTab(const FSpawnTabArgs& Args) const;
	TSharedRef<SDockTab> OnSpawnPakViewerTab(const FSpawnTabArgs& Args) const;

	/** 获取用于原生对话框的最佳父窗口 */
	TSharedPtr<SWindow> GetBestParentWindow() const;

private:
	/** 子标签管理器 */
	TSharedPtr<FTabManager> TabManager;

	/** 所属的 MajorTab */
	TSharedPtr<SDockTab> MajorTab;

	/** 所属窗口 */
	TSharedPtr<SWindow> ParentWindow;

	/** 打包面板（更新版本） */
	TSharedPtr<SHotUpdatePackagingPanel> PackagingPanel;

	/** 自定义打包面板 */
	TSharedPtr<SHotUpdateCustomPackagingPanel> CustomPackagingPanel;

	/** 基础版本打包面板 */
	TSharedPtr<SHotUpdateBaseVersionPanel> BaseVersionPanel;

	/** 版本比较面板 */
	TSharedPtr<SHotUpdateVersionDiffPanel> VersionDiffPanel;

	/** Pak 查看器面板 */
	TSharedPtr<SHotUpdatePakViewerPanel> PakViewerPanel;
};