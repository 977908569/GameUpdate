// Copyright czm. All Rights Reserved.

#include "HotUpdate.h"
#include "Modules/ModuleManager.h"

// 日志分类定义
DEFINE_LOG_CATEGORY(LogHotUpdate);

#define LOCTEXT_NAMESPACE "FHotUpdateModule"

class FHotUpdateModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogHotUpdate, Log, TEXT("HotUpdate module started"));
		
	}

	virtual void ShutdownModule() override
	{
		UE_LOG(LogHotUpdate, Log, TEXT("HotUpdate module shutdown"));
	}
	
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FHotUpdateModule, HotUpdate)