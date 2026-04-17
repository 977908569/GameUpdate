// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

// 日志分类（可通过 ConsoleVariables.ini 调整）
// log.LogHotUpdate off - 关闭所有日志
// log.LogHotUpdate Warning - 只输出警告和错误
// log.LogHotUpdate All - 输出所有日志（包括详细调试信息）
DECLARE_LOG_CATEGORY_EXTERN(LogHotUpdate, Verbose, All);

// 公共头文件包含
#include "Core/HotUpdateTypes.h"
#include "Core/HotUpdateSettings.h"
#include "Core/HotUpdateManager.h"