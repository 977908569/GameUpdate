// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

// 日志分类
DECLARE_LOG_CATEGORY_EXTERN(LogHotUpdateEditor, Verbose, All);

/** 热更新工具待定数据，用于跨入口点传递参数到 Nomad Tab */
struct FHotUpdatePendingData
{
	static int32 InitialTab;

	/** 标记 Tab 已关闭，需要重新注册 spawner */
	static bool bNeedReRegisterSpawner;
};

/**
 * 打开热更新工具窗口
 * 处理 Tab 关闭后重新注册 spawner 的逻辑，所有入口点都应使用此函数
 * @param InitialTab 初始显示的子标签索引（0=基础版本, 1=更新版本, 2=自定义打包, 3=版本比较, 4=Pak查看器）
 */
HOTUPDATEEDITOR_API void HotUpdateOpenTab(int32 InitialTab = 0);