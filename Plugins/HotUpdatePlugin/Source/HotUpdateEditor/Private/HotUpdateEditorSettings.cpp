// Copyright czm. All Rights Reserved.

#include "HotUpdateEditorSettings.h"
#include "HotUpdateEditor.h"

#define LOCTEXT_NAMESPACE "HotUpdateEditorSettings"

UHotUpdateEditorSettings::UHotUpdateEditorSettings()
	: DefaultOutputFormat(EHotUpdateOutputFormat::Pak)
	, bDefaultEnableCompression(true)
	, DefaultCompressionLevel(4)
	, bDefaultEncryptIndex(false)
	, bDefaultEncryptContent(false)
	, NextAutoChunkId(1000)
	, bVerboseLogging(false)
	, bOpenOutputDirectoryAfterPackage(true)
	, bDefaultEnableMinimalPackage(false)
	, DefaultDependencyStrategy(EHotUpdateDependencyStrategy::HardOnly)
{
	// 设置默认输出目录
	DefaultOutputDirectory.Path = FPaths::ProjectSavedDir() / TEXT("HotUpdatePackages");
}

#if WITH_EDITOR
FText UHotUpdateEditorSettings::GetSectionText() const
{
	return LOCTEXT("SectionText", "热更新");
}

FText UHotUpdateEditorSettings::GetSectionDescription() const
{
	return LOCTEXT("SectionDescription", "配置热更新打包和版本比较的默认设置");
}
#endif

UHotUpdateEditorSettings* UHotUpdateEditorSettings::Get()
{
	return GetMutableDefault<UHotUpdateEditorSettings>();
}

FHotUpdateMinimalPackageConfig UHotUpdateEditorSettings::GetDefaultMinimalPackageConfig() const
{
	FHotUpdateMinimalPackageConfig Config;
	Config.bEnableMinimalPackage = bDefaultEnableMinimalPackage;
	Config.DependencyStrategy = DefaultDependencyStrategy;
	Config.WhitelistDirectories = DefaultWhitelistDirectories;
	return Config;
}

int32 UHotUpdateEditorSettings::AllocateChunkId()
{
	int32 ChunkId = NextAutoChunkId;
	NextAutoChunkId++;

	// 保存设置
	SaveConfig();

	if (bVerboseLogging)
	{
		UE_LOG(LogHotUpdateEditor, Log, TEXT("分配 Chunk ID: %d"), ChunkId);
	}

	return ChunkId;
}

#undef LOCTEXT_NAMESPACE