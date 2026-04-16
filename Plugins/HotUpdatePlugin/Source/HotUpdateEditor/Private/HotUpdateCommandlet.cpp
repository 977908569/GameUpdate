// Copyright czm. All Rights Reserved.

#include "HotUpdateCommandlet.h"
#include "HotUpdateEditor.h"
#include "HotUpdateUtils.h"
#include "HotUpdateBaseVersionBuilder.h"
#include "HotUpdateVersionManager.h"
#include "HotUpdatePatchPackageBuilder.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogHotUpdateCommandlet, Log, All);

UHotUpdateCommandlet::UHotUpdateCommandlet(): bShowHelp(false), bIsShipping(false), bSkipBuild(false),
                                              bEnableMinimalPackage(false),
                                              bIncludeBaseContainers(false),
                                              bSkipCook(false)
{
	// 命令行工具不需要渲染
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	HelpDescription = TEXT("热更新打包命令行工具");
	HelpUsage = TEXT(
		"UnrealEditor-Cmd MyProject -run=HotUpdate -mode=base -version=1.0.0 -platform=Windows -output=D:/Output");
	HelpWebLink = TEXT("");
}

int32 UHotUpdateCommandlet::Main(const FString& Params)
{
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("热更新打包命令行工具启动"));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("参数: %s"), *Params);

	// 解析命令行参数
	if (!ParseCommandLine(Params))
	{
		return 1;
	}

	// 显示帮助
	if (bShowHelp)
	{
		ShowHelp();
		return 0;
	}

	// 验证必要参数
	if (Version.IsEmpty())
	{
		UE_LOG(LogHotUpdateCommandlet, Error, TEXT("缺少版本号参数 -version"));
		return 1;
	}

	if (Mode.IsEmpty())
	{
		UE_LOG(LogHotUpdateCommandlet, Error, TEXT("缺少模式参数 -mode (base 或 patch)"));
		return 1;
	}

	// 确保 AssetRegistry 加载完成
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry* AssetRegistry = &AssetRegistryModule.Get();
	AssetRegistry->SearchAllAssets(true);

	// 等待 AssetRegistry 搜索完成
	while (AssetRegistry->IsLoadingAssets())
	{
		FPlatformProcess::Sleep(0.1f);
	}

	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("AssetRegistry 加载完成"));

	int32 Result = 0;

	if (Mode == TEXT("base"))
	{
		Result = ExecuteBasePackage();
	}
	else if (Mode == TEXT("patch"))
	{
		Result = ExecutePatchPackage();
	}
	else
	{
		UE_LOG(LogHotUpdateCommandlet, Error, TEXT("未知的模式: %s (支持: base, patch)"), *Mode);
		Result = 1;
	}

	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("热更新打包完成，返回码: %d"), Result);

	return Result;
}

bool UHotUpdateCommandlet::ParseCommandLine(const FString& Params)
{
	// 解析模式
	FParse::Value(*Params, TEXT("mode="), Mode);

	// 解析版本号
	FParse::Value(*Params, TEXT("version="), Version);

	// 解析基础版本号
	FParse::Value(*Params, TEXT("baseversion="), BaseVersion);

	// 解析平台
	FParse::Value(*Params, TEXT("platform="), PlatformStr);

	// 解析输出目录
	FParse::Value(*Params, TEXT("output="), OutputDir);

	// 解析 Manifest 路径
	FParse::Value(*Params, TEXT("manifest="), ManifestPath);

	// 解析基础版本构建参数
	bIsShipping = FParse::Param(*Params, TEXT("shipping"));
	bSkipBuild = FParse::Param(*Params, TEXT("skipbuild"));

	// 解析最小包配置参数
	bEnableMinimalPackage = FParse::Param(*Params, TEXT("minimal"));
	FParse::Value(*Params, TEXT("whitelist="), WhitelistDirectories);

	// 解析全量热更新参数
	bIncludeBaseContainers = FParse::Param(*Params, TEXT("includebasecontainers"));
	FParse::Value(*Params, TEXT("basecontainerdir="), BaseContainerDir);

	// 解析 Android 纹理格式参数
	FParse::Value(*Params, TEXT("textureformat="), TextureFormatStr);

	// 解析跳过 Cook 参数
	bSkipCook = FParse::Param(*Params, TEXT("skipcook"));

	// 解析帮助标志
	bShowHelp = FParse::Param(*Params, TEXT("help"));

	// 日志输出参数
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("解析参数结果:"));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  Mode: %s"), *Mode);
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  Version: %s"), *Version);
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  BaseVersion: %s"), *BaseVersion);
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  Platform: %s"), *PlatformStr);
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  OutputDir: %s"), *OutputDir);
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  ManifestPath: %s"), *ManifestPath);
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  IsShipping: %s"), bIsShipping ? TEXT("true") : TEXT("false"));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  SkipBuild: %s"), bSkipBuild ? TEXT("true") : TEXT("false"));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  EnableMinimalPackage: %s"), bEnableMinimalPackage ? TEXT("true") : TEXT("false"));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  WhitelistDirectories: %s"), *WhitelistDirectories);
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  IncludeBaseContainers: %s"), bIncludeBaseContainers ? TEXT("true") : TEXT("false"));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  BaseContainerDir: %s"), *BaseContainerDir);
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  TextureFormat: %s"), *TextureFormatStr);
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  SkipCook: %s"), bSkipCook ? TEXT("true") : TEXT("false"));

	return true;
}

void UHotUpdateCommandlet::ShowHelp()
{
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT(""));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("热更新打包命令行工具"));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT(""));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("使用方式:"));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  UnrealEditor-Cmd MyProject -run=HotUpdate -mode=base -version=1.0.0 -platform=Windows -output=D:/Output"));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  UnrealEditor-Cmd MyProject -run=HotUpdate -mode=patch -version=1.0.1 -baseversion=1.0.0 -manifest=D:/Base/manifest.json -platform=Windows -output=D:/Output"));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT(""));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("参数说明:"));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  -mode                 打包模式: base(完整exe/apk基础包), patch(热更包)"));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  -version              版本号 (如 1.0.0)"));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  -baseversion          基础版本号 (热更包需要)"));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  -platform             目标平台: Windows, Android, IOS"));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  -output               输出目录路径"));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  -manifest             基础版本Manifest文件路径 (热更包需要)"));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  -shipping             是否为发布版本构建 (base 模式)"));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  -skipbuild            是否跳过编译步骤 (base/patch 模式，默认会先编译)"));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  -minimal              启用最小包模式 (base 模式)"));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  -whitelist            必须包含的目录，分号分隔 (如 /Game/UI;/Game/Maps)"));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  -includebasecontainers 是否包含基础版本容器（全量热更新模式）"));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  -basecontainerdir     基础版本容器目录路径（全量热更新模式需要）"));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  -textureformat        Android 纹理格式: ETC2, ASTC, DXT, Multi (base 模式, 默认 ETC2)"));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  -skipcook             跳过 Cook 步骤 (patch 模式，默认会先 Cook)"));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  -help                 显示帮助信息"));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT(""));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("示例:"));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  # 打包完整基础包（含exe）"));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  UnrealEditor-Cmd MyProject -run=HotUpdate -mode=base -version=1.0.0 -platform=Windows"));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT(""));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  # 打包发布版本基础包"));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  UnrealEditor-Cmd MyProject -run=HotUpdate -mode=base -version=1.0.0 -platform=Windows -shipping"));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT(""));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  # 打包热更包"));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  UnrealEditor-Cmd MyProject -run=HotUpdate -mode=patch -version=1.0.1 -baseversion=1.0.0 -platform=Windows"));
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT(""));
}

int32 UHotUpdateCommandlet::ExecuteBasePackage()
{
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("开始构建完整基础版本（exe/apk）..."));

	// 构建配置
	FHotUpdateBaseVersionBuildConfig Config;
	Config.VersionString = Version;
	Config.Platform = ParsePlatform(PlatformStr);
	Config.BuildConfiguration = bIsShipping ? EHotUpdateBuildConfiguration::Shipping : EHotUpdateBuildConfiguration::Development;
	Config.bSkipBuild = bSkipBuild;
	Config.bSynchronousMode = true;  // 命令行模式使用同步执行

	// 配置 Android 纹理格式
	if (!TextureFormatStr.IsEmpty() && Config.Platform == EHotUpdatePlatform::Android)
	{
		Config.AndroidTextureFormat = ParseTextureFormat(TextureFormatStr);
		UE_LOG(LogHotUpdateCommandlet, Log, TEXT("使用 Android 纹理格式: %s"), *TextureFormatStr);
	}

	// 配置输出目录
	if (!OutputDir.IsEmpty())
	{
		Config.OutputDirectory = OutputDir;
	}
	else
	{
		// 默认输出目录
		Config.OutputDirectory = UHotUpdateBaseVersionBuilder::GetDefaultOutputDirectory();
	}

	// 配置最小包模式
	if (bEnableMinimalPackage)
	{
		Config.MinimalPackageConfig.bEnableMinimalPackage = true;

		// 解析必须包含的目录
		if (!WhitelistDirectories.IsEmpty())
		{
			TArray<FString> Dirs;
			WhitelistDirectories.ParseIntoArray(Dirs, TEXT(";"));
			for (const FString& Dir : Dirs)
			{
				FDirectoryPath DirPath;
				// 规范化路径：将 // 开头转换为 / 开头（Git Bash 路径转换的 workaround）
					FString NormalizedDir = Dir;
					if (NormalizedDir.StartsWith(TEXT("//")))
					{
						NormalizedDir = NormalizedDir.RightChop(1);
					}
					DirPath.Path = NormalizedDir;
				Config.MinimalPackageConfig.WhitelistDirectories.Add(DirPath);
			}
			UE_LOG(LogHotUpdateCommandlet, Log, TEXT("必须包含的目录: %d 个"), Config.MinimalPackageConfig.WhitelistDirectories.Num());
		}
		UE_LOG(LogHotUpdateCommandlet, Log, TEXT("启用最小包模式"));
	}

	// 创建构建器
	UHotUpdateBaseVersionBuilder* Builder = NewObject<UHotUpdateBaseVersionBuilder>();

	// 绑定进度回调
	Builder->OnBuildProgress.AddLambda([](const FHotUpdateBaseVersionBuildProgress& Progress)
	{
		UE_LOG(LogHotUpdateCommandlet, Log, TEXT("[%s] %.0f%% - %s"),
			*Progress.CurrentStage, Progress.ProgressPercent * 100, *Progress.StatusMessage);
	});

	// 捕获构建结果
	bool bBuildSuccess = false;
	Builder->OnBuildComplete.AddLambda([&bBuildSuccess](const FHotUpdateBaseVersionBuildResult& Result)
	{
		bBuildSuccess = Result.bSuccess;
		if (Result.bSuccess)
		{
			UE_LOG(LogHotUpdateCommandlet, Log, TEXT("基础版本构建成功!"));
			UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  输出目录: %s"), *Result.OutputDirectory);
			UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  可执行文件: %s"), *Result.ExecutablePath);
			UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  版本号: %s"), *Result.VersionString);
			UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  平台: %s"), *HotUpdateUtils::GetPlatformDirectoryName(Result.Platform));
			UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  资源Hash清单: %s"), *Result.ResourceHashPath);
		}
		else
		{
			UE_LOG(LogHotUpdateCommandlet, Error, TEXT("基础版本构建失败: %s"), *Result.ErrorMessage);
		}
	});

	// 开始构建
	Builder->BuildBaseVersion(Config);

	// 等待构建完成
	while (Builder->IsBuilding())
	{
		FPlatformProcess::Sleep(0.5f);
	}

	return bBuildSuccess ? 0 : 1;
}

int32 UHotUpdateCommandlet::ExecutePatchPackage()
{
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("开始构建热更包..."));

	// 验证必要参数
	if (BaseVersion.IsEmpty())
	{
		UE_LOG(LogHotUpdateCommandlet, Error, TEXT("热更包需要指定基础版本号 -baseversion"));
		return 1;
	}

	// 尝试查找基础版本 Manifest（优先从版本管理目录查找）
	FString PlatformName = HotUpdateUtils::GetPlatformString(ParsePlatform(PlatformStr));

	if (ManifestPath.IsEmpty())
	{
		// 优先从 HotUpdateVersions 目录查找
		FString VersionDir = UHotUpdateVersionManager::GetVersionDir(BaseVersion, ParsePlatform(PlatformStr));
		ManifestPath = FPaths::Combine(VersionDir, TEXT("manifest.json"));

		if (!FPaths::FileExists(*ManifestPath))
		{
			// 回退到 BaseVersionBuilds 目录查找
			FString BaseVersionDir = FPaths::Combine(UHotUpdateBaseVersionBuilder::GetDefaultOutputDirectory(), BaseVersion);
			ManifestPath = FPaths::Combine(BaseVersionDir, PlatformName, TEXT("manifest.json"));
		}

		if (!FPaths::FileExists(*ManifestPath))
		{
			UE_LOG(LogHotUpdateCommandlet, Error, TEXT("未找到基础版本 Manifest"));
			UE_LOG(LogHotUpdateCommandlet, Error, TEXT("请使用 -manifest 参数指定 Manifest 路径"));
			return 1;
		}

		UE_LOG(LogHotUpdateCommandlet, Log, TEXT("自动找到 Manifest: %s"), *ManifestPath);
	}

	// 构建配置
	FHotUpdatePatchPackageConfig Config;
	Config.PatchVersion = Version;
	Config.BaseVersion = BaseVersion;
	Config.Platform = ParsePlatform(PlatformStr);
	Config.BaseManifestPath.FilePath = ManifestPath;
	Config.bSkipCook = bSkipCook;
	Config.bSkipBuild = bSkipBuild;

	// 配置输出目录
	if (!OutputDir.IsEmpty())
	{
		Config.OutputDirectory.Path = OutputDir;
	}
	else
	{
		Config.OutputDirectory.Path = FPaths::ProjectSavedDir() / TEXT("HotUpdatePatches");
	}

	// 配置全量热更新模式
	if (bIncludeBaseContainers)
	{
		Config.bIncludeBaseContainers = true;

		// 如果未指定基础容器目录，尝试自动查找
		if (BaseContainerDir.IsEmpty())
		{
			FString BaseVersionBuildDir = FPaths::Combine(UHotUpdateBaseVersionBuilder::GetDefaultOutputDirectory(), BaseVersion, PlatformName);
			if (FPaths::DirectoryExists(*BaseVersionBuildDir))
			{
				BaseContainerDir = BaseVersionBuildDir;
				UE_LOG(LogHotUpdateCommandlet, Log, TEXT("自动找到基础容器目录: %s"), *BaseContainerDir);
			}
		}

		if (!BaseContainerDir.IsEmpty())
		{
			Config.BaseContainerDirectory.Path = BaseContainerDir;
			UE_LOG(LogHotUpdateCommandlet, Log, TEXT("启用全量热更新模式，基础容器目录: %s"), *BaseContainerDir);
		}
	}

	// 创建构建器
	UHotUpdatePatchPackageBuilder* Builder = NewObject<UHotUpdatePatchPackageBuilder>();

	// 验证配置
	FString ErrorMessage;
	if (!Builder->ValidateConfig(Config, ErrorMessage))
	{
		UE_LOG(LogHotUpdateCommandlet, Error, TEXT("配置验证失败: %s"), *ErrorMessage);
		return 1;
	}

	// 执行构建
	UE_LOG(LogHotUpdateCommandlet, Log, TEXT("开始构建热更包..."));
	FHotUpdatePatchPackageResult Result = Builder->BuildPatchPackage(Config);

	if (Result.bSuccess)
	{
		UE_LOG(LogHotUpdateCommandlet, Log, TEXT("热更包构建成功!"));
		UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  输出目录: %s"), *Result.OutputDirectory);
		UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  版本号: %s"), *Result.PatchVersion);
		UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  基础版本: %s"), *Result.BaseVersion);
		UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  变更资源数: %d"), Result.ChangedAssetCount);
		UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  包大小: %.2f MB"), Result.PatchSize / (1024.0 * 1024.0));
		UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  Manifest: %s"), *Result.ManifestFilePath);

		// 全量热更新信息
		if (Result.bIncludesBaseContainers)
		{
			UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  全量热更新: 是"));
			UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  基础容器数量: %d"), Result.BaseContainers.Num());
			UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  总下载大小: %.2f MB"), Result.TotalDownloadSize / (1024.0 * 1024.0));
		}

		UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  差异报告:"));
		UE_LOG(LogHotUpdateCommandlet, Log, TEXT("    新增: %d"), Result.DiffReport.AddedAssets.Num());
		UE_LOG(LogHotUpdateCommandlet, Log, TEXT("    修改: %d"), Result.DiffReport.ModifiedAssets.Num());
		UE_LOG(LogHotUpdateCommandlet, Log, TEXT("    删除: %d"), Result.DiffReport.DeletedAssets.Num());
		UE_LOG(LogHotUpdateCommandlet, Log, TEXT("    未变更: %d"), Result.DiffReport.UnchangedAssets.Num());

		if (!Result.PatchUtocPath.IsEmpty())
		{
			UE_LOG(LogHotUpdateCommandlet, Log, TEXT("  补丁容器: %s"), *Result.PatchUtocPath);
		}

		return 0;
	}
	else
	{
		UE_LOG(LogHotUpdateCommandlet, Error, TEXT("热更包构建失败: %s"), *Result.ErrorMessage);
		return 1;
	}
}

EHotUpdatePlatform UHotUpdateCommandlet::ParsePlatform(const FString& InPlatformStr)
{
	FString LowerPlatform = InPlatformStr.ToLower();

	if (LowerPlatform == TEXT("windows") || LowerPlatform == TEXT("win64") || LowerPlatform == TEXT("pc"))
	{
		return EHotUpdatePlatform::Windows;
	}
	else if (LowerPlatform == TEXT("android"))
	{
		return EHotUpdatePlatform::Android;
	}
	else if (LowerPlatform == TEXT("ios") || LowerPlatform == TEXT("iphone"))
	{
		return EHotUpdatePlatform::IOS;
	}

	UE_LOG(LogHotUpdateCommandlet, Warning, TEXT("未知的平台: %s, 使用默认平台 Windows"), *InPlatformStr);
	return EHotUpdatePlatform::Windows;
}

EHotUpdateChunkStrategy UHotUpdateCommandlet::ParseChunkStrategy(const FString& InStrategyStr)
{
	FString LowerStrategy = InStrategyStr.ToLower();

	if (LowerStrategy == TEXT("none"))
	{
		return EHotUpdateChunkStrategy::None;
	}
	else if (LowerStrategy == TEXT("size"))
	{
		return EHotUpdateChunkStrategy::Size;
	}
	else if (LowerStrategy == TEXT("directory"))
	{
		return EHotUpdateChunkStrategy::Directory;
	}
	else if (LowerStrategy == TEXT("assettype"))
	{
		return EHotUpdateChunkStrategy::AssetType;
	}
	else if (LowerStrategy == TEXT("primaryasset"))
	{
		return EHotUpdateChunkStrategy::PrimaryAsset;
	}
	else if (LowerStrategy == TEXT("hybrid"))
	{
		return EHotUpdateChunkStrategy::Hybrid;
	}

	UE_LOG(LogHotUpdateCommandlet, Warning, TEXT("未知的分包策略: %s, 使用默认策略 PrimaryAsset"), *InStrategyStr);
	return EHotUpdateChunkStrategy::PrimaryAsset;
}

FString UHotUpdateCommandlet::ParseCompressionFormat(const FString& CompressionStr)
{
	FString LowerCompression = CompressionStr.ToLower();

	if (LowerCompression == TEXT("oodle") || LowerCompression == TEXT("kraken"))
	{
		return TEXT("Oodle");
	}
	else if (LowerCompression == TEXT("zlib"))
	{
		return TEXT("Zlib");
	}
	else if (LowerCompression == TEXT("gzip") || LowerCompression == TEXT("gz"))
	{
		return TEXT("GZip");
	}
	else if (LowerCompression == TEXT("none") || LowerCompression == TEXT("no"))
	{
		return TEXT("None");
	}

	UE_LOG(LogHotUpdateCommandlet, Warning, TEXT("未知的压缩格式: %s, 使用默认格式 Oodle"), *CompressionStr);
	return TEXT("Oodle");
}

TArray<FString> UHotUpdateCommandlet::FindIntermediatePatchVersions(const FString& InOutputDir, const FString& InBaseVersion, const FString& InTargetVersion)
{
	TArray<FString> IntermediateVersions;

	// 使用 FHotUpdateVersionInfo 进行版本比较
	FHotUpdateVersionInfo BaseVer = FHotUpdateVersionInfo::FromString(InBaseVersion);
	FHotUpdateVersionInfo TargetVer = FHotUpdateVersionInfo::FromString(InTargetVersion);

	// 获取输出目录中的所有版本目录
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	TArray<FString> FoundVersions;

	if (PlatformFile.DirectoryExists(*InOutputDir))
	{
		TArray<FString> SubDirs;
		PlatformFile.IterateDirectory(*InOutputDir, [&SubDirs](const TCHAR* Name, bool bIsDirectory)
		{
			if (bIsDirectory)
			{
				FString DirName = FPaths::GetCleanFilename(Name);
				// 检查是否是版本号格式 (x.x.x)
				if (DirName.Len() >= 5 && DirName.Contains(TEXT(".")))
				{
					SubDirs.Add(DirName);
				}
			}
			return true;
		});

		// 按版本号排序
		SubDirs.Sort([](const FString& A, const FString& B)
		{
			return FHotUpdateVersionInfo::FromString(A) < FHotUpdateVersionInfo::FromString(B);
		});

		// 篮选在 baseVersion 和 targetVersion 之间的版本
		for (const FString& DirVersion : SubDirs)
		{
			// 检查是否大于 baseVersion 且小于 targetVersion
			FHotUpdateVersionInfo DirVer = FHotUpdateVersionInfo::FromString(DirVersion);
			if (BaseVer < DirVer && DirVer < TargetVer)
			{
				// 检查该版本是否有 Manifest 文件
				FString PlatformName = HotUpdateUtils::GetPlatformString(ParsePlatform(PlatformStr));
				FString DirManifestPath = FPaths::Combine(InOutputDir, DirVersion, PlatformName, TEXT("manifest.json"));
				if (PlatformFile.FileExists(*DirManifestPath))
				{
					IntermediateVersions.Add(DirVersion);
				}
			}
		}
	}

	return IntermediateVersions;
}


EHotUpdateAndroidTextureFormat UHotUpdateCommandlet::ParseTextureFormat(const FString& InFormatStr)
{
	FString LowerFormat = InFormatStr.ToLower();

	if (LowerFormat == TEXT("etc2"))
	{
		return EHotUpdateAndroidTextureFormat::ETC2;
	}
	else if (LowerFormat == TEXT("astc"))
	{
		return EHotUpdateAndroidTextureFormat::ASTC;
	}
	else if (LowerFormat == TEXT("dxt"))
	{
		return EHotUpdateAndroidTextureFormat::DXT;
	}
	else if (LowerFormat == TEXT("multi"))
	{
		return EHotUpdateAndroidTextureFormat::Multi;
	}

	UE_LOG(LogHotUpdateCommandlet, Warning, TEXT("未知的 Android 纹理格式: %s, 使用默认格式 ETC2"), *InFormatStr);
	return EHotUpdateAndroidTextureFormat::ETC2;
}
