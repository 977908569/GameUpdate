// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "HotUpdateEditorTypes.h"
#include "HotUpdateCommandlet.generated.h"

/**
		 * 热更新打包命令行工具
		 *
		 * 使用方式:
		 * UnrealEditor-Cmd MyProject -run=HotUpdate -mode=base -version=1.0.0 -platform=Windows -output=D:/Output
		 * UnrealEditor-Cmd MyProject -run=HotUpdate -mode=patch -version=1.0.1 -baseversion=1.0.0 -platform=Windows -manifest=D:/Base/manifest.json -output=D:/Output
		 *
		 * 参数说明:
		 * -mode           打包模式: base(完整exe/apk基础包), patch(热更包)
		 * -version        版本号 (如 1.0.0)
		 * -baseversion    基础版本号 (热更包需要)
		 * -platform       目标平台: Windows, Android, IOS
		 * -output         输出目录路径
		 * -manifest       基础版本Manifest文件路径 (热更包需要)
		 * -shipping       是否为发布版本构建 (base 模式)
		 * -skipbuild      是否跳过编译步骤 (base/patch 模式均可使用，编辑器进程内运行时需要跳过)
		 * -minimal        启用最小包模式 (base 模式)
		 * -whitelist      白名单目录，分号分隔 (如 /Game/UI;/Game/Startup)
		 * -chunkstrategy  分包策略: None(不分包), Size(按大小分包)
		 * -chunksize      分包大小，单位MB (默认256)
		 * -help           显示帮助信息
		 */
UCLASS()
class HOTUPDATEEDITOR_API UHotUpdateCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UHotUpdateCommandlet();

	virtual int32 Main(const FString& Params) override;

private:
	/** 解析命令行参数 */
	bool ParseCommandLine(const FString& Params);

	/** 显示帮助信息 */
	static void ShowHelp();

	/** 执行完整基础版本打包（exe/apk） */
	int32 ExecuteBasePackage();

	/** 执行热更包打包 */
	int32 ExecutePatchPackage();

	/** 从命令行参数转换为平台枚举 */
	static EHotUpdatePlatform ParsePlatform(const FString& InPlatformStr);

	/** 从命令行参数转换为 Android 纹理格式枚举 */
	static EHotUpdateAndroidTextureFormat ParseTextureFormat(const FString& InFormatStr);

private:
	// 命令行参数
	FString Mode;               // base 或 patch
	FString Version;            // 版本号
	FString BaseVersion;        // 基础版本号
	FString PlatformStr;        // 平台字符串
	FString OutputDir;          // 输出目录
	FString ManifestPath;       // 基础版本Manifest路径
	bool bShowHelp;             // 是否显示帮助

	// 基础版本构建参数
	bool bIsShipping;           // 是否为发布版本
	bool bSkipBuild;            // 是否跳过编译步骤

	// 最小包配置参数
	bool bEnableMinimalPackage;         // 是否启用最小包模式
	FString WhitelistDirectories;       // 必须包含的目录（分号分隔）
	EHotUpdateChunkStrategy PatchChunkStrategy;  // 非首包资源分包策略
	int32 PatchChunkSizeMB;             // 分包大小（MB）


	// Android 纹理格式参数
	FString TextureFormatStr;      // Android 纹理格式字符串

	// 是否跳过 Cook 步骤
	bool bSkipCook;

	// 是否启用增量 Cook
	bool bIncrementalCook;
};