// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HotUpdateEditorTypes.h"

/**
 * 热更新工具函数集合
 */
namespace HotUpdateUtils
{
	/**
	 * 将平台枚举转换为显示名称字符串
	 * @param Platform 平台枚举值
	 * @return 平台显示名称 ("Windows", "Android", "IOS")
	 */
	HOTUPDATEEDITOR_API FString GetPlatformString(EHotUpdatePlatform Platform);

	/**
	 * 将平台枚举转换为目录名称字符串
	 * @param Platform 平台枚举值
	 * @return 平台目录名称 ("Win64", "Android", "IOS")
	 */
	HOTUPDATEEDITOR_API FString GetPlatformDirectoryName(EHotUpdatePlatform Platform);
}