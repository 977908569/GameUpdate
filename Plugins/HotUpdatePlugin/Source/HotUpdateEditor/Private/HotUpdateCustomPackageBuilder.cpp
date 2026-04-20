// Copyright czm. All Rights Reserved.

#include "HotUpdateCustomPackageBuilder.h"
#include "HotUpdatePackageHelper.h"
#include "HotUpdateEditor.h"
#include "HotUpdateUtils.h"
#include "Core/HotUpdateFileUtils.h"
#include "HotUpdateIoStoreBuilder.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"

UHotUpdateCustomPackageBuilder::UHotUpdateCustomPackageBuilder()
	: bIsBuilding(false)
	, bIsCancelled(false)
{
}

TArray<FString> UHotUpdateCustomPackageBuilder::ResolveUassetPathsForCook() const
{
	TArray<FString> AssetPathsToCook;

	UE_LOG(LogHotUpdateEditor, Log, TEXT("ResolveUassetPathsForCook: UassetFilePaths.Num()=%d"), CurrentConfig.UassetFilePaths.Num());

	for (const FString& UassetFilePath : CurrentConfig.UassetFilePaths)
	{
		FString PackageName = ResolveDiskPathToPackageName(UassetFilePath);
		if (!PackageName.IsEmpty())
		{
			AssetPathsToCook.Add(PackageName);
		}
		else
		{
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("ResolveUassetPathsForCook: 无法将 uasset 路径解析为包名: %s"), *UassetFilePath);
		}
	}

	return AssetPathsToCook;
}

FString UHotUpdateCustomPackageBuilder::ResolveDiskPathToPackageName(const FString& DiskPath) const
{
	FString NormalizedPath = DiskPath;
	FPaths::NormalizeFilename(NormalizedPath);

	// 尝试 FPackageName 反向解析
	FString PackageName;
	if (FPackageName::TryConvertFilenameToLongPackageName(NormalizedPath, PackageName))
	{
		return PackageName;
	}

	// 回退：手动约定解析
	FString Normalized = DiskPath;
	FPaths::NormalizeDirectoryName(Normalized);

	// 项目内容：/Content/ 之后的部分
	int32 ContentIdx = INDEX_NONE;
	if ((ContentIdx = Normalized.Find(TEXT("/Content/"), ESearchCase::IgnoreCase)) != INDEX_NONE)
	{
		FString AfterContent = Normalized.Mid(ContentIdx + 9); // 跳过 "/Content/"
		AfterContent.RemoveFromEnd(TEXT(".uasset"));
		AfterContent.RemoveFromEnd(TEXT(".umap"));
		return TEXT("/Game/") + AfterContent;
	}

	// 引擎内容：/Engine/Content/ 之后
	int32 EngineContentIdx = INDEX_NONE;
	if ((EngineContentIdx = Normalized.Find(TEXT("/Engine/Content/"), ESearchCase::IgnoreCase)) != INDEX_NONE)
	{
		FString AfterContent = Normalized.Mid(EngineContentIdx + 16);
		AfterContent.RemoveFromEnd(TEXT(".uasset"));
		AfterContent.RemoveFromEnd(TEXT(".umap"));
		return TEXT("/Engine/") + AfterContent;
	}

	return TEXT("");
}

FString UHotUpdateCustomPackageBuilder::DetermineNonAssetPakPath(const FString& DiskPath) const
{
	// 使用 /Game/ 前缀使 GetPakInternalPath → MapToPakMountPath 能正确映射
	FString ProjectDir = FPaths::ProjectDir();
	FString NormalizedDisk = DiskPath;
	FPaths::NormalizeFilename(NormalizedDisk);
	FString NormalizedProject = ProjectDir;
	FPaths::NormalizeFilename(NormalizedProject);

	if (NormalizedDisk.StartsWith(NormalizedProject))
	{
		FString Relative = NormalizedDisk.Mid(NormalizedProject.Len());
		// 去掉 Content/ 前缀（如果有的话），因为 /Game/ 已经映射到 Content/
		if (Relative.StartsWith(TEXT("Content/")) || Relative.StartsWith(TEXT("Content\\")))
		{
			Relative = Relative.Mid(8); // 跳过 "Content/"
		}
		return TEXT("/Game/") + Relative;
	}

	// 项目外部文件：放在 /Game/ExternalFiles/ 下
	FString Filename = FPaths::GetCleanFilename(DiskPath);
	return FString::Printf(TEXT("/Game/ExternalFiles/%s"), *Filename);
}

FHotUpdateCustomPackageResult UHotUpdateCustomPackageBuilder::ExecuteBuild(const FHotUpdateCustomPackageConfig& Config, const TArray<FString>& AssetPathsToCook)
{
	UE_LOG(LogHotUpdateEditor, Log, TEXT("ExecuteBuild 开始 (后台线程)"));

	FHotUpdateCustomPackageResult Result;
	bIsCancelled = false;

	int32 TotalAssetCount = Config.UassetFilePaths.Num() + Config.NonAssetFilePaths.Num();
	UE_LOG(LogHotUpdateEditor, Log, TEXT("自定义打包: uasset %d 个, 非资产 %d 个"), Config.UassetFilePaths.Num(), Config.NonAssetFilePaths.Num());

	if (TotalAssetCount == 0)
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("没有找到可打包的资源");
		bIsBuilding = false;
		return Result;
	}

	// 编译项目
	if (!Config.bSkipBuild)
	{
		UpdateProgress(TEXT("编译项目"), TEXT(""), 0, 0);
		if (!FHotUpdatePackageHelper::CompileProject(Config.Platform))
		{
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("项目编译失败");
			bIsBuilding = false;
			return Result;
		}
	}
	else
	{
		UE_LOG(LogHotUpdateEditor, Log, TEXT("跳过编译步骤 (bSkipBuild = true)"));
	}

	// 只 Cook uasset 资源
	if (!Config.bSkipCook && AssetPathsToCook.Num() > 0)
	{
		UpdateProgress(TEXT("增量 Cook 资源"), TEXT(""), 0, AssetPathsToCook.Num());
		if (!FHotUpdatePackageHelper::CookAssets(Config.Platform, AssetPathsToCook))
		{
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("Cook 资源失败");
			bIsBuilding = false;
			return Result;
		}
	}

	// 构建合并的 AssetDiskPaths 映射
	TArray<FString> ValidAssetPaths;
	TMap<FString, FString> AssetDiskPaths;
	TMap<FString, FString> AssetSourcePaths;

	// uasset 文件：磁盘路径已经可用
	FString CookedPlatformDir = HotUpdateUtils::GetCookedPlatformDir(Config.Platform);
	for (const FString& UassetPath : Config.UassetFilePaths)
	{
		// 尝试使用 Cook 后路径
		FString PackageName = ResolveDiskPathToPackageName(UassetPath);
		FString DiskPath;

		if (!PackageName.IsEmpty())
		{
			DiskPath = FHotUpdatePackageHelper::GetAssetDiskPath(PackageName, CookedPlatformDir);
		}

		// 如果 Cook 后路径不存在，尝试使用用户提供的路径
		if (DiskPath.IsEmpty() || !FPaths::FileExists(*DiskPath))
		{
			if (FPaths::FileExists(*UassetPath))
			{
				DiskPath = UassetPath;
			}
		}

		if (!DiskPath.IsEmpty() && FPaths::FileExists(*DiskPath))
		{
			FString Key = !PackageName.IsEmpty() ? PackageName : UassetPath;
			ValidAssetPaths.Add(Key);
			AssetDiskPaths.Add(Key, DiskPath);

			FString SourcePath = FHotUpdatePackageHelper::GetAssetSourcePath(Key);
			if (!SourcePath.IsEmpty())
			{
				AssetSourcePaths.Add(Key, SourcePath);
			}
		}
		else
		{
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("自定义打包: 跳过无 cooked 文件的 uasset: %s"), *UassetPath);
		}
	}

	// non-asset 文件：直接使用原始磁盘路径
	for (const FString& NonAssetPath : Config.NonAssetFilePaths)
	{
		if (FPaths::FileExists(*NonAssetPath))
		{
			FString PakPath = DetermineNonAssetPakPath(NonAssetPath);
			ValidAssetPaths.Add(PakPath);
			AssetDiskPaths.Add(PakPath, NonAssetPath);
		}
		else
		{
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("自定义打包: 跳过不存在的非资产文件: %s"), *NonAssetPath);
		}
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("自定义打包: 有效资源 %d 个"), ValidAssetPaths.Num());

	if (ValidAssetPaths.Num() == 0)
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("没有找到可打包的资源（Cook 后无有效文件）");
		bIsBuilding = false;
		return Result;
	}

	// 计算资源 Hash
	UpdateProgress(TEXT("计算资源 Hash"), TEXT(""), 0, ValidAssetPaths.Num());

	TMap<FString, FString> AssetHashes;
	TMap<FString, int64> AssetSizes;

	for (int32 i = 0; i < ValidAssetPaths.Num(); i++)
	{
		if (bIsCancelled)
		{
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("构建已取消");
			bIsBuilding = false;
			return Result;
		}

		const FString& AssetPath = ValidAssetPaths[i];
		const FString* DiskPath = AssetDiskPaths.Find(AssetPath);
		const FString* SourcePath = AssetSourcePaths.Find(AssetPath);

		FString HashPath = (SourcePath && FPaths::FileExists(**SourcePath)) ? *SourcePath
			: (DiskPath && FPaths::FileExists(**DiskPath)) ? *DiskPath : TEXT("");

		if (!HashPath.IsEmpty())
		{
			AssetHashes.Add(AssetPath, UHotUpdateFileUtils::CalculateFileHash(HashPath));
			AssetSizes.Add(AssetPath, IFileManager::Get().FileSize(*HashPath));
		}
		UpdateProgress(TEXT("计算资源 Hash"), AssetPath, i + 1, ValidAssetPaths.Num());
	}

	// 确定输出目录
	FString OutputDir = Config.OutputDirectory.Path;
	if (OutputDir.IsEmpty())
	{
		OutputDir = FPaths::ProjectSavedDir() / TEXT("HotUpdateCustomPackages");
	}

	FString PlatformStr = HotUpdateUtils::GetPlatformString(Config.Platform);
	OutputDir = FPaths::Combine(OutputDir, Config.PatchVersion, PlatformStr);
	FPaths::NormalizeDirectoryName(OutputDir);

	IPlatformFile::GetPlatformPhysical().CreateDirectoryTree(*OutputDir);

	Result.OutputDirectory = OutputDir;
	Result.PatchVersion = Config.PatchVersion;
	Result.AssetCount = ValidAssetPaths.Num();

	// 创建 IoStore 容器
	UpdateProgress(TEXT("创建打包容器"), TEXT(""), 0, ValidAssetPaths.Num());

	if (AssetDiskPaths.Num() > 0)
	{
		UHotUpdateIoStoreBuilder* IoStoreBuilder = NewObject<UHotUpdateIoStoreBuilder>();

		FHotUpdateIoStoreConfig IoStoreConfig = Config.IoStoreConfig;
		IoStoreConfig.bUseIoStore = false;
		FString PrioritySuffix = FString::Printf(TEXT("_%d_P"), Config.PakPriority);
		IoStoreConfig.ContainerName = FString::Printf(TEXT("%s%s"), *Config.PatchVersion, *PrioritySuffix);

		FString PaksDir = FPaths::Combine(OutputDir, TEXT("Paks"));
		IPlatformFile::GetPlatformPhysical().CreateDirectoryTree(*PaksDir);

		FString PatchOutputPath = FPaths::Combine(PaksDir, IoStoreConfig.ContainerName);

		FHotUpdateIoStoreResult IoStoreResult = IoStoreBuilder->BuildIoStoreContainer(
			AssetDiskPaths, PatchOutputPath, IoStoreConfig);

		if (!IoStoreResult.bSuccess)
		{
			Result.bSuccess = false;
			Result.ErrorMessage = FString::Printf(TEXT("容器创建失败: %s"), *IoStoreResult.ErrorMessage);
			bIsBuilding = false;
			return Result;
		}

		Result.PatchUtocPath = IoStoreResult.UtocPath;
		Result.PatchSize = IoStoreResult.ContainerSize;

		UE_LOG(LogHotUpdateEditor, Log, TEXT("自定义打包容器创建成功: %s"), *IoStoreResult.UtocPath);
	}
	else
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("没有找到可打包的资源");
		bIsBuilding = false;
		return Result;
	}

	Result.bSuccess = true;
	bIsBuilding = false;

	UpdateProgress(TEXT("完成"), TEXT(""), 1, 1);

	UE_LOG(LogHotUpdateEditor, Log, TEXT("自定义打包构建成功: %s, %d 个资源, 大小 %lld 字节"),
		*OutputDir, ValidAssetPaths.Num(), Result.PatchSize);

	return Result;
}

FHotUpdateCustomPackageResult UHotUpdateCustomPackageBuilder::BuildCustomPackage(const FHotUpdateCustomPackageConfig& Config)
{
	UE_LOG(LogHotUpdateEditor, Log, TEXT("BuildCustomPackage (同步) 开始调用"));

	CurrentConfig = Config;

	TArray<FString> AssetPathsToCook = ResolveUassetPathsForCook();

	return ExecuteBuild(Config, AssetPathsToCook);
}

void UHotUpdateCustomPackageBuilder::BuildCustomPackageAsync(const FHotUpdateCustomPackageConfig& Config)
{
	UE_LOG(LogHotUpdateEditor, Log, TEXT("BuildCustomPackageAsync 开始调用"));

	if (bIsBuilding)
	{
		if (BuildTask.IsValid() && !BuildTask.IsReady())
		{
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("已有自定义打包构建任务正在运行，拒绝新的构建请求"));
			FHotUpdateCustomPackageResult Result;
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("已有构建任务正在进行中");
			OnComplete.Broadcast(Result);
			return;
		}
		else
		{
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("检测到之前的构建异常终止，正在重置构建状态"));
			bIsBuilding = false;
			bIsCancelled = false;
		}
	}

	bIsBuilding = true;
	bIsCancelled = false;
	CurrentConfig = Config;

	UE_LOG(LogHotUpdateEditor, Log, TEXT("CurrentConfig.UassetFilePaths 数量: %d, NonAssetFilePaths 数量: %d"),
		CurrentConfig.UassetFilePaths.Num(), CurrentConfig.NonAssetFilePaths.Num());

	// 在 GameThread 将 uasset 磁盘路径反向解析为 UE 包名（供 Cook 使用）
	TArray<FString> AssetPathsToCook = ResolveUassetPathsForCook();

	UE_LOG(LogHotUpdateEditor, Log, TEXT("GameThread 解析到 %d 个 Cook 路径，启动后台构建"), AssetPathsToCook.Num());

	if (AssetPathsToCook.Num() == 0 && CurrentConfig.NonAssetFilePaths.Num() == 0)
	{
		FHotUpdateCustomPackageResult Result;
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("没有找到可打包的资源");
		bIsBuilding = false;
		OnComplete.Broadcast(Result);
		return;
	}

	TWeakObjectPtr<UHotUpdateCustomPackageBuilder> WeakThis(this);

	BuildTask = Async(EAsyncExecution::Thread, [WeakThis, AssetPathsToCook]()
	{
		UHotUpdateCustomPackageBuilder* Builder = WeakThis.Get();
		if (!Builder)
		{
			return;
		}

		struct FBuildGuard
		{
			UHotUpdateCustomPackageBuilder* Builder;
			FHotUpdateCustomPackageResult Result;
			bool bNormalCompletion = false;
			FBuildGuard(UHotUpdateCustomPackageBuilder* InBuilder) : Builder(InBuilder) {}
			~FBuildGuard()
			{
				if (Builder && Builder->bIsBuilding && !bNormalCompletion)
				{
					Builder->bIsBuilding = false;
					UE_LOG(LogHotUpdateEditor, Warning, TEXT("自定义打包构建异常终止，已重置构建状态"));

					UHotUpdateCustomPackageBuilder* GuardBuilder = Builder;
					FHotUpdateCustomPackageResult ResultCopy = Result;
					AsyncTask(ENamedThreads::GameThread, [GuardBuilder, ResultCopy]()
					{
						if (IsValid(GuardBuilder))
						{
							GuardBuilder->OnComplete.Broadcast(ResultCopy);
						}
					});
				}
			}
		};
		FBuildGuard Guard(Builder);

		FHotUpdateCustomPackageResult Result = Builder->ExecuteBuild(Builder->CurrentConfig, AssetPathsToCook);
		Guard.Result = Result;
		Guard.bNormalCompletion = true;

		AsyncTask(ENamedThreads::GameThread, [WeakThis,
			bSuccess = Result.bSuccess,
			ErrorMessage = Result.ErrorMessage,
			OutputDirectory = Result.OutputDirectory,
			PatchVersion = Result.PatchVersion,
			PatchUtocPath = Result.PatchUtocPath,
			PatchSize = Result.PatchSize,
			AssetCount = Result.AssetCount]()
		{
			UHotUpdateCustomPackageBuilder* PinnedBuilder = WeakThis.Get();
			if (IsValid(PinnedBuilder))
			{
				FHotUpdateCustomPackageResult GameThreadResult;
				GameThreadResult.bSuccess = bSuccess;
				GameThreadResult.ErrorMessage = ErrorMessage;
				GameThreadResult.OutputDirectory = OutputDirectory;
				GameThreadResult.PatchVersion = PatchVersion;
				GameThreadResult.PatchUtocPath = PatchUtocPath;
				GameThreadResult.PatchSize = PatchSize;
				GameThreadResult.AssetCount = AssetCount;
				PinnedBuilder->OnComplete.Broadcast(GameThreadResult);
			}
		});
	});
}

void UHotUpdateCustomPackageBuilder::CancelBuild()
{
	bIsCancelled = true;
}

FHotUpdatePackageProgress UHotUpdateCustomPackageBuilder::GetCurrentProgress() const
{
	FScopeLock Lock(&ProgressCriticalSection);
	return CurrentProgress;
}

void UHotUpdateCustomPackageBuilder::UpdateProgress(const FString& Stage, const FString& CurrentFile, int32 ProcessedFiles, int32 TotalFiles)
{
	FHotUpdatePackageProgress ProgressCopy;
	{
		FScopeLock Lock(&ProgressCriticalSection);
		CurrentProgress.CurrentStage= Stage;
		CurrentProgress.CurrentFile = CurrentFile;
		CurrentProgress.ProcessedFiles = ProcessedFiles;
		CurrentProgress.TotalFiles = TotalFiles;
		CurrentProgress.bIsComplete = (ProcessedFiles >= TotalFiles && TotalFiles > 0);
		ProgressCopy = CurrentProgress;
	}

	TWeakObjectPtr<UHotUpdateCustomPackageBuilder> WeakThis(this);
	AsyncTask(ENamedThreads::GameThread, [WeakThis, ProgressCopy]()
	{
		UHotUpdateCustomPackageBuilder* PinnedBuilder = WeakThis.Get();
		if (IsValid(PinnedBuilder))
		{
			PinnedBuilder->OnProgress.Broadcast(ProgressCopy);
		}
	});
}