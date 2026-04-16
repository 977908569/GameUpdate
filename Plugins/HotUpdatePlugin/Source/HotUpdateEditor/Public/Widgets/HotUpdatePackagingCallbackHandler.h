// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HotUpdateEditorTypes.h"
#include "HotUpdatePackagingCallbackHandler.generated.h"

class SHotUpdatePackagingPanel;

/**
 * 打包回调处理器 - 用于接收动态委托回调
 */
UCLASS()
class HOTUPDATEEDITOR_API UHotUpdatePackagingCallbackHandler : public UObject
{
	GENERATED_BODY()
public:
	// 用于 Panel 的回调目标
	TWeakPtr<SHotUpdatePackagingPanel> OwnerPanel;

	// 更新包完成回调
	UFUNCTION()
	void OnPatchPackageComplete(const FHotUpdatePatchPackageResult& Result);

	// 进度回调（通用）
	UFUNCTION()
	void OnPackagingProgress(const FHotUpdatePackageProgress& Progress);
};