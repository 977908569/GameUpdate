// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "HotUpdateSettings.generated.h"

/**
 * 热更新插件设置
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Hot Update Settings"))
class HOTUPDATE_API UHotUpdateSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UHotUpdateSettings();

	// == 服务器配置 ==

	/// 版本检查 URL（latest.json，返回最新版本号和 manifest 地址）
	UPROPERTY(Config, EditAnywhere, Category = "Server", meta = (DisplayName = "Version Check URL"))
	FString ManifestUrl;

	/// 资源下载基础 URL（可包含 {version} 占位符，如 http://8.147.65.56/hotpatch/{version}/Windows/）
	UPROPERTY(Config, EditAnywhere, Category = "Server", meta = (DisplayName = "Resource Base URL"))
	FString ResourceBaseUrl;

	/// 请求超时时间（秒）
	UPROPERTY(Config, EditAnywhere, Category = "Server", meta = (ClampMin = "1", UIMin = "5", UIMax = "60"))
	float RequestTimeout;

	// == 下载配置 ==

	/// 最大并发下载数
	UPROPERTY(Config, EditAnywhere, Category = "Download", meta = (ClampMin = "1", ClampMax = "10", UIMin = "1", UIMax = "6"))
	int32 MaxConcurrentDownloads;

	/// 下载重试次数
	UPROPERTY(Config, EditAnywhere, Category = "Download", meta = (ClampMin = "0", ClampMax = "10"))
	int32 MaxRetryCount;

	/// 重试间隔（秒）
	UPROPERTY(Config, EditAnywhere, Category = "Download", meta = (ClampMin = "1"))
	float RetryInterval;

	/// 是否启用断点续传
	UPROPERTY(Config, EditAnywhere, Category = "Download")
	bool bEnableResume;

	/// 下载超时时间（秒）
	UPROPERTY(Config, EditAnywhere, Category = "Download", meta = (ClampMin = "10"))
	float DownloadTimeout;

	// == 存储配置 ==

	/// 本地 Pak 存储相对路径
	UPROPERTY(Config, EditAnywhere, Category = "Storage", meta = (DisplayName = "Local Pak Directory"))
	FString LocalPakDirectory;

	/// 最大本地版本保留数
	UPROPERTY(Config, EditAnywhere, Category = "Storage", meta = (ClampMin = "1", ClampMax = "10"))
	int32 MaxLocalVersionCount;

	/// 是否自动清理旧版本
	UPROPERTY(Config, EditAnywhere, Category = "Storage")
	bool bAutoCleanupOldVersions;

	// == 行为配置 ==

	/// 启动时自动检查更新
	UPROPERTY(Config, EditAnywhere, Category = "Behavior")
	bool bAutoCheckOnStartup;

	/// 检测到更新后自动开始下载
	UPROPERTY(Config, EditAnywhere, Category = "Behavior")
	bool bAutoDownload;

	/// 获取本地 Pak 存储完整路径
	FString GetLocalPakFullPath() const;

	/// 获取默认设置
	static UHotUpdateSettings* Get();

	/// 验证 URL 是否有效且安全
	static bool ValidateUrl(const FString& Url, FString& OutErrorMessage);

	/// 检查是否允许 HTTP（非 HTTPS）连接
	static bool IsHttpAllowed();

	// == 最小包模式配置（打包时使用）==

	/// 是否启用最小包模式
	UPROPERTY(Config, EditAnywhere, Category = "MinimalPackage")
	bool bEnableMinimalPackage;

	/// 必须包含的目录（这些资源将打包到 Chunk 0）
	UPROPERTY(Config, EditAnywhere, Category = "MinimalPackage")
	TArray<FString> WhitelistDirectories;


protected:
	/// 允许的域名白名单（留空表示允许所有）
	UPROPERTY(Config, EditAnywhere, Category = "Server", meta = (DisplayName = "Allowed Domains"))
	TArray<FString> AllowedDomains;

	/// 是否允许 HTTP 连接（不推荐，仅用于开发测试）
	UPROPERTY(Config, EditAnywhere, Category = "Server")
	bool bAllowHttpConnection;
};