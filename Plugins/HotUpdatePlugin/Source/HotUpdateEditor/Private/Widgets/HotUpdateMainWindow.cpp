// Copyright czm. All Rights Reserved.

#include "Widgets/HotUpdateMainWindow.h"
#include "Widgets/HotUpdatePackagingPanel.h"
#include "Widgets/HotUpdateCustomPackagingPanel.h"
#include "Widgets/HotUpdateBaseVersionPanel.h"
#include "Widgets/HotUpdateVersionDiffPanel.h"
#include "Widgets/HotUpdatePakViewerPanel.h"
#include "HotUpdateEditor.h"
#include "HotUpdateEditorStyle.h"
#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "HotUpdateMainWindow"

void SHotUpdateMainWindow::Construct(const FArguments& InArgs, const TSharedRef<SDockTab>& InMajorTab, const TSharedPtr<SWindow>& InParentWindow)
{
	MajorTab = InMajorTab;
	ParentWindow = InParentWindow;

	// 为子标签创建独立的 TabManager（参考 Session Frontend 模式）
	TabManager = FGlobalTabmanager::Get()->NewTabManager(InMajorTab);

	// 注册子标签生成器
	RegisterTabSpawners(TabManager);

	// 启用窗口菜单栏
	TabManager->SetAllowWindowMenuBar(true);

	// 创建 Window 菜单栏（通过 SetMenuMultiBox 交给 TabManager，嵌入标题栏）
	FMenuBarBuilder MenuBarBuilder = FMenuBarBuilder(TSharedPtr<const FUICommandList>());
	MenuBarBuilder.AddPullDownMenu(
		LOCTEXT("WindowMenuLabel", "Window"),
		FText::GetEmpty(),
		FNewMenuDelegate::CreateStatic(&SHotUpdateMainWindow::FillWindowMenu, TabManager),
		"Window"
	);
	TSharedRef<SWidget> MenuBarWidget = MenuBarBuilder.MakeWidget();
	TabManager->SetMenuMultiBox(MenuBarBuilder.GetMultiBox(), MenuBarWidget);

	// 使用 FTabManager 恢复布局
	TSharedRef<FTabManager::FLayout> Layout = CreateDefaultLayout();

	ChildSlot
	[
		TabManager->RestoreFrom(Layout, InParentWindow).ToSharedRef()
	];
}

void SHotUpdateMainWindow::SetInitialTab(int32 TabIndex)
{
	if (!TabManager.IsValid())
	{
		return;
	}

	FName TabId;
	switch (TabIndex)
	{
	case 0: TabId = HotUpdateTabIds::BaseVersion;    break;
	case 1: TabId = HotUpdateTabIds::Packaging;      break;
	case 2: TabId = HotUpdateTabIds::CustomPackaging; break;
	case 3: TabId = HotUpdateTabIds::VersionDiff;    break;
	case 4: TabId = HotUpdateTabIds::PakViewer;      break;
	default: TabId = HotUpdateTabIds::BaseVersion;   break;
	}

	TabManager->TryInvokeTab(TabId);
}


void SHotUpdateMainWindow::FillWindowMenu(FMenuBuilder& MenuBuilder, const TSharedPtr<FTabManager> TabManager)
{
	if (TabManager.IsValid())
	{
		TabManager->PopulateLocalTabSpawnerMenu(MenuBuilder);
	}
}

TSharedRef<FTabManager::FLayout> SHotUpdateMainWindow::CreateDefaultLayout()
{
	return FTabManager::NewLayout("HotUpdate_MainLayout_v2")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(1.0f)
				->AddTab(HotUpdateTabIds::BaseVersion, ETabState::OpenedTab)
				->AddTab(HotUpdateTabIds::Packaging, ETabState::OpenedTab)
				->AddTab(HotUpdateTabIds::CustomPackaging, ETabState::OpenedTab)
				->AddTab(HotUpdateTabIds::VersionDiff, ETabState::OpenedTab)
				->AddTab(HotUpdateTabIds::PakViewer, ETabState::OpenedTab)
				->SetForegroundTab(HotUpdateTabIds::BaseVersion)
			)
		);
}

void SHotUpdateMainWindow::RegisterTabSpawners(const TSharedPtr<FTabManager>& InTabManager)
{
	// 注册子标签生成器，并添加到 local workspace menu 使 PopulateLocalTabSpawnerMenu 能找到它们
	TSharedRef<FWorkspaceItem> HotUpdateGroup = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("HotUpdateGroup", "热更新工具"));

	InTabManager->RegisterTabSpawner(HotUpdateTabIds::BaseVersion,
		FOnSpawnTab::CreateSP(this, &SHotUpdateMainWindow::OnSpawnBaseVersionTab))
		.SetDisplayName(LOCTEXT("BaseVersionTab", "基础版本"))
		.SetTooltipText(LOCTEXT("BaseVersionTabTooltip", "完整打包生成 exe/apk"))
		.SetGroup(HotUpdateGroup);

	InTabManager->RegisterTabSpawner(HotUpdateTabIds::Packaging,
		FOnSpawnTab::CreateSP(this, &SHotUpdateMainWindow::OnSpawnPackagingTab))
		.SetDisplayName(LOCTEXT("PackagingTab", "更新版本"))
		.SetTooltipText(LOCTEXT("PackagingTabTooltip", "从项目打包配置读取资源进行热更新打包"))
		.SetGroup(HotUpdateGroup);

	InTabManager->RegisterTabSpawner(HotUpdateTabIds::CustomPackaging,
		FOnSpawnTab::CreateSP(this, &SHotUpdateMainWindow::OnSpawnCustomPackagingTab))
		.SetDisplayName(LOCTEXT("CustomPackagingTab", "自定义打包"))
		.SetTooltipText(LOCTEXT("CustomPackagingTabTooltip", "手动选择资源或目录进行热更新打包"))
		.SetGroup(HotUpdateGroup);

	InTabManager->RegisterTabSpawner(HotUpdateTabIds::VersionDiff,
		FOnSpawnTab::CreateSP(this, &SHotUpdateMainWindow::OnSpawnVersionDiffTab))
		.SetDisplayName(LOCTEXT("VersionDiffTab", "版本比较"))
		.SetTooltipText(LOCTEXT("VersionDiffTabTooltip", "比较两个版本的资源差异"))
		.SetGroup(HotUpdateGroup);

	InTabManager->RegisterTabSpawner(HotUpdateTabIds::PakViewer,
		FOnSpawnTab::CreateSP(this, &SHotUpdateMainWindow::OnSpawnPakViewerTab))
		.SetDisplayName(LOCTEXT("PakViewerTab", "Pak 查看器"))
		.SetTooltipText(LOCTEXT("PakViewerTabTooltip", "查看 Pak 包内容"))
		.SetGroup(HotUpdateGroup);
}

TSharedRef<SDockTab> SHotUpdateMainWindow::OnSpawnBaseVersionTab(const FSpawnTabArgs& Args) const
{
	SHotUpdateMainWindow* MutableThis = const_cast<SHotUpdateMainWindow*>(this);
	return SNew(SDockTab)
		.TabRole(PanelTab)
		.Label(LOCTEXT("BaseVersionTab", "基础版本"))
		[
			SAssignNew(MutableThis->BaseVersionPanel, SHotUpdateBaseVersionPanel)
		];
}

TSharedRef<SDockTab> SHotUpdateMainWindow::OnSpawnPackagingTab(const FSpawnTabArgs& Args) const
{
	SHotUpdateMainWindow* MutableThis = const_cast<SHotUpdateMainWindow*>(this);

	return SNew(SDockTab)
		.TabRole(PanelTab)
		.Label(LOCTEXT("PackagingTab", "更新版本"))
		[
			SAssignNew(MutableThis->PackagingPanel, SHotUpdatePackagingPanel)
		];
}

TSharedRef<SDockTab> SHotUpdateMainWindow::OnSpawnCustomPackagingTab(const FSpawnTabArgs& Args) const
{
	SHotUpdateMainWindow* MutableThis = const_cast<SHotUpdateMainWindow*>(this);

	return SNew(SDockTab)
		.TabRole(PanelTab)
		.Label(LOCTEXT("CustomPackagingTab", "自定义打包"))
		[
			SAssignNew(MutableThis->CustomPackagingPanel, SHotUpdateCustomPackagingPanel)
		];
}

TSharedRef<SDockTab> SHotUpdateMainWindow::OnSpawnVersionDiffTab(const FSpawnTabArgs& Args) const
{
	SHotUpdateMainWindow* MutableThis = const_cast<SHotUpdateMainWindow*>(this);
	return SNew(SDockTab)
		.TabRole(PanelTab)
		.Label(LOCTEXT("VersionDiffTab", "版本比较"))
		[
			SAssignNew(MutableThis->VersionDiffPanel, SHotUpdateVersionDiffPanel)
		];
}

TSharedRef<SDockTab> SHotUpdateMainWindow::OnSpawnPakViewerTab(const FSpawnTabArgs& Args) const
{
	SHotUpdateMainWindow* MutableThis = const_cast<SHotUpdateMainWindow*>(this);
	return SNew(SDockTab)
		.TabRole(PanelTab)
		.Label(LOCTEXT("PakViewerTab", "Pak 查看器"))
		[
			SAssignNew(MutableThis->PakViewerPanel, SHotUpdatePakViewerPanel)
		];
}

TSharedPtr<SWindow> SHotUpdateMainWindow::GetBestParentWindow() const
{
	return FSlateApplication::Get().FindBestParentWindowForDialogs(nullptr);
}

#undef LOCTEXT_NAMESPACE