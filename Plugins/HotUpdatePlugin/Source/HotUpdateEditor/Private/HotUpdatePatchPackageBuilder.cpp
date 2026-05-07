// Copyright czm. All Rights Reserved.

#include "HotUpdatePatchPackageBuilder.h"
#include "HotUpdatePackageHelper.h"
#include "HotUpdateAssetFilter.h"
#include "HotUpdateUtils.h"
#include "Core/HotUpdateFileUtils.h"
#include "HotUpdateEditor.h"
#include "HotUpdateIoStoreBuilder.h"
#include "HotUpdatePackagingSettingsHelper.h"
#include "HotUpdateVersionManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FHotUpdatePatchPackageBuilder::FHotUpdatePatchPackageBuilder()
	: bIsBuilding(false)
	, bIsCancelled(false)
{
}

FHotUpdatePatchPackageResult FHotUpdatePatchPackageBuilder::BuildPatchPackage(const FHotUpdatePatchPackageConfig& Config)
{
	UE_LOG(LogHotUpdateEditor, Log, TEXT("BuildPatchPackage (同步) 开始调用"));

	CurrentConfig = Config;
	bIsBuilding = true;
	bIsCancelled = false;
	CurrentResult = FHotUpdatePatchPackageResult();

	FPatchBuildContext Ctx;

	// 阶段1: 验证、编译、Cook
	if (!PrepareBuild(Ctx))
	{
		bIsBuilding = false;
		return CurrentResult;
	}

	// 阶段2: 加载基础 Manifest、收集资源、计算差异、增量 Cook
	if (!ComputeChanges(Ctx))
	{
		bIsBuilding = false;
		return CurrentResult;
	}

	// 阶段3: 确定输出目录、创建 Patch 容器
	if (!CreatePatchContainer(Ctx))
	{
		bIsBuilding = false;
		return CurrentResult;
	}

	// 阶段4: 解析基础容器、生成 Manifest、注册版本
	FHotUpdatePatchPackageResult Result;
	if (!BuildAndRegisterManifest(Ctx, Result))
	{
		bIsBuilding = false;
		return CurrentResult;
	}

	bIsBuilding = false;
	UpdateProgress(TEXT("完成"), TEXT(""), 1, 1);

	return Result;
}

void FHotUpdatePatchPackageBuilder::BuildPatchPackageAsync(const FHotUpdatePatchPackageConfig& Config)
{
	UE_LOG(LogHotUpdateEditor, Log, TEXT("BuildPatchPackageAsync 开始调用"));
	UE_LOG(LogHotUpdateEditor, Log, TEXT("  bIsBuilding: %s"), bIsBuilding ? TEXT("true") : TEXT("false"));
	UE_LOG(LogHotUpdateEditor, Log, TEXT("  BuildTask.IsValid(): %s"), BuildTask.IsValid() ? TEXT("true") : TEXT("false"));
	UE_LOG(LogHotUpdateEditor, Log, TEXT("  BuildTask.IsReady(): %s"), BuildTask.IsReady() ? TEXT("true") : TEXT("false"));

	// 检查是否有正在运行的构建任务
	if (bIsBuilding)
	{
		if (BuildTask.IsValid() && !BuildTask.IsReady())
		{
			// 任务仍在运行
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("已有更新包构建任务正在运行，拒绝新的构建请求"));
			FHotUpdatePatchPackageResult Result;
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("已有构建任务正在进行中");
			OnComplete.Broadcast(Result);
			return;
		}
		else
		{
			// 之前的构建异常终止，重置状态
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("检测到之前的构建异常终止，正在重置构建状态"));
			bIsBuilding = false;
			bIsCancelled = false;
		}
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("开始新的更新包构建任务，版本: %s，基础版本: %s"), *CurrentConfig.PatchVersion, *CurrentConfig.BaseVersion);

	bIsBuilding = true;
	bIsCancelled = false;

	CurrentConfig = Config;

	// 始终从打包配置读取资源路径
	UE_LOG(LogHotUpdateEditor, Log, TEXT("在游戏线程预收集打包设置中的资源..."));
	FString ErrorMessage;
	if (!CollectAssets(ErrorMessage))
	{
		bIsBuilding = false;
		UE_LOG(LogHotUpdateEditor, Warning, TEXT("CollectAssets : %s"), *ErrorMessage);
		return;
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("预收集完成（含依赖），共 %d 个资源, %d 个非资源文件"), CurrentConfig.AssetPaths.Num(), CurrentConfig.NonAssetPaths.Num());

	TWeakPtr<FHotUpdatePatchPackageBuilder> WeakBuilder(AsShared());
	BuildTask = Async(EAsyncExecution::Thread, [WeakBuilder](){
		TSharedPtr<FHotUpdatePatchPackageBuilder> Builder = WeakBuilder.Pin();
		if (!Builder.IsValid())
		{
			return;
		}

		FHotUpdatePatchPackageResult Result = Builder->BuildPatchPackage(Builder->CurrentConfig);

		AsyncTask(ENamedThreads::GameThread, [WeakBuilder, Result]()
		{
			TSharedPtr<FHotUpdatePatchPackageBuilder> PinnedBuilder = WeakBuilder.Pin();
			if (PinnedBuilder.IsValid())
			{
				PinnedBuilder->OnComplete.Broadcast(Result);
			}
		});
	});
}

void FHotUpdatePatchPackageBuilder::CancelBuild()
{
	bIsCancelled = true;
}

bool FHotUpdatePatchPackageBuilder::ValidateConfig(const FHotUpdatePatchPackageConfig& Config, FString& OutErrorMessage)
{
	if (Config.PatchVersion.IsEmpty())
	{
		OutErrorMessage = TEXT("更新包版本号不能为空");
		return false;
	}

	// 热更新打包需要基础版本相关配置
	if (Config.BaseVersion.IsEmpty())
	{
		OutErrorMessage = TEXT("基础版本号不能为空");
		return false;
	}

	if (Config.BaseFileManifestPath.FilePath.IsEmpty())
	{
		OutErrorMessage = TEXT("基础版本 Manifest 路径不能为空");
		return false;
	}

	if (!FPaths::FileExists(Config.BaseFileManifestPath.FilePath))
	{
		OutErrorMessage = FString::Printf(TEXT("基础版本 Manifest 文件不存在: %s"), *Config.BaseFileManifestPath.FilePath);
		return false;
	}

	return true;
}

bool FHotUpdatePatchPackageBuilder::CollectAssets( FString& OutErrorMessage)
{
	// 已经收集了
	if (CurrentConfig.AssetPaths.Num() > 0)
	{
		return true;
	}
	FHotUpdatePackagingSettingsResult SettingsResult = FHotUpdatePackagingSettingsHelper::ParsePackagingSettings(true);
	if (SettingsResult.Errors.Num() > 0)
	{
		OutErrorMessage = FString::Join(SettingsResult.Errors, TEXT("\n"));
		return false;
	}
	CurrentConfig.AssetPaths = SettingsResult.AssetPaths;
	CurrentConfig.NonAssetPaths = SettingsResult.NonAssetPaths;
	
	return true;
}

bool FHotUpdatePatchPackageBuilder::LoadBaseFileManifest(
	const FString& ManifestPath,
	FHotUpdateBaseManifestData& OutData,
	FString& OutErrorMessage)
{
	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *ManifestPath))
	{
		OutErrorMessage = FString::Printf(TEXT("无法读取 Manifest 文件: %s"), *ManifestPath);
		return false;
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		OutErrorMessage = FString::Printf(TEXT("无法解析 Manifest JSON: %s"), *ManifestPath);
		return false;
	}

	// 临时存储所有文件
	TMap<FString, FString> AllHashes;
	TMap<FString, int64> AllSizes;

	const TArray<TSharedPtr<FJsonValue>>* FilesArray;
	if (JsonObject->TryGetArrayField(TEXT("files"), FilesArray))
	{
		for (const TSharedPtr<FJsonValue>& FileValue : *FilesArray)
		{
			TSharedPtr<FJsonObject> FileObj = FileValue->AsObject();
			if (!FileObj.IsValid()) continue;

			FString FilePath = FileObj->GetStringField(TEXT("filePath"));
			FString Hash = FileObj->GetStringField(TEXT("fileHash"));
			int64 Size = (int64)FileObj->GetNumberField(TEXT("fileSize"));

			FPaths::NormalizeFilename(FilePath);
			AllHashes.Add(FilePath, Hash);
			AllSizes.Add(FilePath, Size);
		}
	}

	// 按资产类型分类
	SplitHashesByAssetType(AllHashes, AllSizes,
		OutData.AssetHashes, OutData.AssetSizes,
		OutData.NonAssetHashes, OutData.NonAssetSizes);

	UE_LOG(LogHotUpdateEditor, Display, TEXT("LoadBaseFileManifest: 加载 %d 个资产, %d 个非资产文件"),
		OutData.AssetHashes.Num(), OutData.NonAssetHashes.Num());

	return OutData.IsValid();
}

bool FHotUpdatePatchPackageBuilder::ComputeDiff(
	const TArray<FString>& CurrentAssets,
	const TMap<FString, FString>& CurrentHashes,
	const TMap<FString, int64>& CurrentSizes,
	const TMap<FString, FString>& BaseHashes,
	const TMap<FString, int64>& BaseSizes,
	TArray<FString>& OutChangedAssets,
	FHotUpdateDiffReport& OutReport)
{
	UE_LOG(LogHotUpdateEditor, Display, TEXT("ComputeDiff: CurrentAssets.Num=%d, CurrentHashes.Num=%d, BaseHashes.Num=%d"), CurrentAssets.Num(), CurrentHashes.Num(), BaseHashes.Num());

	// 使用公共差异计算函数
	HotUpdateUtils::CalculateAssetDiff(CurrentAssets, CurrentHashes, CurrentSizes, BaseHashes, BaseSizes, OutChangedAssets, OutReport);

	UE_LOG(LogHotUpdateEditor, Display, TEXT("ComputeDiff: Added=%d, Modified=%d, Deleted=%d, Unchanged=%d"), 
		OutReport.AddedAssets.Num(), OutReport.ModifiedAssets.Num(), OutReport.DeletedAssets.Num(), OutReport.UnchangedAssets.Num());

	return true;
}

bool FHotUpdatePatchPackageBuilder::GenerateManifest(
	const FString& ManifestPath,
	const FString& PatchUtocPath,
	const FString& PatchUcasPath,
	const FHotUpdateDiffReport& DiffReport,
	const TArray<FHotUpdateContainerInfo>& BaseContainers) const
{
	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);

	// 包类型
	RootObject->SetNumberField(TEXT("packageKind"), static_cast<int32>(EHotUpdatePackageKind::Patch));

	// 版本信息
	TSharedPtr<FJsonObject> VersionInfo = MakeShareable(new FJsonObject);

	TArray<FString> VersionParts;
	CurrentConfig.PatchVersion.ParseIntoArray(VersionParts, TEXT("."));
	
	VersionInfo->SetStringField(TEXT("version"), CurrentConfig.PatchVersion);
	VersionInfo->SetStringField(TEXT("platform"), HotUpdateUtils::GetPlatformString(CurrentConfig.Platform));
	VersionInfo->SetNumberField(TEXT("timestamp"), FDateTime::Now().ToUnixTimestamp());

	RootObject->SetObjectField(TEXT("version"), VersionInfo);

	// 基础版本
	RootObject->SetStringField(TEXT("baseVersion"), CurrentConfig.BaseVersion);

	// 差异摘要
	TSharedPtr<FJsonObject> DiffSummary = MakeShareable(new FJsonObject);
	DiffSummary->SetNumberField(TEXT("addedCount"), DiffReport.AddedAssets.Num());
	DiffSummary->SetNumberField(TEXT("modifiedCount"), DiffReport.ModifiedAssets.Num());
	DiffSummary->SetNumberField(TEXT("deletedCount"), DiffReport.DeletedAssets.Num());
	DiffSummary->SetNumberField(TEXT("unchangedCount"), DiffReport.UnchangedAssets.Num());
	RootObject->SetObjectField(TEXT("diffSummary"), DiffSummary);
	
	// 容器文件列表
	TArray<TSharedPtr<FJsonValue>> ContainersArray;

	// 1. 先添加基础版本容器（全量热更新模式）
	for (const FHotUpdateContainerInfo& BaseContainer : BaseContainers)
	{
		TSharedPtr<FJsonObject> BaseContainerObj = MakeShareable(new FJsonObject);
		BaseContainerObj->SetStringField(TEXT("containerName"), BaseContainer.ContainerName);

		if (!BaseContainer.UtocFile.Path.IsEmpty())
		{
			// IoStore 格式
			BaseContainerObj->SetStringField(TEXT("utocPath"), BaseContainer.UtocFile.Path);
			BaseContainerObj->SetNumberField(TEXT("utocSize"), BaseContainer.UtocFile.Size);
			BaseContainerObj->SetStringField(TEXT("utocHash"), BaseContainer.UtocFile.Hash);

			if (!BaseContainer.UcasFile.Path.IsEmpty())
			{
				BaseContainerObj->SetStringField(TEXT("ucasPath"), BaseContainer.UcasFile.Path);
				BaseContainerObj->SetNumberField(TEXT("ucasSize"), BaseContainer.UcasFile.Size);
				BaseContainerObj->SetStringField(TEXT("ucasHash"), BaseContainer.UcasFile.Hash);
				BaseContainerObj->SetStringField(TEXT("containerType"), TEXT("patch"));
			}
			else
			{
				BaseContainerObj->SetStringField(TEXT("containerType"), TEXT("patch_embedded"));
			}
		}
		else if (!BaseContainer.PakFile.Path.IsEmpty())
		{
			// 传统 Pak 格式
			BaseContainerObj->SetStringField(TEXT("pakPath"), BaseContainer.PakFile.Path);
			BaseContainerObj->SetNumberField(TEXT("pakSize"), BaseContainer.PakFile.Size);
			BaseContainerObj->SetStringField(TEXT("pakHash"), BaseContainer.PakFile.Hash);
			BaseContainerObj->SetStringField(TEXT("containerType"), TEXT("patch_pak"));
		}

		BaseContainerObj->SetStringField(TEXT("version"), BaseContainer.Version);
		ContainersArray.Add(MakeShareable(new FJsonValueObject(BaseContainerObj)));
	}

	// 添加当前 Patch 容器

	if (!PatchUtocPath.IsEmpty() && FPaths::FileExists(*PatchUtocPath))
	{
		TSharedPtr<FJsonObject> PatchContainerObj = MakeShareable(new FJsonObject);

			FString ContainerName = FString::Printf(TEXT("Patch_%s_P"), *CurrentConfig.PatchVersion);
		PatchContainerObj->SetStringField(TEXT("containerName"), ContainerName);

		if (PatchUtocPath.EndsWith(TEXT(".pak")))
		{
			// 传统 .pak 格式
			FString PakFileName = FPaths::GetCleanFilename(PatchUtocPath);
			PatchContainerObj->SetStringField(TEXT("pakPath"), TEXT("Paks/") + PakFileName);
			PatchContainerObj->SetNumberField(TEXT("pakSize"), IFileManager::Get().FileSize(*PatchUtocPath));
			PatchContainerObj->SetStringField(TEXT("pakHash"), UHotUpdateFileUtils::CalculateFileHash(PatchUtocPath));
			PatchContainerObj->SetStringField(TEXT("containerType"), TEXT("patch_pak"));
		}
		else
		{
			// IoStore 格式
			FString UtocFileName = FPaths::GetCleanFilename(PatchUtocPath);
			PatchContainerObj->SetStringField(TEXT("utocPath"), TEXT("Paks/") + UtocFileName);
			PatchContainerObj->SetNumberField(TEXT("utocSize"), IFileManager::Get().FileSize(*PatchUtocPath));
			PatchContainerObj->SetStringField(TEXT("utocHash"), UHotUpdateFileUtils::CalculateFileHash(PatchUtocPath));

			if (!PatchUcasPath.IsEmpty() && FPaths::FileExists(*PatchUcasPath))
			{
				FString UcasFileName = FPaths::GetCleanFilename(PatchUcasPath);
				PatchContainerObj->SetStringField(TEXT("ucasPath"), TEXT("Paks/") + UcasFileName);
				PatchContainerObj->SetNumberField(TEXT("ucasSize"), IFileManager::Get().FileSize(*PatchUcasPath));
				PatchContainerObj->SetStringField(TEXT("ucasHash"), UHotUpdateFileUtils::CalculateFileHash(PatchUcasPath));
				PatchContainerObj->SetStringField(TEXT("containerType"), TEXT("patch"));
			}
			else
			{
				PatchContainerObj->SetStringField(TEXT("containerType"), TEXT("patch_embedded"));
			}
		}

		PatchContainerObj->SetStringField(TEXT("version"), CurrentConfig.PatchVersion);
		ContainersArray.Add(MakeShareable(new FJsonValueObject(PatchContainerObj)));
	}

	RootObject->SetArrayField(TEXT("containers"), ContainersArray);

	// 注意: 不生成 files 字段，客户端只关心需要下载的容器文件
	// files 信息仅供编辑器端差异计算使用，存储在单独的 fileManifest 文件中

	// 序列化客户端 manifest
	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

	if (!FFileHelper::SaveStringToFile(OutputString, *ManifestPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		return false;
	}

	// 生成编辑器端 fileManifest（包含 files 信息，用于差异计算）
	FString FileManifestPath = FPaths::Combine(FPaths::GetPath(ManifestPath), TEXT("filemanifest.json"));

	TSharedPtr<FJsonObject> FileManifestObj = MakeShareable(new FJsonObject);
	FileManifestObj->SetNumberField(TEXT("packageKind"), static_cast<int32>(EHotUpdatePackageKind::Patch));
	FileManifestObj->SetObjectField(TEXT("version"), VersionInfo);
	FileManifestObj->SetStringField(TEXT("baseVersion"), CurrentConfig.BaseVersion);

	FileManifestObj->SetObjectField(TEXT("diffSummary"), DiffSummary);
	FileManifestObj->SetArrayField(TEXT("containers"), ContainersArray);

	// 收集所有资源路径（从 DiffReport 推导），filemanifest 需要全量文件作为链式热更的 diff 基线
	TSet<FString> AllAssetPaths;
	TMap<FString, const FHotUpdateAssetDiff*> AssetDiffMap;

	for (const FHotUpdateAssetDiff& Diff : DiffReport.AddedAssets)
	{
		AllAssetPaths.Add(Diff.AssetPath);
		AssetDiffMap.Add(Diff.AssetPath, &Diff);
	}
	for (const FHotUpdateAssetDiff& Diff : DiffReport.ModifiedAssets)
	{
		AllAssetPaths.Add(Diff.AssetPath);
		AssetDiffMap.Add(Diff.AssetPath, &Diff);
	}
	for (const FHotUpdateAssetDiff& Diff : DiffReport.UnchangedAssets)
	{
		AllAssetPaths.Add(Diff.AssetPath);
		AssetDiffMap.Add(Diff.AssetPath, &Diff);
	}

	TArray<TSharedPtr<FJsonValue>> FilesArray;

	for (const FString& AssetPath : AllAssetPaths)
	{
		TSharedPtr<FJsonObject> FileObj = MakeShareable(new FJsonObject);
		FileObj->SetStringField(TEXT("filePath"), AssetPath);

		const FHotUpdateAssetDiff* Diff = AssetDiffMap.FindRef(AssetPath);
		if (Diff)
		{
			if (Diff->ChangeType == EHotUpdateFileChangeType::Added || Diff->ChangeType == EHotUpdateFileChangeType::Modified)
			{
				FileObj->SetStringField(TEXT("fileHash"), Diff->NewHash);
				FileObj->SetNumberField(TEXT("fileSize"), Diff->NewSize);
				FileObj->SetStringField(TEXT("source"), TEXT("patch"));
			}
			else if (Diff->ChangeType == EHotUpdateFileChangeType::Unchanged)
			{
				FileObj->SetStringField(TEXT("fileHash"), Diff->OldHash);
				FileObj->SetNumberField(TEXT("fileSize"), Diff->OldSize);
				FileObj->SetStringField(TEXT("source"), TEXT("base"));
			}
		}

		FileObj->SetBoolField(TEXT("isCompressed"), CurrentConfig.IoStoreConfig.CompressionFormat != TEXT("None"));
		FilesArray.Add(MakeShareable(new FJsonValueObject(FileObj)));
	}

	FileManifestObj->SetArrayField(TEXT("files"), FilesArray);

	// 序列化 fileManifest
	FString FileManifestString;
	TSharedRef<TJsonWriter<>> FileWriter = TJsonWriterFactory<>::Create(&FileManifestString);
	FJsonSerializer::Serialize(FileManifestObj.ToSharedRef(), FileWriter);

	return FFileHelper::SaveStringToFile(FileManifestString, *FileManifestPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void FHotUpdatePatchPackageBuilder::UpdateProgress(
	const FString& Stage,
	const FString& CurrentFile,
	int32 ProcessedFiles,
	int32 TotalFiles)
{
	FHotUpdatePackageProgress ProgressCopy;
	{
		FScopeLock Lock(&ProgressCriticalSection);
		HotUpdateProgressHelper::UpdateProgressData(CurrentProgress, Stage, CurrentFile, ProcessedFiles, TotalFiles);
		ProgressCopy = CurrentProgress;
	}

	if (CurrentConfig.bSynchronousMode)
	{
		HotUpdateProgressHelper::BroadcastProgressSync(ProgressCopy, OnProgress);
	}
	else
	{
		HotUpdateProgressHelper::BroadcastProgressAsync(ProgressCopy, OnProgress);
	}
}

void FHotUpdatePatchPackageBuilder::SplitHashesByAssetType(
	const TMap<FString, FString>& AllHashes,
	const TMap<FString, int64>& AllSizes,
	TMap<FString, FString>& OutAssetHashes,
	TMap<FString, int64>& OutAssetSizes,
	TMap<FString, FString>& OutNonAssetHashes,
	TMap<FString, int64>& OutNonAssetSizes)
{
	for (const auto& Pair : AllHashes)
	{
		const FString& Path = Pair.Key;
		// Manifest 中虚拟路径无扩展名（如 /Game/TopDown/Lvl_TopDown）是 UE 资产
		// 有扩展名的（如 /Game/Setting/txt_pak.txt）是非资产文件
		const FString Extension = FPaths::GetExtension(Path);
		const bool bIsAsset = Extension.IsEmpty() || Path.EndsWith(TEXT(".uasset")) || Path.EndsWith(TEXT(".umap"));
		if (bIsAsset)
		{
			OutAssetHashes.Add(Path, Pair.Value);
			if (const int64* Size = AllSizes.Find(Path))
			{
				OutAssetSizes.Add(Path, *Size);
			}
		}
		else
		{
			OutNonAssetHashes.Add(Path, Pair.Value);
			if (const int64* Size = AllSizes.Find(Path))
			{
				OutNonAssetSizes.Add(Path, *Size);
			}
		}
	}
}

FHotUpdateDiffReport FHotUpdatePatchPackageBuilder::MergeDiffReports(
	const FHotUpdateDiffReport& AssetReport,
	const FHotUpdateDiffReport& NonAssetReport,
	const FString& BaseVersion,
	const FString& TargetVersion)
{
	FHotUpdateDiffReport Result;
	Result.BaseVersion = BaseVersion;
	Result.TargetVersion = TargetVersion;

	Result.AddedAssets = AssetReport.AddedAssets;
	Result.AddedAssets.Append(NonAssetReport.AddedAssets);

	Result.ModifiedAssets = AssetReport.ModifiedAssets;
	Result.ModifiedAssets.Append(NonAssetReport.ModifiedAssets);

	Result.DeletedAssets = AssetReport.DeletedAssets;
	Result.DeletedAssets.Append(NonAssetReport.DeletedAssets);

	Result.UnchangedAssets = AssetReport.UnchangedAssets;
	Result.UnchangedAssets.Append(NonAssetReport.UnchangedAssets);

	return Result;
}

FHotUpdatePatchPackageResult FHotUpdatePatchPackageBuilder::MakeErrorResult(const FString& ErrorMessage)
{
	FHotUpdatePatchPackageResult Result;
	Result.bSuccess = false;
	Result.ErrorMessage = ErrorMessage;
	return Result;
}

// === BuildPatchPackage 阶段函数实现 ===

bool FHotUpdatePatchPackageBuilder::PrepareBuild(FPatchBuildContext& Ctx)
{
	// 验证配置
	FString ErrorMessage;
	if (!ValidateConfig(CurrentConfig, ErrorMessage))
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("配置验证失败: %s"), *ErrorMessage);
		CurrentResult = MakeErrorResult(ErrorMessage);
		return false;
	}

	// 编译项目：确保 Cook 使用最新的游戏代码
	if (!CurrentConfig.bSkipBuild)
	{
		UpdateProgress(TEXT("编译项目"), TEXT(""), 0, 0);
		if (!FHotUpdatePackageHelper::CompileProject(CurrentConfig.Platform))
		{
			CurrentResult = MakeErrorResult(TEXT("项目编译失败"));
			return false;
		}
	}
	else
	{
		UE_LOG(LogHotUpdateEditor, Log, TEXT("跳过编译步骤 (bSkipBuild = true)"));
	}

	// Cook 资源（增量模式延迟到 ComputeChanges 阶段）
	if (!CurrentConfig.bSkipCook)
	{
		if (CurrentConfig.bIncrementalCook)
		{
			UE_LOG(LogHotUpdateEditor, Log, TEXT("增量 Cook 模式: Cook 将在 Diff 之后执行"));
		}
		else
		{
			UpdateProgress(TEXT("Cook 资源"), TEXT(""), 0, 0);
			if (!FHotUpdatePackageHelper::CookAssets(CurrentConfig.Platform))
			{
				CurrentResult = MakeErrorResult(TEXT("Cook 资源失败"));
				return false;
			}
		}
	}
	else
	{
		UE_LOG(LogHotUpdateEditor, Log, TEXT("跳过 Cook 步骤 (bSkipCook = true)"));
	}

	return true;
}

bool FHotUpdatePatchPackageBuilder::ComputeChanges(FPatchBuildContext& Ctx)
{
	// 1. 加载基础版本 FileManifest
	UpdateProgress(TEXT("加载基础版本"), TEXT(""), 0, 0);

	FString ManifestLoadError;
	if (!LoadBaseFileManifest(CurrentConfig.BaseFileManifestPath.FilePath, Ctx.BaseManifestData, ManifestLoadError))
	{
		CurrentResult = MakeErrorResult(ManifestLoadError);
		return false;
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("加载了基础版本 %d 个资产, %d 个非资产"),
		Ctx.BaseManifestData.AssetHashes.Num(), Ctx.BaseManifestData.NonAssetHashes.Num());

	// 从 Manifest 提取版本号
	Ctx.ActualBaseVersion = HotUpdateUtils::ExtractVersionFromManifest(CurrentConfig.BaseFileManifestPath.FilePath);
	if (Ctx.ActualBaseVersion.IsEmpty())
	{
		Ctx.ActualBaseVersion = CurrentConfig.BaseVersion;
	}
	else
	{
		UE_LOG(LogHotUpdateEditor, Log, TEXT("从 Manifest 更新 BaseVersion 为: %s"), *Ctx.ActualBaseVersion);
	}

	// 2. 收集当前资源
	UpdateProgress(TEXT("收集资源"), TEXT(""), 0, 0);
	FString ErrorMessage;
	if (!CollectAssets(ErrorMessage))
	{
		CurrentResult = MakeErrorResult(ErrorMessage);
		return false;
	}

	// 收集源文件路径
	if (!CollectSourceFilePaths(Ctx.AssetSourcePaths, Ctx.NonAssetSourcePaths, ErrorMessage))
	{
		CurrentResult = MakeErrorResult(TEXT("没有可打包的资源文件"));
		return false;
	}

	// 构建绝对路径 -> 资产虚拟路径映射（用于 manifest 生成）
	for (const FString& AssetPath : CurrentConfig.AssetPaths)
	{
		const FString SourcePath = FHotUpdatePackageHelper::GetAssetSourcePath(AssetPath);
		if (!SourcePath.IsEmpty())
		{
			FString AbsPath = FPaths::ConvertRelativePathToFull(SourcePath);
			Ctx.AbsolutePathToAssetPath.Add(AbsPath, AssetPath);
		}
	}
	for (const FString& FilePath : CurrentConfig.NonAssetPaths)
	{
		FString AbsPath = FPaths::ConvertRelativePathToFull(FilePath);
		FString VirtualPath = FHotUpdatePackageHelper::FilePathToContentMountPath(FilePath);
		if (!VirtualPath.IsEmpty())
		{
			Ctx.AbsolutePathToAssetPath.Add(AbsPath, VirtualPath);
		}
	}

	// 3. 计算当前资源 Hash 和 Size（使用虚拟路径作为 key，与基础 manifest 一致）
	UpdateProgress(TEXT("计算资产 Hash"), TEXT(""), 0, Ctx.AssetSourcePaths.Num());
	for (int32 i = 0; i < Ctx.AssetSourcePaths.Num(); i++)
	{
		if (bIsCancelled)
		{
			CurrentResult = MakeErrorResult(TEXT("构建已取消"));
			return false;
		}

		const FString& SourcePath = Ctx.AssetSourcePaths[i];
		const FString* VirtualPath = Ctx.AbsolutePathToAssetPath.Find(SourcePath);
		FString Key = VirtualPath ? *VirtualPath : SourcePath;
		Ctx.CurrentAssetHashes.Add(Key, UHotUpdateFileUtils::CalculateFileHash(SourcePath));
		Ctx.CurrentAssetSizes.Add(Key, IFileManager::Get().FileSize(*SourcePath));
		UpdateProgress(TEXT("计算资产 Hash"), SourcePath, i + 1, Ctx.AssetSourcePaths.Num());
	}

	UpdateProgress(TEXT("计算非资产 Hash"), TEXT(""), 0, Ctx.NonAssetSourcePaths.Num());
	for (int32 i = 0; i < Ctx.NonAssetSourcePaths.Num(); i++)
	{
		if (bIsCancelled)
		{
			CurrentResult = MakeErrorResult(TEXT("构建已取消"));
			return false;
		}

		const FString& SourcePath = Ctx.NonAssetSourcePaths[i];
		const FString* VirtualPath = Ctx.AbsolutePathToAssetPath.Find(SourcePath);
		FString Key = VirtualPath ? *VirtualPath : SourcePath;
		Ctx.CurrentNonAssetHashes.Add(Key, UHotUpdateFileUtils::CalculateFileHash(SourcePath));
		Ctx.CurrentNonAssetSizes.Add(Key, IFileManager::Get().FileSize(*SourcePath));
		UpdateProgress(TEXT("计算非资产 Hash"), SourcePath, i + 1, Ctx.NonAssetSourcePaths.Num());
	}

	// 4. 计算差异（使用虚拟路径作为 key，与基础 manifest 一致）
	UpdateProgress(TEXT("计算差异"), TEXT(""), 0, 0);

	// 将绝对路径数组转换为虚拟路径数组
	TArray<FString> AssetVirtualPaths;
	for (const FString& AbsPath : Ctx.AssetSourcePaths)
	{
		const FString* VirtualPath = Ctx.AbsolutePathToAssetPath.Find(AbsPath);
		AssetVirtualPaths.Add(VirtualPath ? *VirtualPath : AbsPath);
	}
	TArray<FString> NonAssetVirtualPaths;
	for (const FString& AbsPath : Ctx.NonAssetSourcePaths)
	{
		const FString* VirtualPath = Ctx.AbsolutePathToAssetPath.Find(AbsPath);
		NonAssetVirtualPaths.Add(VirtualPath ? *VirtualPath : AbsPath);
	}

	TArray<FString> ChangedAssetPaths;
	TArray<FString> ChangedNonAssetPaths;
	FHotUpdateDiffReport AssetDiffReport;
	FHotUpdateDiffReport NonAssetDiffReport;

	if (!ComputeDiff(AssetVirtualPaths, Ctx.CurrentAssetHashes, Ctx.CurrentAssetSizes, Ctx.BaseManifestData.AssetHashes, Ctx.BaseManifestData.AssetSizes, ChangedAssetPaths, AssetDiffReport))
	{
		CurrentResult = MakeErrorResult(TEXT("计算资产差异失败"));
		return false;
	}

	if (!ComputeDiff(NonAssetVirtualPaths, Ctx.CurrentNonAssetHashes, Ctx.CurrentNonAssetSizes, Ctx.BaseManifestData.NonAssetHashes, Ctx.BaseManifestData.NonAssetSizes, ChangedNonAssetPaths, NonAssetDiffReport))
	{
		CurrentResult = MakeErrorResult(TEXT("计算非资产差异失败"));
		return false;
	}

	// 合并 DiffReport
	Ctx.DiffReport.BaseVersion = CurrentConfig.BaseVersion;
	Ctx.DiffReport.TargetVersion = CurrentConfig.PatchVersion;
	Ctx.DiffReport.AddedAssets = AssetDiffReport.AddedAssets;
	Ctx.DiffReport.AddedAssets.Append(NonAssetDiffReport.AddedAssets);
	Ctx.DiffReport.ModifiedAssets = AssetDiffReport.ModifiedAssets;
	Ctx.DiffReport.ModifiedAssets.Append(NonAssetDiffReport.ModifiedAssets);
	Ctx.DiffReport.DeletedAssets = AssetDiffReport.DeletedAssets;
	Ctx.DiffReport.DeletedAssets.Append(NonAssetDiffReport.DeletedAssets);
	Ctx.DiffReport.UnchangedAssets = AssetDiffReport.UnchangedAssets;
	Ctx.DiffReport.UnchangedAssets.Append(NonAssetDiffReport.UnchangedAssets);

	UE_LOG(LogHotUpdateEditor, Display, TEXT("差异: 资产(新增 %d, 修改 %d), 非资产(新增 %d, 修改 %d)"),
		AssetDiffReport.AddedAssets.Num(), AssetDiffReport.ModifiedAssets.Num(),
		NonAssetDiffReport.AddedAssets.Num(), NonAssetDiffReport.ModifiedAssets.Num());

	// === 增量 Cook：在 Diff 之后执行 ===
	if (CurrentConfig.bIncrementalCook && !CurrentConfig.bSkipCook)
	{
		// 提取修改资源（有 Cooked 输出，Hash 变了）
		TArray<FString> AssetsToCook;
		TSet<FString> AddedAssetsSet;
		UE_LOG(LogHotUpdateEditor, Display, TEXT("增量 Cook: ModifiedAssets.Num=%d"), Ctx.DiffReport.ModifiedAssets.Num());
		for (const FHotUpdateAssetDiff& Diff : Ctx.DiffReport.ModifiedAssets)
		{
			if (FHotUpdatePackageHelper::IsValidPackagePath(Diff.AssetPath))
			{
				FString PackageName = FHotUpdatePackageHelper::FilePathToLongPackageName(Diff.AssetPath);
				if (!PackageName.IsEmpty())
				{
					if (!AddedAssetsSet.Contains(PackageName))
					{
						AssetsToCook.Add(PackageName);
						AddedAssetsSet.Add(PackageName);
						UE_LOG(LogHotUpdateEditor, Display, TEXT("增量 Cook: 添加修改资产: %s -> %s"), *Diff.AssetPath, *PackageName);
					}
				}
			}
			else
			{
				UE_LOG(LogHotUpdateEditor, Display, TEXT("增量 Cook: 跳过非资产文件: %s"), *Diff.AssetPath);
			}
		}

		// 不在基础 FileManifest 中的资源 = 新增资源
		TArray<FString> AllPackagingAssetPaths;
		if (CurrentConfig.AssetPaths.Num() > 0)
		{
			AllPackagingAssetPaths = CurrentConfig.AssetPaths;
		}
		else
		{
			AllPackagingAssetPaths = FHotUpdatePackagingSettingsHelper::ParsePackagingSettings(true).AssetPaths;
		}

		TSet<FString> BaseAssetSet;
		for (const auto& Pair : Ctx.BaseManifestData.AssetHashes)
		{
			BaseAssetSet.Add(Pair.Key);
		}

		for (const FString& AssetPath : AllPackagingAssetPaths)
		{
			if (FHotUpdatePackageHelper::IsExternalAsset(AssetPath))
			{
				continue;
			}

			FString AssetSourcePath = FHotUpdatePackageHelper::GetAssetSourcePath(AssetPath);
			if (AssetSourcePath.IsEmpty())
			{
				AssetsToCook.Add(AssetPath);
				UE_LOG(LogHotUpdateEditor, Warning, TEXT("增量 Cook: 无法获取源路径，视为新增: %s"), *AssetPath);
				continue;
			}

			if (!BaseAssetSet.Contains(AssetPath))
			{
				if (!AddedAssetsSet.Contains(AssetPath))
				{
					AssetsToCook.Add(AssetPath);
					AddedAssetsSet.Add(AssetPath);
					UE_LOG(LogHotUpdateEditor, Display, TEXT("增量 Cook: 发现新增资源: %s"), *AssetPath);
				}
			}
		}

		UE_LOG(LogHotUpdateEditor, Display, TEXT("增量 Cook: 需要 Cook %d 个资源 (修改 %d + 新增)"), AssetsToCook.Num(), Ctx.DiffReport.ModifiedAssets.Num());

		if (AssetsToCook.Num() > 0)
		{
			TArray<FString> AssetsWithDeps = FHotUpdatePackageHelper::CollectDependenciesAndFilterEngine(AssetsToCook);

			UpdateProgress(TEXT("增量 Cook 资源"), TEXT(""), 0, AssetsWithDeps.Num());
			if (!FHotUpdatePackageHelper::CookAssets(CurrentConfig.Platform, AssetsWithDeps))
			{
				UE_LOG(LogHotUpdateEditor, Warning, TEXT("增量 Cook 失败"));
				CurrentResult = MakeErrorResult(TEXT("Cook 资源失败"));
				return false;
			}
		}
		else
		{
			UE_LOG(LogHotUpdateEditor, Log, TEXT("增量 Cook: 没有需要 Cook 的资源变更"));
		}
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("增量模式: 变更资源 (新增 %d + 修改 %d)"), Ctx.DiffReport.AddedAssets.Num(), Ctx.DiffReport.ModifiedAssets.Num());

	// 检查是否有变更
	UE_LOG(LogHotUpdateEditor, Display, TEXT("ChangedAssetPaths.Num=%d, ChangedNonAssetPaths.Num=%d"), ChangedAssetPaths.Num(), ChangedNonAssetPaths.Num());

	Ctx.ChangedAssetPaths = MoveTemp(ChangedAssetPaths);
	Ctx.ChangedNonAssetPaths = MoveTemp(ChangedNonAssetPaths);

	if ((Ctx.ChangedAssetPaths.Num() + Ctx.ChangedNonAssetPaths.Num()) == 0)
	{
		CurrentResult.bSuccess = true;
		CurrentResult.ErrorMessage = TEXT("没有发现资源变更");
		CurrentResult.DiffReport = Ctx.DiffReport;
		return false;
	}

	return true;
}

bool FHotUpdatePatchPackageBuilder::CreatePatchContainer(FPatchBuildContext& Ctx)
{
	// 确定输出目录
	Ctx.OutputDir = CurrentConfig.OutputDirectory.Path;
	if (Ctx.OutputDir.IsEmpty())
	{
		Ctx.OutputDir = FPaths::ProjectSavedDir() / TEXT("HotUpdateVersions");
	}

	FString PlatformStr = HotUpdateUtils::GetPlatformString(CurrentConfig.Platform);
	Ctx.OutputDir = FPaths::Combine(Ctx.OutputDir, CurrentConfig.PatchVersion, PlatformStr);
	FPaths::NormalizeDirectoryName(Ctx.OutputDir);

	IPlatformFile::GetPlatformPhysical().CreateDirectoryTree(*Ctx.OutputDir);

	// 创建 Patch IoStore 容器
	UpdateProgress(TEXT("创建 Patch 容器"), TEXT(""), 0, (Ctx.ChangedAssetPaths.Num() + Ctx.ChangedNonAssetPaths.Num()));

	if ((Ctx.ChangedAssetPaths.Num() + Ctx.ChangedNonAssetPaths.Num()) > 0)
	{
		FHotUpdateIoStoreBuilder IoStoreBuilder;

		FHotUpdateIoStoreConfig IoStoreConfig = CurrentConfig.IoStoreConfig;
		IoStoreConfig.bUseIoStore = false;
		IoStoreConfig.ContainerName = FString::Printf(TEXT("Patch_%s_P"), *CurrentConfig.PatchVersion);

		FString PaksDir = FPaths::Combine(Ctx.OutputDir, TEXT("Paks"));
		IPlatformFile::GetPlatformPhysical().CreateDirectoryTree(*PaksDir);

		FString PatchOutputPath = FPaths::Combine(PaksDir, IoStoreConfig.ContainerName);

		TArray<FString> VirtualPackagePaths;
		VirtualPackagePaths.Reserve(Ctx.ChangedAssetPaths.Num() + Ctx.ChangedNonAssetPaths.Num());

		// ChangedAssetPaths 已经是虚拟路径（/Game/...），直接使用
		VirtualPackagePaths.Append(Ctx.ChangedAssetPaths);
		VirtualPackagePaths.Append(Ctx.ChangedNonAssetPaths);

		FString CookedPlatformDir = HotUpdateUtils::GetCookedPlatformDir(CurrentConfig.Platform);
		FHotUpdateIoStoreResult IoStoreResult = IoStoreBuilder.BuildIoStoreContainer(VirtualPackagePaths, PatchOutputPath, IoStoreConfig, CookedPlatformDir);

		if (!IoStoreResult.bSuccess)
		{
			CurrentResult = MakeErrorResult(FString::Printf(TEXT("Patch 容器创建失败: %s"), *IoStoreResult.ErrorMessage));
			return false;
		}

		Ctx.PatchUtocPath = IoStoreResult.UtocPath;
		Ctx.PatchUcasPath = IoStoreResult.UcasPath;
		Ctx.PatchSize = IoStoreResult.ContainerSize;

		UE_LOG(LogHotUpdateEditor, Log, TEXT("Patch 容器创建成功: %s, 大小 %lld 字节"), *Ctx.PatchUtocPath, Ctx.PatchSize);
	}

	return true;
}

bool FHotUpdatePatchPackageBuilder::BuildAndRegisterManifest(FPatchBuildContext& Ctx, FHotUpdatePatchPackageResult& Result)
{
	// 从基础版本 Manifest 解析容器信息
	FString BaseManifestJson;
	if (FFileHelper::LoadFileToString(BaseManifestJson, *CurrentConfig.BaseFileManifestPath.FilePath))
	{
		TSharedPtr<FJsonObject> BaseManifestObj;
		TSharedRef<TJsonReader<>> BaseReader = TJsonReaderFactory<>::Create(BaseManifestJson);
		if (FJsonSerializer::Deserialize(BaseReader, BaseManifestObj) && BaseManifestObj.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* ContainersArray = nullptr;
			bool bFound = BaseManifestObj->TryGetArrayField(TEXT("containers"), ContainersArray);

			if (bFound && ContainersArray)
			{
				for (const TSharedPtr<FJsonValue>& ContainerValue : *ContainersArray)
				{
					TSharedPtr<FJsonObject> ContainerObj = ContainerValue->AsObject();
					if (!ContainerObj.IsValid()) continue;

					FString ContainerType;
					if (ContainerObj->TryGetStringField(TEXT("containerType"), ContainerType) && !ContainerType.StartsWith(TEXT("patch")))
					{
						continue;
					}

					FHotUpdateContainerInfo Info;
					ContainerObj->TryGetStringField(TEXT("containerName"), Info.ContainerName);

					ContainerObj->TryGetStringField(TEXT("utocPath"), Info.UtocFile.Path);
					ContainerObj->TryGetNumberField(TEXT("utocSize"), Info.UtocFile.Size);
					ContainerObj->TryGetStringField(TEXT("utocHash"), Info.UtocFile.Hash);

					ContainerObj->TryGetStringField(TEXT("pakPath"), Info.PakFile.Path);
					ContainerObj->TryGetNumberField(TEXT("pakSize"), Info.PakFile.Size);
					ContainerObj->TryGetStringField(TEXT("pakHash"), Info.PakFile.Hash);

					FString UcasPath;
					if (ContainerObj->TryGetStringField(TEXT("ucasPath"), UcasPath) && !UcasPath.IsEmpty())
					{
						Info.UcasFile.Path = UcasPath;
						Info.UcasFile.Size = (int64)ContainerObj->GetNumberField(TEXT("ucasSize"));
						Info.UcasFile.Hash = ContainerObj->GetStringField(TEXT("ucasHash"));
					}

					Info.ContainerType = EHotUpdateContainerType::Patch;
					if (ContainerObj->HasField(TEXT("version")))
					{
						Info.Version = ContainerObj->GetStringField(TEXT("version"));
					}
					else
					{
						Info.Version = Ctx.ActualBaseVersion;
					}

					Ctx.BaseContainers.Add(Info);
				}

				UE_LOG(LogHotUpdateEditor, Log, TEXT("从 Manifest 加载了 %d 个 patch 容器引用"), Ctx.BaseContainers.Num());
			}
		}
	}

	// 生成 Manifest
	UpdateProgress(TEXT("生成 Manifest"), TEXT(""), 0, 0);

	CurrentConfig.BaseVersion = Ctx.ActualBaseVersion;

	FString ManifestPath = FPaths::Combine(Ctx.OutputDir, TEXT("manifest.json"));

	if (!GenerateManifest(ManifestPath, Ctx.PatchUtocPath, Ctx.PatchUcasPath, Ctx.DiffReport, Ctx.BaseContainers))
	{
		CurrentResult = MakeErrorResult(TEXT("生成 Manifest 失败"));
		return false;
	}

	// 注册版本
	UpdateProgress(TEXT("注册版本"), TEXT(""), 0, 0);

	FHotUpdateVersionManager VersionManager;

	FHotUpdateEditorVersionInfo VersionInfo;
	VersionInfo.VersionString = CurrentConfig.PatchVersion;
	VersionInfo.PackageKind = EHotUpdatePackageKind::Patch;
	VersionInfo.BaseVersion = CurrentConfig.BaseVersion;
	VersionInfo.Platform = CurrentConfig.Platform;
	VersionInfo.CreatedTime = FDateTime::Now();
	VersionInfo.FileManifestPath = FPaths::Combine(Ctx.OutputDir, TEXT("filemanifest.json"));
	VersionInfo.UtocPath = Ctx.PatchUtocPath;
	VersionInfo.AssetCount = Ctx.DiffReport.AddedAssets.Num() + Ctx.DiffReport.ModifiedAssets.Num() + Ctx.DiffReport.UnchangedAssets.Num();
	VersionInfo.PackageSize = Ctx.PatchSize;

	VersionManager.RegisterVersion(VersionInfo);

	// 填充结果
	Result.bSuccess = true;
	Result.OutputDirectory = Ctx.OutputDir;
	Result.PatchVersion = CurrentConfig.PatchVersion;
	Result.BaseVersion = Ctx.ActualBaseVersion;
	Result.DiffReport = Ctx.DiffReport;
	Result.ChangedAssetCount = (Ctx.ChangedAssetPaths.Num() + Ctx.ChangedNonAssetPaths.Num());
	Result.PatchUtocPath = Ctx.PatchUtocPath;
	Result.PatchUcasPath = Ctx.PatchUcasPath;
	Result.PatchSize = Ctx.PatchSize;
	Result.ManifestFilePath = ManifestPath;

	UE_LOG(LogHotUpdateEditor, Log, TEXT("更新包构建成功: %s, 变更 %d 个资源, 大小 %lld 字节"), *Ctx.OutputDir, Result.ChangedAssetCount, Ctx.PatchSize);

	return true;
}

// === 阶段子函数实现 ===

bool FHotUpdatePatchPackageBuilder::CollectSourceFilePaths(
	TArray<FString>& OutAssetSourcePaths,
	TArray<FString>& OutNonAssetSourcePaths,
	FString& OutErrorMessage)
{
	// 收集 UE 资产源文件路径
	for (const FString& AssetPath : CurrentConfig.AssetPaths)
	{
		const FString SourcePath = FHotUpdatePackageHelper::GetAssetSourcePath(AssetPath);
		if (!SourcePath.IsEmpty() && FPaths::FileExists(*SourcePath))
		{
			OutAssetSourcePaths.Add(FPaths::ConvertRelativePathToFull(SourcePath));
		}
		else
		{
			UE_LOG(LogHotUpdateEditor, Verbose, TEXT("跳过源文件不存在的资产: %s -> %s"), *AssetPath, *SourcePath);
		}
	}

	// 收集非资产源文件路径
	for (const FString& FilePath : CurrentConfig.NonAssetPaths)
	{
		if (FPaths::FileExists(*FilePath))
		{
			OutNonAssetSourcePaths.Add(FPaths::ConvertRelativePathToFull(FilePath));
		}
		else
		{
			UE_LOG(LogHotUpdateEditor, Verbose, TEXT("跳过源文件不存在的非资产文件: %s"), *FilePath);
		}
	}

	return OutAssetSourcePaths.Num() > 0 || OutNonAssetSourcePaths.Num() > 0;
}


