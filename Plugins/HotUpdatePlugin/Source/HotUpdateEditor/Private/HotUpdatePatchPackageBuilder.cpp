// Copyright czm. All Rights Reserved.

#include "HotUpdatePatchPackageBuilder.h"
#include "Core/HotUpdateFileUtils.h"
#include "HotUpdateEditor.h"
#include "HotUpdateUtils.h"
#include "HotUpdatePackagingSettingsHelper.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "Misc/SecureHash.h"
#include "JsonObjectConverter.h"

UHotUpdatePatchPackageBuilder::UHotUpdatePatchPackageBuilder()
	: bIsBuilding(false)
	, bIsCancelled(false)
{
}

FHotUpdatePatchPackageResult UHotUpdatePatchPackageBuilder::BuildPatchPackage(const FHotUpdatePatchPackageConfig& Config)
{
	UE_LOG(LogHotUpdateEditor, Log, TEXT("BuildPatchPackage (同步) 开始调用"));

	FHotUpdatePatchPackageResult Result;

	// 验证配置
	FString ErrorMessage;
	if (!ValidateConfig(Config, ErrorMessage))
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("配置验证失败: %s"), *ErrorMessage);
		Result.bSuccess = false;
		Result.ErrorMessage = ErrorMessage;
		return Result;
	}

	// 注意：不在此同步版本中检查 bIsBuilding，因为异步版本会在调用前设置此标志
	// 直接调用此同步方法的用户需要自行管理并发控制

	bIsCancelled = false;

	// Cook 资源：确保使用最新的 cooked 文件
	if (!Config.bSkipCook)
	{
		UpdateProgress(TEXT("Cook 资源"), TEXT(""), 0, 0);
		if (!CookAssets(Config))
		{
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("Cook 资源失败");
			bIsBuilding = false;
			return Result;
		}
	}
	else
	{
		UE_LOG(LogHotUpdateEditor, Log, TEXT("跳过 Cook 步骤 (bSkipCook = true)"));
	}

	// 1. 加载基础版本 Manifest
	UpdateProgress(TEXT("加载基础版本"), TEXT(""), 0, 0);

	TMap<FString, FString> BaseAssetHashes;
	TMap<FString, int64> BaseAssetSizes;

	if (!LoadBaseManifest(Config.BaseManifestPath.FilePath, BaseAssetHashes, BaseAssetSizes))
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("无法加载基础版本 Manifest");
		bIsBuilding = false;
		return Result;
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("加载了基础版本 %d 个资源"), BaseAssetHashes.Num());

	// 从 manifest 中读取实际版本号作为 BaseVersion
	FString ActualBaseVersion = Config.BaseVersion;
	{
		FString ManifestContent;
		if (FFileHelper::LoadFileToString(ManifestContent, *Config.BaseManifestPath.FilePath))
		{
			TSharedPtr<FJsonObject> ManifestObj;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ManifestContent);
			if (FJsonSerializer::Deserialize(Reader, ManifestObj) && ManifestObj.IsValid())
			{
				const TSharedPtr<FJsonObject>* VersionObj;
				if (ManifestObj->TryGetObjectField(TEXT("version"), VersionObj))
				{
					FString ManifestVersion;
					if (VersionObj->Get()->TryGetStringField(TEXT("version"), ManifestVersion))
					{
						ActualBaseVersion = ManifestVersion;
						UE_LOG(LogHotUpdateEditor, Log, TEXT("从 Manifest 更新 BaseVersion 为: %s"), *ActualBaseVersion);
					}
				}
				else if (ManifestObj->TryGetObjectField(TEXT("versionInfo"), VersionObj))
				{
					FString ManifestVersion;
					if (VersionObj->Get()->TryGetStringField(TEXT("versionString"), ManifestVersion))
					{
						ActualBaseVersion = ManifestVersion;
						UE_LOG(LogHotUpdateEditor, Log, TEXT("从 Manifest (旧格式) 更新 BaseVersion 为: %s"), *ActualBaseVersion);
					}
				}
			}
		}
	}

	// 2. 收集当前资源
	UpdateProgress(TEXT("收集资源"), TEXT(""), 0, 0);

	TArray<FString> CurrentAssetPaths;
	TMap<FString, FString> CurrentAssetDiskPaths;

	if (!CollectAssets(Config, CurrentAssetPaths, CurrentAssetDiskPaths, ErrorMessage))
	{
		Result.bSuccess = false;
		Result.ErrorMessage = ErrorMessage;
		bIsBuilding = false;
		return Result;
	}

	// 3. 计算当前资源 Hash
	UpdateProgress(TEXT("计算资源 Hash"), TEXT(""), 0, CurrentAssetPaths.Num());

	TMap<FString, FString> CurrentAssetHashes;
	TMap<FString, int64> CurrentAssetSizes;

	for (int32 i = 0; i < CurrentAssetPaths.Num(); i++)
	{
		if (bIsCancelled)
		{
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("构建已取消");
			bIsBuilding = false;
			return Result;
		}

		const FString& AssetPath = CurrentAssetPaths[i];
		const FString* DiskPath = CurrentAssetDiskPaths.Find(AssetPath);

		if (DiskPath && FPaths::FileExists(**DiskPath))
		{
			CurrentAssetHashes.Add(AssetPath, UHotUpdateFileUtils::CalculateFileHash(*DiskPath));
			CurrentAssetSizes.Add(AssetPath, IFileManager::Get().FileSize(**DiskPath));
		}

		UpdateProgress(TEXT("计算资源 Hash"), AssetPath, i + 1, CurrentAssetPaths.Num());
	}

	// 4. 计算差异
	UpdateProgress(TEXT("计算差异"), TEXT(""), 0, 0);

	TArray<FString> ChangedAssets;
	FHotUpdateDiffReport DiffReport;

	if (!ComputeDiff(CurrentAssetPaths, CurrentAssetHashes, BaseAssetHashes, ChangedAssets, DiffReport))
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("计算差异失败");
		bIsBuilding = false;
		return Result;
	}

	DiffReport.BaseVersion = Config.BaseVersion;
	DiffReport.TargetVersion = Config.PatchVersion;

	UE_LOG(LogHotUpdateEditor, Log, TEXT("差异: 新增 %d, 修改 %d, 删除 %d"),
		DiffReport.AddedAssets.Num(), DiffReport.ModifiedAssets.Num(), DiffReport.DeletedAssets.Num());

	// 增量模式：只打包变更的资源（新增 + 修改）
	// 用于打包的资源列表（只包含变更资源）
	TMap<FString, FString> ChangedAssetDiskPaths;

	// Cooked 平台目录，用于解析运行时需要的 cooked 格式文件路径
		FString CookedPlatformDir = FPaths::ProjectSavedDir() / TEXT("Cooked") / HotUpdateUtils::GetPlatformString(Config.Platform);
	// 添加新增资源的磁盘路径
	for (const FHotUpdateAssetDiff& AddedDiff : DiffReport.AddedAssets)
	{
		FString DiskPath = GetAssetDiskPath(AddedDiff.AssetPath, CookedPlatformDir);
		if (!DiskPath.IsEmpty() && FPaths::FileExists(*DiskPath))
		{
			ChangedAssetDiskPaths.Add(AddedDiff.AssetPath, DiskPath);
		}
	}

	// 添加修改资源的磁盘路径
	for (const FHotUpdateAssetDiff& ModifiedDiff : DiffReport.ModifiedAssets)
	{
		FString DiskPath = GetAssetDiskPath(ModifiedDiff.AssetPath, CookedPlatformDir);
		if (!DiskPath.IsEmpty() && FPaths::FileExists(*DiskPath))
		{
			ChangedAssetDiskPaths.Add(ModifiedDiff.AssetPath, DiskPath);
		}
	}

	// 用于打包的资源列表
	TArray<FString> AssetsToPackage;
	for (const auto& Pair : ChangedAssetDiskPaths)
	{
		AssetsToPackage.Add(Pair.Key);
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("增量模式: 变更资源数 %d (新增 %d + 修改 %d)"),
		AssetsToPackage.Num(), DiffReport.AddedAssets.Num(), DiffReport.ModifiedAssets.Num());

	// 检查是否有变更
	// 全量热更新模式：即使没有变更，也需要复制基础容器
	// 增量模式：没有变更则提前返回
	bool bHasChanges = ChangedAssets.Num() > 0;
	bool bIsFullHotUpdate = Config.bIncludeBaseContainers && !Config.BaseContainerDirectory.Path.IsEmpty();

	if (!bHasChanges && !bIsFullHotUpdate)
	{
		Result.bSuccess = true;
		Result.ErrorMessage = TEXT("没有发现资源变更");
		Result.DiffReport = DiffReport;
		bIsBuilding = false;
		return Result;
	}

	// 5. 确定输出目录
	FString OutputDir = Config.OutputDirectory.Path;
	if (OutputDir.IsEmpty())
	{
		OutputDir = FPaths::ProjectSavedDir() / TEXT("HotUpdatePatches");
	}

	FString PlatformStr = HotUpdateUtils::GetPlatformString(Config.Platform);
	OutputDir = FPaths::Combine(OutputDir, Config.PatchVersion, PlatformStr);
	FPaths::NormalizeDirectoryName(OutputDir);

	IPlatformFile::GetPlatformPhysical().CreateDirectoryTree(*OutputDir);

	Result.OutputDirectory = OutputDir;
	Result.PatchVersion = Config.PatchVersion;
	Result.BaseVersion = ActualBaseVersion;
	Result.DiffReport = DiffReport;
	Result.ChangedAssetCount = ChangedAssets.Num();
	Result.bRequiresBasePackage = true; // 默认需要基础包，全量热更新模式会在后面设置为 false

	// 6. 创建 Patch IoStore 容器
	// 全量热更新模式：即使没有变更，也需要复制基础容器
	// 增量模式：没有变更资源则跳过 Patch 容器创建
	UpdateProgress(TEXT("创建 Patch 容器"), TEXT(""), 0, AssetsToPackage.Num());

	FString PatchUtocPath;
	FString PatchUcasPath;
	int64 PatchSize = 0;

	if (ChangedAssetDiskPaths.Num() > 0)
	{
		UHotUpdateIoStoreBuilder* IoStoreBuilder = NewObject<UHotUpdateIoStoreBuilder>();

		FHotUpdateIoStoreConfig IoStoreConfig = Config.IoStoreConfig;
		IoStoreConfig.bUseIoStore = false;  // Patch 强制使用 .pak 格式
		// UE5 标准补丁命名格式：{项目名}-{平台}_{PatchIndex}_P
			// 补丁命名格式：{项目名}_P_{版本号}，如 MyProject_P_1.5.2
			// _P 后缀让引擎运行时自动提升 PakOrder，版本号保证多补丁不重名
			IoStoreConfig.ContainerName = FString::Printf(TEXT("Patch_%s_P"), *Config.PatchVersion);

		FString PaksDir = FPaths::Combine(OutputDir, TEXT("Paks"));
		IPlatformFile::GetPlatformPhysical().CreateDirectoryTree(*PaksDir);

		FString PatchOutputPath = FPaths::Combine(PaksDir, IoStoreConfig.ContainerName);

		FHotUpdateIoStoreResult IoStoreResult = IoStoreBuilder->BuildIoStoreContainer(
			ChangedAssetDiskPaths, PatchOutputPath, IoStoreConfig);

		if (!IoStoreResult.bSuccess)
		{
			Result.bSuccess = false;
			Result.ErrorMessage = FString::Printf(TEXT("Patch 容器创建失败: %s"), *IoStoreResult.ErrorMessage);
			bIsBuilding = false;
			return Result;
		}

		PatchUtocPath = IoStoreResult.UtocPath;
		PatchUcasPath = IoStoreResult.UcasPath;
		PatchSize = IoStoreResult.ContainerSize;

		UE_LOG(LogHotUpdateEditor, Log, TEXT("Patch 容器创建成功: %s, 大小 %lld 字节"), *PatchUtocPath, PatchSize);
	}
	else if (bIsFullHotUpdate)
	{
		UE_LOG(LogHotUpdateEditor, Log, TEXT("全量热更新模式: 无变更资源，跳过 Patch 容器创建，仅复制基础容器"));
	}
	else
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("没有有效的资源文件");
		bIsBuilding = false;
		return Result;
	}

	Result.PatchUtocPath = PatchUtocPath;
	Result.PatchUcasPath = PatchUcasPath;
	Result.PatchSize = PatchSize;

	// 6.5 加载之前的 Patch Manifest（链式模式）
	TArray<FHotUpdateContainerInfo> ChainPatchContainers;
	TMap<FString, FString> PreviousPatchFilesHash;
	TMap<FString, int64> PreviousPatchFilesSize;
	TArray<FString> PatchVersionChain;

	if (Config.bEnableChainPatch && Config.PreviousPatchManifestPaths.Num() > 0)
	{
		UpdateProgress(TEXT("加载之前的 Patch"), TEXT(""), 0, Config.PreviousPatchManifestPaths.Num());

		for (int32 i = 0; i < Config.PreviousPatchManifestPaths.Num(); i++)
		{
			if (bIsCancelled)
			{
				Result.bSuccess = false;
				Result.ErrorMessage = TEXT("构建已取消");
				bIsBuilding = false;
				return Result;
			}

			const FString& PrevManifestPath = Config.PreviousPatchManifestPaths[i].FilePath;

			TArray<FHotUpdateContainerInfo> PrevContainers;
			TMap<FString, FString> PrevFilesHash;
			TMap<FString, int64> PrevFilesSize;
			FString PrevPatchVersion;

			if (LoadPreviousPatchManifest(PrevManifestPath, PrevContainers, PrevFilesHash, PrevFilesSize, PrevPatchVersion))
			{
				// 添加之前的容器信息
				ChainPatchContainers.Append(PrevContainers);

				// 添加之前的 patch 文件信息
				PreviousPatchFilesHash.Append(PrevFilesHash);
				PreviousPatchFilesSize.Append(PrevFilesSize);

				// 记录版本链
				PatchVersionChain.Add(PrevPatchVersion);

				UE_LOG(LogHotUpdateEditor, Log, TEXT("链式 Patch: 加载版本 %s 的 %d 个容器"), *PrevPatchVersion, PrevContainers.Num());
			}
			else
			{
				UE_LOG(LogHotUpdateEditor, Warning, TEXT("链式 Patch: 无法加载之前的 Manifest: %s"), *PrevManifestPath);
			}

			UpdateProgress(TEXT("加载之前的 Patch"), PrevManifestPath, i + 1, Config.PreviousPatchManifestPaths.Num());
		}

		Result.bIsChainPatch = true;
		Result.ChainPatchContainers = ChainPatchContainers;
		Result.PatchVersionChain = PatchVersionChain;

		UE_LOG(LogHotUpdateEditor, Log, TEXT("链式 Patch: 共加载 %d 个之前的容器, %d 个之前的 Patch 文件"),
			ChainPatchContainers.Num(), PreviousPatchFilesHash.Num());

		// 复制之前版本的容器文件到当前输出目录
		UpdateProgress(TEXT("复制之前的容器文件"), TEXT(""), 0, ChainPatchContainers.Num());

		int32 CopiedCount = 0;
		for (const FHotUpdateContainerInfo& PrevContainer : ChainPatchContainers)
		{
			if (bIsCancelled)
			{
				Result.bSuccess = false;
				Result.ErrorMessage = TEXT("用户取消");
				bIsBuilding = false;
				return Result;
			}

			// 构建源文件路径（从之前版本目录）
			FString SourceUtocPath = PrevContainer.UtocPath;

			// UE5 标准容器名不再包含版本号，改用 PatchVersionChain 回退逻辑
			FString PrevVersion;
			int32 ContainerIndex = ChainPatchContainers.IndexOfByKey(PrevContainer);
			if (ContainerIndex != INDEX_NONE && ContainerIndex < PatchVersionChain.Num())
			{
				PrevVersion = PatchVersionChain[ContainerIndex];
			}
			if (!PrevVersion.IsEmpty())
			{
				// 查找源目录
				FString SourceBaseDir;
				for (const FFilePath& PrevManifestPath : Config.PreviousPatchManifestPaths)
				{
					if (PrevManifestPath.FilePath.Contains(PrevVersion))
					{
						SourceBaseDir = FPaths::GetPath(PrevManifestPath.FilePath);
						break;
					}
				}

				if (SourceBaseDir.IsEmpty())
				{
					SourceBaseDir = FPaths::Combine(Config.OutputDirectory.Path, PrevVersion, HotUpdateUtils::GetPlatformString(Config.Platform));
				}

				SourceUtocPath = FPaths::Combine(SourceBaseDir, PrevContainer.UtocPath);
			}

			// 构建目标文件路径（当前输出目录）
			FString DestUtocPath = FPaths::Combine(OutputDir, PrevContainer.UtocPath);

			// 复制文件
			if (FPaths::FileExists(*SourceUtocPath))
			{
				FString DestDir = FPaths::GetPath(DestUtocPath);
				IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
				if (!PlatformFile.DirectoryExists(*DestDir))
				{
					PlatformFile.CreateDirectoryTree(*DestDir);
				}

				if (PlatformFile.CopyFile(*DestUtocPath, *SourceUtocPath))
				{
					UE_LOG(LogHotUpdateEditor, Log, TEXT("  复制容器: %s -> %s"), *SourceUtocPath, *DestUtocPath);
					CopiedCount++;

					// 同时复制 ucas 文件（如果存在且有 ucas 路径）
					if (!PrevContainer.UcasPath.IsEmpty())
					{
						FString SourceUcasPath = FPaths::ChangeExtension(SourceUtocPath, TEXT("ucas"));
						FString DestUcasPath = FPaths::ChangeExtension(DestUtocPath, TEXT("ucas"));
						if (FPaths::FileExists(*SourceUcasPath))
						{
							PlatformFile.CopyFile(*DestUcasPath, *SourceUcasPath);
						}
					}
				}
				else
				{
					UE_LOG(LogHotUpdateEditor, Warning, TEXT("  复制容器失败: %s"), *SourceUtocPath);
				}
			}
			else
			{
				UE_LOG(LogHotUpdateEditor, Warning, TEXT("  源容器文件不存在: %s"), *SourceUtocPath);
			}

			UpdateProgress(TEXT("复制之前的容器文件"), PrevContainer.ContainerName, CopiedCount, ChainPatchContainers.Num());
		}

		UE_LOG(LogHotUpdateEditor, Log, TEXT("链式 Patch: 已复制 %d 个之前版本的容器到当前目录"), CopiedCount);
	}

	// 6.6 从基础版本目录加载文件 hash（用于 filemanifest，不填充 BaseContainers）
		TArray<FHotUpdateContainerInfo> BaseContainers;
		TMap<FString, FString> BaseContainerFilesHash;
		TMap<FString, int64> BaseContainerFilesSize;

		if (Config.bIncludeBaseContainers && !Config.BaseContainerDirectory.Path.IsEmpty())
		{
			FString BaseContainerDir = Config.BaseContainerDirectory.Path;
			FPaths::NormalizeDirectoryName(BaseContainerDir);

			UE_LOG(LogHotUpdateEditor, Log, TEXT("全量热更新: 从目录加载基础版本容器文件: %s"), *BaseContainerDir);

			TArray<FHotUpdateContainerInfo> ScannedContainers;
			if (LoadBaseContainers(BaseContainerDir, ScannedContainers, BaseContainerFilesHash, BaseContainerFilesSize))
			{
				UE_LOG(LogHotUpdateEditor, Log, TEXT("全量热更新: 扫描了 %d 个基础版本容器文件, %d 个文件 hash"),
					ScannedContainers.Num(), BaseContainerFilesHash.Num());
			}
			else
			{
				UE_LOG(LogHotUpdateEditor, Warning, TEXT("全量热更新: 无法加载基础版本容器目录: %s"), *BaseContainerDir);
			}
		}

		// 6.7 从基础版本 Manifest 解析容器信息（写入 manifest 供客户端下载）
	{
		FString BaseManifestJson;
		if (FFileHelper::LoadFileToString(BaseManifestJson, *Config.BaseManifestPath.FilePath))
		{
			TSharedPtr<FJsonObject> BaseManifestObj;
			TSharedRef<TJsonReader<>> BaseReader = TJsonReaderFactory<>::Create(BaseManifestJson);
			if (FJsonSerializer::Deserialize(BaseReader, BaseManifestObj) && BaseManifestObj.IsValid())
			{
				// 兼容 chunks 和 containers 两种字段名
				const TArray<TSharedPtr<FJsonValue>>* ContainersArray = nullptr;
				bool bFound = BaseManifestObj->TryGetArrayField(TEXT("chunks"), ContainersArray);
				if (!bFound)
				{
					bFound = BaseManifestObj->TryGetArrayField(TEXT("containers"), ContainersArray);
				}

				if (bFound && ContainersArray)
				{
					for (const TSharedPtr<FJsonValue>& ContainerValue : *ContainersArray)
					{
						TSharedPtr<FJsonObject> ContainerObj = ContainerValue->AsObject();
						if (!ContainerObj.IsValid()) continue;

						// 只保留 patch 类型的容器（base 类型属于基础包，不在 patch manifest 中引用）
						FString ContainerType;
						if (ContainerObj->TryGetStringField(TEXT("containerType"), ContainerType) && !ContainerType.StartsWith(TEXT("patch")))
						{
							continue;
						}

						FHotUpdateContainerInfo Info;
						// 兼容 ChunkName 和 containerName
						if (ContainerObj->HasField(TEXT("ChunkName")))
						{
							Info.ContainerName = ContainerObj->GetStringField(TEXT("ChunkName"));
						}
						else
						{
							Info.ContainerName = ContainerObj->GetStringField(TEXT("containerName"));
						}
						Info.UtocPath = ContainerObj->GetStringField(TEXT("utocPath"));
						Info.UtocSize = (int64)ContainerObj->GetNumberField(TEXT("utocSize"));
						Info.UtocHash = ContainerObj->GetStringField(TEXT("utocHash"));

						if (ContainerObj->HasField(TEXT("ucasPath")))
						{
							Info.UcasPath = ContainerObj->GetStringField(TEXT("ucasPath"));
							Info.UcasSize = (int64)ContainerObj->GetNumberField(TEXT("ucasSize"));
							Info.UcasHash = ContainerObj->GetStringField(TEXT("ucasHash"));
						}

						Info.ContainerType = EHotUpdateContainerType::Patch;
						Info.ChunkId = (int32)ContainerObj->GetNumberField(TEXT("chunkId"));
						// 优先使用 manifest 中的 version 字段，否则使用 Config.BaseVersion
						if (ContainerObj->HasField(TEXT("version")))
						{
							Info.Version = ContainerObj->GetStringField(TEXT("version"));
						}
						else
						{
							Info.Version = ActualBaseVersion;
						}

						BaseContainers.Add(Info);
					}

					UE_LOG(LogHotUpdateEditor, Log, TEXT("从 Manifest 加载了 %d 个 patch 容器引用"), BaseContainers.Num());
				}
			}
		}

		// 如果有基础容器，标记包含基础容器并计算总下载大小
		if (BaseContainers.Num() > 0)
		{
			Result.bIncludesBaseContainers = true;
			Result.BaseContainers = BaseContainers;

			// 总下载大小 = Patch 包大小 + 基础容器大小
			Result.TotalDownloadSize = Result.PatchSize;
			for (const FHotUpdateContainerInfo& Container : BaseContainers)
			{
				Result.TotalDownloadSize += Container.UtocSize + Container.UcasSize;
			}
		}
	}

	// 7. 生成 Manifest
	UpdateProgress(TEXT("生成 Manifest"), TEXT(""), 0, 0);

		// 使用实际 BaseVersion 创建可变 Config 副本
		FHotUpdatePatchPackageConfig ManifestConfig = Config;
		ManifestConfig.BaseVersion = ActualBaseVersion;

	FString ManifestPath = FPaths::Combine(OutputDir, TEXT("manifest.json"));

	if (!GenerateManifest(ManifestPath, Result.PatchUtocPath, Result.PatchUcasPath, AssetsToPackage, ChangedAssetDiskPaths, BaseAssetHashes, BaseAssetSizes, DiffReport, ManifestConfig, ChainPatchContainers, PreviousPatchFilesHash, PreviousPatchFilesSize, PatchVersionChain, BaseContainers, BaseContainerFilesHash, BaseContainerFilesSize))
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("生成 Manifest 失败");
		bIsBuilding = false;
		return Result;
	}

	Result.ManifestFilePath = ManifestPath;

	// 8. 注册版本
	UpdateProgress(TEXT("注册版本"), TEXT(""), 0, 0);

	UHotUpdateVersionManager* VersionManager = NewObject<UHotUpdateVersionManager>();

	FHotUpdateEditorVersionInfo VersionInfo;
	VersionInfo.VersionString = Config.PatchVersion;
	VersionInfo.PackageKind = EHotUpdatePackageKind::Patch;
	VersionInfo.BaseVersion = Config.BaseVersion;
	VersionInfo.Platform = Config.Platform;
	VersionInfo.CreatedTime = FDateTime::Now();
	VersionInfo.ManifestPath = ManifestPath;
	VersionInfo.UtocPath = Result.PatchUtocPath;
	VersionInfo.AssetCount = AssetsToPackage.Num();
	VersionInfo.PackageSize = Result.bIncludesBaseContainers ? Result.TotalDownloadSize : Result.PatchSize;

	VersionManager->RegisterVersion(VersionInfo);

	// 9. 完成
	Result.bSuccess = true;

	bIsBuilding = false;

	UpdateProgress(TEXT("完成"), TEXT(""), 1, 1);

	UE_LOG(LogHotUpdateEditor, Log, TEXT("更新包构建成功: %s, 变更 %d 个资源, 大小 %lld 字节"),
		*OutputDir, ChangedAssets.Num(), Result.PatchSize);

	return Result;
}

void UHotUpdatePatchPackageBuilder::BuildPatchPackageAsync(const FHotUpdatePatchPackageConfig& Config)
{
	UE_LOG(LogHotUpdateEditor, Log, TEXT("BuildPatchPackageAsync 开始调用"));
	UE_LOG(LogHotUpdateEditor, Log, TEXT("  bIsBuilding: %s"), bIsBuilding ? TEXT("true") : TEXT("false"));
	UE_LOG(LogHotUpdateEditor, Log, TEXT("  BuildTask.IsValid(): %s"), BuildTask.IsValid() ? TEXT("true") : TEXT("false"));
	UE_LOG(LogHotUpdateEditor, Log, TEXT("  BuildTask.IsReady(): %s"), BuildTask.IsReady() ? TEXT("true") : TEXT("false"));

	// 检查是否有正在运行的构建任务
	// 如果 bIsBuilding 为 true 但 BuildTask 已经完成，说明之前的构建异常终止，需要重置状态
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

	UE_LOG(LogHotUpdateEditor, Log, TEXT("开始新的更新包构建任务，版本: %s，基础版本: %s"), *Config.PatchVersion, *Config.BaseVersion);

	bIsBuilding = true;
	bIsCancelled = false;

	// 在后台线程启动前，先在游戏线程完成需要访问 AssetRegistry 的操作
	// 因为 AssetRegistry->GetAssets() 必须在游戏线程执行
	FHotUpdatePatchPackageConfig ConfigForThread = Config;

	if (Config.PackageType == EHotUpdatePackageType::FromPackagingSettings)
	{
		UE_LOG(LogHotUpdateEditor, Log, TEXT("在游戏线程预收集打包设置中的资源..."));

		FHotUpdatePackagingSettingsResult SettingsResult = FHotUpdatePackagingSettingsHelper::ParsePackagingSettings(true);
		if (SettingsResult.Errors.Num() > 0)
		{
			UE_LOG(LogHotUpdateEditor, Error, TEXT("解析打包设置失败: %s"), *FString::Join(SettingsResult.Errors, TEXT("\n")));

			FHotUpdatePatchPackageResult Result;
			Result.bSuccess = false;
			Result.ErrorMessage = FString::Join(SettingsResult.Errors, TEXT("\n"));
			bIsBuilding = false;
			OnComplete.Broadcast(Result);
			return;
		}

		// 将收集到的资源路径转换为 Asset 类型配置
		ConfigForThread.PackageType = EHotUpdatePackageType::Asset;
		ConfigForThread.AssetPaths = SettingsResult.AssetPaths;

		UE_LOG(LogHotUpdateEditor, Log, TEXT("预收集完成，共 %d 个资源路径"), ConfigForThread.AssetPaths.Num());
	}

	TWeakObjectPtr<UHotUpdatePatchPackageBuilder> WeakThis(this);

		BuildTask = Async(EAsyncExecution::Thread, [WeakThis, ConfigForThread]()
		{
			UHotUpdatePatchPackageBuilder* Builder = WeakThis.Get();
			if (!Builder)
			{
				return;
			}

			// RAII 保护，确保异常情况下 bIsBuilding 会被重置
			struct FBuildGuard
			{
				UHotUpdatePatchPackageBuilder* Builder;
				FHotUpdatePatchPackageResult Result;
				bool bNormalCompletion = false;
				FBuildGuard(UHotUpdatePatchPackageBuilder* InBuilder) : Builder(InBuilder) {}
				~FBuildGuard()
				{
					if (Builder && Builder->bIsBuilding && !bNormalCompletion)
					{
						Builder->bIsBuilding = false;
						UE_LOG(LogHotUpdateEditor, Warning, TEXT("Patch构建异常终止，已重置构建状态"));

						// 安全地传递结果到 GameThread
						UHotUpdatePatchPackageBuilder* GuardBuilder = Builder;
						FHotUpdateDiffReport DiffReportCopy = Result.DiffReport;
						TArray<FHotUpdateContainerInfo> ChainPatchContainersCopy = Result.ChainPatchContainers;
						TArray<FString> PatchVersionChainCopy = Result.PatchVersionChain;
						AsyncTask(ENamedThreads::GameThread, [GuardBuilder,
							bSuccess = Result.bSuccess,
							OutputDirectory = Result.OutputDirectory,
							PatchUtocPath = Result.PatchUtocPath,
							PatchUcasPath = Result.PatchUcasPath,
							ManifestFilePath = Result.ManifestFilePath,
							PatchVersion = Result.PatchVersion,
							BaseVersion = Result.BaseVersion,
							DiffReport = MoveTemp(DiffReportCopy),
							ChangedAssetCount = Result.ChangedAssetCount,
							PatchSize = Result.PatchSize,
							bRequiresBasePackage = Result.bRequiresBasePackage,
							bIsChainPatch = Result.bIsChainPatch,
							ChainPatchContainers = MoveTemp(ChainPatchContainersCopy),
							PatchVersionChain = MoveTemp(PatchVersionChainCopy),
							ErrorMessage = Result.ErrorMessage]()
						{
							if (IsValid(GuardBuilder))
							{
								FHotUpdatePatchPackageResult GameThreadResult;
								GameThreadResult.bSuccess = bSuccess;
								GameThreadResult.OutputDirectory = OutputDirectory;
								GameThreadResult.PatchUtocPath = PatchUtocPath;
								GameThreadResult.PatchUcasPath = PatchUcasPath;
								GameThreadResult.ManifestFilePath = ManifestFilePath;
								GameThreadResult.PatchVersion = PatchVersion;
								GameThreadResult.BaseVersion = BaseVersion;
								GameThreadResult.DiffReport = DiffReport;
								GameThreadResult.ChangedAssetCount = ChangedAssetCount;
								GameThreadResult.PatchSize = PatchSize;
								GameThreadResult.bRequiresBasePackage = bRequiresBasePackage;
								GameThreadResult.bIsChainPatch = bIsChainPatch;
								GameThreadResult.ChainPatchContainers = ChainPatchContainers;
								GameThreadResult.PatchVersionChain = PatchVersionChain;
								GameThreadResult.ErrorMessage = ErrorMessage;

								GuardBuilder->OnComplete.Broadcast(GameThreadResult);
							}
						});
					}
				}
			};
			FBuildGuard Guard(Builder);

			FHotUpdatePatchPackageResult Result = Builder->BuildPatchPackage(ConfigForThread);
			Guard.Result = Result;
			Guard.bNormalCompletion = true;

			// 正常完成时，Guard 析构不会执行额外操作（因为 bIsBuilding 已被 BuildPatchPackage 设置为 false）
			// 安全地传递结果到 GameThread
			FHotUpdateDiffReport DiffReportCopy = Result.DiffReport;
			TArray<FHotUpdateContainerInfo> ChainPatchContainersCopy = Result.ChainPatchContainers;
			TArray<FString> PatchVersionChainCopy = Result.PatchVersionChain;
			AsyncTask(ENamedThreads::GameThread, [WeakThis,
				bSuccess = Result.bSuccess,
				OutputDirectory = Result.OutputDirectory,
				PatchUtocPath = Result.PatchUtocPath,
				PatchUcasPath = Result.PatchUcasPath,
				ManifestFilePath = Result.ManifestFilePath,
				PatchVersion = Result.PatchVersion,
				BaseVersion = Result.BaseVersion,
				DiffReport = MoveTemp(DiffReportCopy),
				ChangedAssetCount = Result.ChangedAssetCount,
				PatchSize = Result.PatchSize,
				bRequiresBasePackage = Result.bRequiresBasePackage,
				bIsChainPatch = Result.bIsChainPatch,
				ChainPatchContainers = MoveTemp(ChainPatchContainersCopy),
				PatchVersionChain = MoveTemp(PatchVersionChainCopy),
				ErrorMessage = Result.ErrorMessage]()
			{
				UHotUpdatePatchPackageBuilder* PinnedBuilder = WeakThis.Get();
				if (IsValid(PinnedBuilder))
				{
					FHotUpdatePatchPackageResult GameThreadResult;
					GameThreadResult.bSuccess = bSuccess;
					GameThreadResult.OutputDirectory = OutputDirectory;
					GameThreadResult.PatchUtocPath = PatchUtocPath;
					GameThreadResult.PatchUcasPath = PatchUcasPath;
					GameThreadResult.ManifestFilePath = ManifestFilePath;
					GameThreadResult.PatchVersion = PatchVersion;
					GameThreadResult.BaseVersion = BaseVersion;
					GameThreadResult.DiffReport = DiffReport;
					GameThreadResult.ChangedAssetCount = ChangedAssetCount;
					GameThreadResult.PatchSize = PatchSize;
					GameThreadResult.bRequiresBasePackage = bRequiresBasePackage;
					GameThreadResult.bIsChainPatch = bIsChainPatch;
					GameThreadResult.ChainPatchContainers = ChainPatchContainers;
					GameThreadResult.PatchVersionChain = PatchVersionChain;
					GameThreadResult.ErrorMessage = ErrorMessage;

					PinnedBuilder->OnComplete.Broadcast(GameThreadResult);
				}
			});
		});
}

FHotUpdateDiffReport UHotUpdatePatchPackageBuilder::PreviewDiff(const FHotUpdatePatchPackageConfig& Config)
{
	FHotUpdateDiffReport Report;

	// 加载基础版本
	TMap<FString, FString> BaseAssetHashes;
	TMap<FString, int64> BaseAssetSizes;

	if (!LoadBaseManifest(Config.BaseManifestPath.FilePath, BaseAssetHashes, BaseAssetSizes))
	{
		return Report;
	}

	// 收集当前资源
	TArray<FString> CurrentAssetPaths;
	TMap<FString, FString> CurrentAssetDiskPaths;
	FString ErrorMessage;

	if (!CollectAssets(Config, CurrentAssetPaths, CurrentAssetDiskPaths, ErrorMessage))
	{
		return Report;
	}

	// 计算当前 Hash
	TMap<FString, FString> CurrentAssetHashes;
	for (const FString& AssetPath : CurrentAssetPaths)
	{
		const FString* DiskPath = CurrentAssetDiskPaths.Find(AssetPath);
		if (DiskPath && FPaths::FileExists(**DiskPath))
		{
			CurrentAssetHashes.Add(AssetPath, UHotUpdateFileUtils::CalculateFileHash(*DiskPath));
		}
	}

	// 计算差异
	TArray<FString> ChangedAssets;
	ComputeDiff(CurrentAssetPaths, CurrentAssetHashes, BaseAssetHashes, ChangedAssets, Report);

	Report.BaseVersion = Config.BaseVersion;
	Report.TargetVersion = Config.PatchVersion;

	return Report;
}

void UHotUpdatePatchPackageBuilder::CancelBuild()
{
	bIsCancelled = true;
}

FHotUpdatePackageProgress UHotUpdatePatchPackageBuilder::GetCurrentProgress() const
{
	FScopeLock Lock(&ProgressCriticalSection);
	return CurrentProgress;
}

bool UHotUpdatePatchPackageBuilder::ValidateConfig(const FHotUpdatePatchPackageConfig& Config, FString& OutErrorMessage)
{
	if (Config.PatchVersion.IsEmpty())
	{
		OutErrorMessage = TEXT("更新包版本号不能为空");
		return false;
	}

	if (Config.BaseVersion.IsEmpty())
	{
		OutErrorMessage = TEXT("基础版本号不能为空");
		return false;
	}

	if (Config.BaseManifestPath.FilePath.IsEmpty())
	{
		OutErrorMessage = TEXT("基础版本 Manifest 路径不能为空");
		return false;
	}

	if (!FPaths::FileExists(Config.BaseManifestPath.FilePath))
	{
		OutErrorMessage = FString::Printf(TEXT("基础版本 Manifest 文件不存在: %s"), *Config.BaseManifestPath.FilePath);
		return false;
	}

	return true;
}

bool UHotUpdatePatchPackageBuilder::CollectAssets(
	const FHotUpdatePatchPackageConfig& Config,
	TArray<FString>& OutAssetPaths,
	TMap<FString, FString>& OutAssetDiskPaths,
	FString& OutErrorMessage)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry* AssetRegistry = &AssetRegistryModule.Get();

	TArray<FString> AllAssetPaths;

	switch (Config.PackageType)
	{
	case EHotUpdatePackageType::Asset:
		AllAssetPaths = Config.AssetPaths;
		break;

	case EHotUpdatePackageType::Directory:
		for (const FString& DirPath : Config.AssetPaths)
		{
			FARFilter Filter;
			Filter.PackagePaths.Add(FName(*DirPath));
			Filter.bRecursivePaths = true;

			TArray<FAssetData> AssetDataList;
			AssetRegistry->GetAssets(Filter, AssetDataList);

			for (const FAssetData& AssetData : AssetDataList)
			{
				AllAssetPaths.Add(AssetData.PackageName.ToString());
			}
		}
		break;

	case EHotUpdatePackageType::FromPackagingSettings:
		{
			FHotUpdatePackagingSettingsResult SettingsResult = FHotUpdatePackagingSettingsHelper::ParsePackagingSettings(true);
			if (SettingsResult.Errors.Num() > 0)
			{
				OutErrorMessage = FString::Join(SettingsResult.Errors, TEXT("\n"));
				return false;
			}
			AllAssetPaths = SettingsResult.AssetPaths;
		}
		break;
	}

	// 收集依赖
	if (Config.bIncludeDependencies)
	{
		TSet<FString> UniquePaths(AllAssetPaths);

		for (const FString& AssetPath : AllAssetPaths)
		{
			TArray<FName> Dependencies;
			if (AssetRegistry->GetDependencies(FName(*AssetPath), Dependencies))
			{
				for (const FName& Dep : Dependencies)
				{
					FString DepStr = Dep.ToString();
					// 只收集项目资源依赖（/Game/ 开头）
					// 不包含引擎资源（/Engine/）和插件资源（如 /NNE/）
					if (DepStr.StartsWith(TEXT("/Game/")))
					{
						UniquePaths.Add(DepStr);
					}
				}
			}
		}

		AllAssetPaths = UniquePaths.Array();
	}

	// 过滤非项目资源：只保留 /Game/ 开头的资源
	// Patch 包不应该包含引擎或插件资源，这些资源在基础包中已存在
	int32 FilteredCount = 0;
	AllAssetPaths.RemoveAll([&FilteredCount](const FString& Path)
	{
		if (!Path.StartsWith(TEXT("/Game/")))
		{
			FilteredCount++;
			UE_LOG(LogHotUpdateEditor, Verbose, TEXT("过滤非项目资源: %s"), *Path);
			return true;
		}
		return false;
	});

	if (FilteredCount > 0)
	{
		UE_LOG(LogHotUpdateEditor, Log, TEXT("过滤掉 %d 个非项目资源（引擎/插件资源）"), FilteredCount);
	}

	// 过滤 UE5 OFPA 外部路径：__ExternalActors__ 和 __ExternalObjects__
	// 这些是 Level 的子对象，烘焙时已合入 .umap 文件，不应作为独立资源打包
	int32 ExternalFilteredCount = 0;
	AllAssetPaths.RemoveAll([&ExternalFilteredCount](const FString& Path)
	{
		if (HotUpdateUtils::IsExternalActorOrObjectPath(Path))
		{
			ExternalFilteredCount++;
			UE_LOG(LogHotUpdateEditor, Verbose, TEXT("过滤 UE5 外部 Actor/Object: %s"), *Path);
			return true;
		}
		return false;
	});

	if (ExternalFilteredCount > 0)
	{
		UE_LOG(LogHotUpdateEditor, Log, TEXT("过滤掉 %d 个 UE5 外部 Actor/Object 资源"), ExternalFilteredCount);
	}

	// 获取磁盘路径
	// 使用 Cooked 目录解析磁盘路径（运行时需要 cooked 格式的 .uasset + .uexp）
	FString CookedPlatformDir = FPaths::ProjectSavedDir() / TEXT("Cooked") / HotUpdateUtils::GetPlatformString(Config.Platform);
	for (const FString& AssetPath : AllAssetPaths)
	{
		FString DiskPath = GetAssetDiskPath(AssetPath, CookedPlatformDir);
		if (!DiskPath.IsEmpty() && FPaths::FileExists(*DiskPath))
		{
			OutAssetPaths.Add(AssetPath);
			OutAssetDiskPaths.Add(AssetPath, DiskPath);
		}
		else
		{
			UE_LOG(LogHotUpdateEditor, Verbose, TEXT("跳过无 cooked 文件的资源: %s"), *AssetPath);
		}
	}

	return true;
}

bool UHotUpdatePatchPackageBuilder::LoadBaseManifest(
	const FString& ManifestPath,
	TMap<FString, FString>& OutAssetHashes,
	TMap<FString, int64>& OutAssetSizes)
{
	// 优先读取 filemanifest.json（包含 files 信息）
	FString FileManifestPath = ManifestPath;
	if (FileManifestPath.EndsWith(TEXT(".manifest.json")))
	{
		FileManifestPath = FileManifestPath.Replace(TEXT(".manifest.json"), TEXT(".filemanifest.json"));
	}
	else if (FileManifestPath.EndsWith(TEXT("manifest.json")))
	{
		// 处理 "manifest.json" 不带前导点的情况（如从 Commandlet 传入的路径）
		FileManifestPath = FileManifestPath.Replace(TEXT("manifest.json"), TEXT("filemanifest.json"));
	}

	FString JsonString;
	bool bLoaded = false;

	// 先尝试读取 filemanifest.json
	if (FPaths::FileExists(FileManifestPath))
	{
		bLoaded = FFileHelper::LoadFileToString(JsonString, *FileManifestPath);
		if (bLoaded)
		{
			UE_LOG(LogHotUpdateEditor, Log, TEXT("加载 fileManifest: %s"), *FileManifestPath);
		}
	}

	// 如果 filemanifest.json 不存在，尝试读取原始 manifest.json
	if (!bLoaded)
	{
		bLoaded = FFileHelper::LoadFileToString(JsonString, *ManifestPath);
		if (bLoaded)
		{
			UE_LOG(LogHotUpdateEditor, Log, TEXT("加载 manifest (旧格式): %s"), *ManifestPath);
		}
	}

	if (!bLoaded)
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("无法读取 Manifest: %s 或 %s"), *ManifestPath, *FileManifestPath);
		return false;
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("无法解析 Manifest JSON: %s"), *ManifestPath);
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* FilesArray;
	if (JsonObject->TryGetArrayField(TEXT("files"), FilesArray))
	{
		for (const TSharedPtr<FJsonValue>& FileValue : *FilesArray)
		{
			TSharedPtr<FJsonObject> FileObj = FileValue->AsObject();
			if (!FileObj.IsValid()) continue;

			FString Path = FileObj->GetStringField(TEXT("filePath"));
			FString Hash = FileObj->GetStringField(TEXT("fileHash"));
			int64 Size = (int64)FileObj->GetNumberField(TEXT("fileSize"));

			OutAssetHashes.Add(Path, Hash);
			OutAssetSizes.Add(Path, Size);
		}
	}

	return OutAssetHashes.Num() > 0;
}

bool UHotUpdatePatchPackageBuilder::ComputeDiff(
	const TArray<FString>& CurrentAssets,
	const TMap<FString, FString>& CurrentHashes,
	const TMap<FString, FString>& BaseHashes,
	TArray<FString>& OutChangedAssets,
	FHotUpdateDiffReport& OutReport)
{
	// 收集所有路径
	TSet<FString> AllPaths;
	for (const auto& Pair : BaseHashes) AllPaths.Add(Pair.Key);
	for (const FString& Path : CurrentAssets) AllPaths.Add(Path);

	for (const FString& Path : AllPaths)
	{
		bool bInBase = BaseHashes.Contains(Path);
		bool bInCurrent = CurrentHashes.Contains(Path);

		FHotUpdateAssetDiff Diff;
		Diff.AssetPath = Path;
		Diff.AssetType = FPaths::GetExtension(Path);

		if (!bInBase && bInCurrent)
		{
			// 新增资源
			Diff.ChangeType = EHotUpdateFileChangeType::Added;
			Diff.NewHash = CurrentHashes[Path];
			Diff.ChangeDescription = TEXT("新增资源");
			OutReport.AddedAssets.Add(Diff);
			OutChangedAssets.Add(Path);
		}
		else if (bInBase && !bInCurrent)
		{
			// 删除资源
			Diff.ChangeType = EHotUpdateFileChangeType::Deleted;
			Diff.OldHash = BaseHashes[Path];
			Diff.ChangeDescription = TEXT("删除资源");
			OutReport.DeletedAssets.Add(Diff);
		}
		else if (bInBase && bInCurrent)
		{
			if (BaseHashes[Path] != CurrentHashes[Path])
			{
				// 修改资源
				Diff.ChangeType = EHotUpdateFileChangeType::Modified;
				Diff.OldHash = BaseHashes[Path];
				Diff.NewHash = CurrentHashes[Path];
				Diff.ChangeDescription = TEXT("修改资源");
				OutReport.ModifiedAssets.Add(Diff);
				OutChangedAssets.Add(Path);
			}
			else
			{
				// 未变更
				Diff.ChangeType = EHotUpdateFileChangeType::Unchanged;
				Diff.OldHash = BaseHashes[Path];
				Diff.NewHash = CurrentHashes[Path];
				OutReport.UnchangedAssets.Add(Diff);
			}
		}
	}

	return true;
}

bool UHotUpdatePatchPackageBuilder::GenerateManifest(
	const FString& ManifestPath,
	const FString& PatchUtocPath,
	const FString& PatchUcasPath,
	const TArray<FString>& ChangedAssetPaths,
	const TMap<FString, FString>& ChangedAssetDiskPaths,
	const TMap<FString, FString>& BaseAssetHashes,
	const TMap<FString, int64>& BaseAssetSizes,
	const FHotUpdateDiffReport& DiffReport,
	const FHotUpdatePatchPackageConfig& Config,
	const TArray<FHotUpdateContainerInfo>& ChainPatchContainers,
	const TMap<FString, FString>& PreviousPatchFilesHash,
	const TMap<FString, int64>& PreviousPatchFilesSize,
	const TArray<FString>& PatchVersionChain,
	const TArray<FHotUpdateContainerInfo>& BaseContainers,
	const TMap<FString, FString>& BaseContainerFilesHash,
	const TMap<FString, int64>& BaseContainerFilesSize)
{
	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);

	// Manifest 版本
	RootObject->SetNumberField(TEXT("manifestVersion"), 4); // 升级版本号，支持链式 Patch

	// 包类型
	RootObject->SetNumberField(TEXT("packageKind"), static_cast<int32>(EHotUpdatePackageKind::Patch));

	// 版本信息
	TSharedPtr<FJsonObject> VersionInfo = MakeShareable(new FJsonObject);

	TArray<FString> VersionParts;
	Config.PatchVersion.ParseIntoArray(VersionParts, TEXT("."));

	int32 Major = VersionParts.Num() > 0 ? FCString::Atoi(*VersionParts[0]) : 0;
	int32 Minor = VersionParts.Num() > 1 ? FCString::Atoi(*VersionParts[1]) : 0;
	int32 Patch = VersionParts.Num() > 2 ? FCString::Atoi(*VersionParts[2]) : 0;
	int32 Build = VersionParts.Num() > 3 ? FCString::Atoi(*VersionParts[3]) : 0;

	VersionInfo->SetStringField(TEXT("version"), Config.PatchVersion);
	VersionInfo->SetStringField(TEXT("platform"), HotUpdateUtils::GetPlatformString(Config.Platform));
	VersionInfo->SetNumberField(TEXT("timestamp"), FDateTime::Now().ToUnixTimestamp());

	RootObject->SetObjectField(TEXT("version"), VersionInfo);

	// 基础版本
	RootObject->SetStringField(TEXT("baseVersion"), Config.BaseVersion);

	// Patch 版本链（链式模式）
	if (Config.bEnableChainPatch && PatchVersionChain.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> PatchChainArray;
		for (const FString& PrevVersion : PatchVersionChain)
		{
			PatchChainArray.Add(MakeShareable(new FJsonValueString(PrevVersion)));
		}
		// 添加当前版本
		PatchChainArray.Add(MakeShareable(new FJsonValueString(Config.PatchVersion)));
		RootObject->SetArrayField(TEXT("patchChain"), PatchChainArray);
	}

	// 差异摘要
	TSharedPtr<FJsonObject> DiffSummary = MakeShareable(new FJsonObject);
	DiffSummary->SetNumberField(TEXT("addedCount"), DiffReport.AddedAssets.Num());
	DiffSummary->SetNumberField(TEXT("modifiedCount"), DiffReport.ModifiedAssets.Num());
	DiffSummary->SetNumberField(TEXT("deletedCount"), DiffReport.DeletedAssets.Num());
	DiffSummary->SetNumberField(TEXT("unchangedCount"), DiffReport.UnchangedAssets.Num());
	RootObject->SetObjectField(TEXT("diffSummary"), DiffSummary);

	// 全量热更新标志
	if (BaseContainers.Num() > 0)
	{
		RootObject->SetBoolField(TEXT("bIncludesBaseContainers"), true);
		RootObject->SetBoolField(TEXT("bRequiresBasePackage"), false);

		// 计算总下载大小
		int64 TotalDownloadSize = 0;
		for (const FHotUpdateContainerInfo& Container : BaseContainers)
		{
			TotalDownloadSize += Container.UtocSize + Container.UcasSize;
		}
		// 加上当前 patch 容器大小
		if (!PatchUtocPath.IsEmpty() && FPaths::FileExists(*PatchUtocPath))
		{
			TotalDownloadSize += IFileManager::Get().FileSize(*PatchUtocPath);
		}
		RootObject->SetNumberField(TEXT("totalDownloadSize"), TotalDownloadSize);
	}
	else
	{
		RootObject->SetBoolField(TEXT("bIncludesBaseContainers"), false);
		RootObject->SetBoolField(TEXT("bRequiresBasePackage"), true);
	}

	// 容器文件列表
	TArray<TSharedPtr<FJsonValue>> ContainersArray;

	// 1. 先添加基础版本容器（全量热更新模式）
	for (const FHotUpdateContainerInfo& BaseContainer : BaseContainers)
	{
		TSharedPtr<FJsonObject> BaseContainerObj = MakeShareable(new FJsonObject);
		BaseContainerObj->SetStringField(TEXT("containerName"), BaseContainer.ContainerName);
		BaseContainerObj->SetStringField(TEXT("utocPath"), BaseContainer.UtocPath);
		BaseContainerObj->SetNumberField(TEXT("utocSize"), BaseContainer.UtocSize);
		BaseContainerObj->SetStringField(TEXT("utocHash"), BaseContainer.UtocHash);

		if (!BaseContainer.UcasPath.IsEmpty())
		{
			BaseContainerObj->SetStringField(TEXT("ucasPath"), BaseContainer.UcasPath);
			BaseContainerObj->SetNumberField(TEXT("ucasSize"), BaseContainer.UcasSize);
			BaseContainerObj->SetStringField(TEXT("ucasHash"), BaseContainer.UcasHash);
			BaseContainerObj->SetStringField(TEXT("containerType"), TEXT("patch"));
		}
		else
		{
			BaseContainerObj->SetStringField(TEXT("ucasPath"), TEXT(""));
			BaseContainerObj->SetNumberField(TEXT("ucasSize"), 0);
			BaseContainerObj->SetStringField(TEXT("ucasHash"), TEXT(""));
			BaseContainerObj->SetStringField(TEXT("containerType"), TEXT("patch_pak"));
		}

		BaseContainerObj->SetNumberField(TEXT("chunkId"), BaseContainer.ChunkId);
		BaseContainerObj->SetStringField(TEXT("version"), BaseContainer.Version);
		ContainersArray.Add(MakeShareable(new FJsonValueObject(BaseContainerObj)));
	}

	// 2. 添加之前的 Patch 容器（链式模式）
	for (const FHotUpdateContainerInfo& PrevContainer : ChainPatchContainers)
	{
		TSharedPtr<FJsonObject> PrevContainerObj = MakeShareable(new FJsonObject);
		PrevContainerObj->SetStringField(TEXT("containerName"), PrevContainer.ContainerName);
		PrevContainerObj->SetStringField(TEXT("utocPath"), PrevContainer.UtocPath);
		PrevContainerObj->SetNumberField(TEXT("utocSize"), PrevContainer.UtocSize);
		PrevContainerObj->SetStringField(TEXT("utocHash"), PrevContainer.UtocHash);

		if (!PrevContainer.UcasPath.IsEmpty())
		{
			PrevContainerObj->SetStringField(TEXT("ucasPath"), PrevContainer.UcasPath);
			PrevContainerObj->SetNumberField(TEXT("ucasSize"), PrevContainer.UcasSize);
			PrevContainerObj->SetStringField(TEXT("ucasHash"), PrevContainer.UcasHash);
			PrevContainerObj->SetStringField(TEXT("containerType"), TEXT("patch"));
		}
		else
		{
			PrevContainerObj->SetStringField(TEXT("ucasPath"), TEXT(""));
			PrevContainerObj->SetNumberField(TEXT("ucasSize"), 0);
			PrevContainerObj->SetStringField(TEXT("ucasHash"), TEXT(""));
			PrevContainerObj->SetStringField(TEXT("containerType"), TEXT("patch_embedded"));
		}

		PrevContainerObj->SetNumberField(TEXT("chunkId"), PrevContainer.ChunkId);
			PrevContainerObj->SetStringField(TEXT("version"), PrevContainer.Version);
		ContainersArray.Add(MakeShareable(new FJsonValueObject(PrevContainerObj)));
	}

	// 3. 添加当前 Patch 容器
	// 计算当前 Patch 的 ChunkId（基于基础容器和链式容器数量）
	int32 CurrentChunkId = 10000 + BaseContainers.Num() * 100 + ChainPatchContainers.Num() * 100;

	if (!PatchUtocPath.IsEmpty() && FPaths::FileExists(*PatchUtocPath))
	{
		TSharedPtr<FJsonObject> PatchContainerObj = MakeShareable(new FJsonObject);

			FString ContainerName = FString::Printf(TEXT("Patch_%s_P"), *Config.PatchVersion);
		PatchContainerObj->SetStringField(TEXT("containerName"), ContainerName);

		// .utoc 文件信息
		FString UtocFileName = FPaths::GetCleanFilename(PatchUtocPath);
		PatchContainerObj->SetStringField(TEXT("utocPath"), TEXT("Paks/") + UtocFileName);
		PatchContainerObj->SetNumberField(TEXT("utocSize"), IFileManager::Get().FileSize(*PatchUtocPath));
		PatchContainerObj->SetStringField(TEXT("utocHash"), UHotUpdateFileUtils::CalculateFileHash(PatchUtocPath));

		// .ucas 文件信息（可选）
		if (!PatchUcasPath.IsEmpty() && FPaths::FileExists(*PatchUcasPath))
		{
			FString UcasFileName = FPaths::GetCleanFilename(PatchUcasPath);
			PatchContainerObj->SetStringField(TEXT("ucasPath"), TEXT("Paks/") + UcasFileName);
			PatchContainerObj->SetNumberField(TEXT("ucasSize"), IFileManager::Get().FileSize(*PatchUcasPath));
			PatchContainerObj->SetStringField(TEXT("ucasHash"), UHotUpdateFileUtils::CalculateFileHash(PatchUcasPath));
			PatchContainerObj->SetStringField(TEXT("containerType"), TEXT("patch"));
		}
		else if (PatchUtocPath.EndsWith(TEXT(".pak")))
		{
			// 传统 .pak 格式
			PatchContainerObj->SetStringField(TEXT("ucasPath"), TEXT(""));
			PatchContainerObj->SetNumberField(TEXT("ucasSize"), 0);
			PatchContainerObj->SetStringField(TEXT("ucasHash"), TEXT(""));
			PatchContainerObj->SetStringField(TEXT("containerType"), TEXT("patch_pak"));
		}
		else
		{
			// 单文件格式，数据嵌入在 utoc 中
			PatchContainerObj->SetStringField(TEXT("ucasPath"), TEXT(""));
			PatchContainerObj->SetNumberField(TEXT("ucasSize"), 0);
			PatchContainerObj->SetStringField(TEXT("ucasHash"), TEXT(""));
			PatchContainerObj->SetStringField(TEXT("containerType"), TEXT("patch_embedded"));
		}

		PatchContainerObj->SetNumberField(TEXT("chunkId"), CurrentChunkId);
			PatchContainerObj->SetStringField(TEXT("version"), Config.PatchVersion);

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
	FString FileManifestPath = FPaths::Combine(FPaths::GetPath(ManifestPath),
		TEXT("filemanifest.json"));

	TSharedPtr<FJsonObject> FileManifestObj = MakeShareable(new FJsonObject);
	FileManifestObj->SetNumberField(TEXT("manifestVersion"), 4);
	FileManifestObj->SetNumberField(TEXT("packageKind"), static_cast<int32>(EHotUpdatePackageKind::Patch));
	FileManifestObj->SetObjectField(TEXT("version"), VersionInfo);
	FileManifestObj->SetStringField(TEXT("baseVersion"), Config.BaseVersion);

	if (Config.bEnableChainPatch && PatchVersionChain.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> PatchChainArray;
		for (const FString& PrevVersion : PatchVersionChain)
		{
			PatchChainArray.Add(MakeShareable(new FJsonValueString(PrevVersion)));
		}
		PatchChainArray.Add(MakeShareable(new FJsonValueString(Config.PatchVersion)));
		FileManifestObj->SetArrayField(TEXT("patchChain"), PatchChainArray);
	}

	FileManifestObj->SetObjectField(TEXT("diffSummary"), DiffSummary);
	FileManifestObj->SetArrayField(TEXT("containers"), ContainersArray);

	// 收集所有资源路径（变更 + 未变更）
	TSet<FString> AllAssetPaths;
	for (const FString& Path : ChangedAssetPaths)
	{
		AllAssetPaths.Add(Path);
	}
	for (const FHotUpdateAssetDiff& UnchangedDiff : DiffReport.UnchangedAssets)
	{
		AllAssetPaths.Add(UnchangedDiff.AssetPath);
	}

	// 文件列表（仅用于编辑器端差异计算）
	TArray<TSharedPtr<FJsonValue>> FilesArray;

	for (const FString& AssetPath : AllAssetPaths)
	{
		TSharedPtr<FJsonObject> FileObj = MakeShareable(new FJsonObject);
		FileObj->SetStringField(TEXT("filePath"), ConvertAssetPathToFileName(AssetPath));

		// 检查文件来源（优先级：当前 patch > 之前 patch > 基础版本容器 > base manifest）
		bool bIsCurrentPatch = ChangedAssetDiskPaths.Contains(AssetPath);
		bool bIsPreviousPatch = PreviousPatchFilesHash.Contains(AssetPath);
		bool bIsBaseContainer = BaseContainerFilesHash.Contains(AssetPath);

		if (bIsCurrentPatch)
		{
			// 当前变更资源：从当前 patch 加载
			const FString* DiskPath = ChangedAssetDiskPaths.Find(AssetPath);
			if (DiskPath)
			{
				int64 FileSize = IFileManager::Get().FileSize(**DiskPath);
				FileObj->SetNumberField(TEXT("fileSize"), FileSize);
				FileObj->SetStringField(TEXT("fileHash"), UHotUpdateFileUtils::CalculateFileHash(*DiskPath));
				FileObj->SetNumberField(TEXT("chunkId"), CurrentChunkId);
				FileObj->SetStringField(TEXT("source"), TEXT("patch"));
			}
		}
		else if (bIsPreviousPatch)
		{
			// 之前 patch 中的文件：从之前的 patch 加载
			const FString* PrevHash = PreviousPatchFilesHash.Find(AssetPath);
			const int64* PrevSize = PreviousPatchFilesSize.Find(AssetPath);

			// 查找对应的 ChunkId（从容器列表中找）
			int32 PrevChunkId = 10000; // 默认值
			for (const FHotUpdateContainerInfo& PrevContainer : ChainPatchContainers)
			{
				PrevChunkId = PrevContainer.ChunkId;
			}

			if (PrevHash && PrevSize)
			{
				FileObj->SetNumberField(TEXT("fileSize"), *PrevSize);
				FileObj->SetStringField(TEXT("fileHash"), *PrevHash);
				FileObj->SetNumberField(TEXT("chunkId"), PrevChunkId);
				FileObj->SetStringField(TEXT("source"), TEXT("patch"));
			}
		}
		else if (bIsBaseContainer)
		{
			// 基础版本容器中的文件：从基础容器加载
			const FString* BaseContainerHash = BaseContainerFilesHash.Find(AssetPath);
			const int64* BaseContainerSize = BaseContainerFilesSize.Find(AssetPath);

			// 查找对应的 ChunkId（从基础容器列表中找）
			int32 BaseChunkId = 0;
			for (const FHotUpdateContainerInfo& BaseContainer : BaseContainers)
			{
				BaseChunkId = BaseContainer.ChunkId;
			}

			if (BaseContainerHash && BaseContainerSize)
			{
				FileObj->SetNumberField(TEXT("fileSize"), *BaseContainerSize);
				FileObj->SetStringField(TEXT("fileHash"), *BaseContainerHash);
				FileObj->SetNumberField(TEXT("chunkId"), BaseChunkId);
				FileObj->SetStringField(TEXT("source"), TEXT("base_container"));
			}
		}
		else
		{
			// 未变更资源：从基础包 manifest 加载
			const FString* BaseHash = BaseAssetHashes.Find(AssetPath);
			const int64* BaseSize = BaseAssetSizes.Find(AssetPath);
			if (BaseHash && BaseSize)
			{
				FileObj->SetNumberField(TEXT("fileSize"), *BaseSize);
				FileObj->SetStringField(TEXT("fileHash"), *BaseHash);
				FileObj->SetNumberField(TEXT("chunkId"), 0);
				FileObj->SetStringField(TEXT("source"), TEXT("base"));
			}
		}

		FileObj->SetNumberField(TEXT("priority"), 0);
		FileObj->SetBoolField(TEXT("isCompressed"), Config.IoStoreConfig.CompressionFormat != TEXT("None"));

		FilesArray.Add(MakeShareable(new FJsonValueObject(FileObj)));
	}

	FileManifestObj->SetArrayField(TEXT("files"), FilesArray);

	// 序列化 fileManifest
	FString FileManifestString;
	TSharedRef<TJsonWriter<>> FileWriter = TJsonWriterFactory<>::Create(&FileManifestString);
	FJsonSerializer::Serialize(FileManifestObj.ToSharedRef(), FileWriter);

	return FFileHelper::SaveStringToFile(FileManifestString, *FileManifestPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void UHotUpdatePatchPackageBuilder::UpdateProgress(
	const FString& Stage,
	const FString& CurrentFile,
	int32 ProcessedFiles,
	int32 TotalFiles)
{
	FHotUpdatePackageProgress ProgressCopy;
	{
		FScopeLock Lock(&ProgressCriticalSection);
		CurrentProgress.CurrentStage = Stage;
		CurrentProgress.CurrentFile = CurrentFile;
		CurrentProgress.ProcessedFiles = ProcessedFiles;
		CurrentProgress.TotalFiles = TotalFiles;
		CurrentProgress.bIsComplete = (ProcessedFiles >= TotalFiles && TotalFiles > 0);
		ProgressCopy = CurrentProgress;
	}

	// 在游戏线程中安全地广播委托
	TWeakObjectPtr<UHotUpdatePatchPackageBuilder> WeakThis(this);
	AsyncTask(ENamedThreads::GameThread, [WeakThis, ProgressCopy]()
	{
		UHotUpdatePatchPackageBuilder* PinnedBuilder = WeakThis.Get();
		if (IsValid(PinnedBuilder))
		{
			PinnedBuilder->OnProgress.Broadcast(ProgressCopy);
		}
	});
}

FString UHotUpdatePatchPackageBuilder::ConvertAssetPathToFileName(const FString& AssetPath)
{
	FString FileName = AssetPath;

	// 去掉前导斜杠（如 "/Game/Startup/umg_hotupdate" -> "Game/Startup/umg_hotupdate"）
	if (FileName.StartsWith(TEXT("/")))
	{
		FileName.RightChopInline(1);
	}

	// 使用实际磁盘文件来确定后缀
	FString DiskPath = GetAssetDiskPath(AssetPath, TEXT(""));
	if (!DiskPath.IsEmpty() && FPaths::FileExists(*DiskPath))
	{
		// 从磁盘路径获取实际扩展名
		FString Extension = FPaths::GetExtension(DiskPath);
		if (!Extension.IsEmpty())
		{
			FileName += TEXT(".") + Extension;
			return FileName;
		}
	}

	// 如果文件不存在（可能是新资产），使用默认的 .uasset 后缀
	FileName += TEXT(".uasset");
	return FileName;
}

FString UHotUpdatePatchPackageBuilder::GetAssetDiskPath(const FString& AssetPath, const FString& CookedPlatformDir)
{
	// 安全网：过滤 UE5 OFPA 外部路径
	// __ExternalActors__/__ExternalObjects__ 烘焙时已合入 .umap，不应作为独立资源打包
	// 不返回磁盘路径，避免 FPackageName fallback 从源码目录找到文件
	if (HotUpdateUtils::IsExternalActorOrObjectPath(AssetPath))
	{
		UE_LOG(LogHotUpdateEditor, Verbose, TEXT("跳过 UE5 外部 Actor/Object 路径: %s"), *AssetPath);
		return TEXT("");
	}

	// 从 /Game/ 提取相对路径
	FString RelativePath = AssetPath;
	if (RelativePath.StartsWith(TEXT("/Game/")))
	{
		RelativePath = RelativePath.Mid(6); // 去掉 "/Game/" 前缀
	}
	else if (RelativePath.StartsWith(TEXT("/")))
	{
		RelativePath = RelativePath.Mid(1); // 去掉前导 "/"
	}

	// 优先从 Cooked 目录解析（运行时需要 cooked 格式的 .uasset + .uexp）
	if (!CookedPlatformDir.IsEmpty())
	{
		FString CookedContentDir = FPaths::Combine(CookedPlatformDir, FApp::GetProjectName(), TEXT("Content"));

		// 先尝试 .umap（地图）
		FString CookedMapPath = FPaths::Combine(CookedContentDir, RelativePath + TEXT(".umap"));
		if (FPaths::FileExists(*CookedMapPath))
		{
			return CookedMapPath;
		}

		// 再尝试 .uasset（普通资源）
		FString CookedAssetPath = FPaths::Combine(CookedContentDir, RelativePath + TEXT(".uasset"));
		if (FPaths::FileExists(*CookedAssetPath))
		{
			return CookedAssetPath;
		}
	}

	// 回退：使用 FPackageName 解析到原始 Content 目录
	FString DiskPath = FPackageName::LongPackageNameToFilename(
		AssetPath, FPackageName::GetAssetPackageExtension());

	if (!FPaths::FileExists(*DiskPath))
	{
		FString MapDiskPath = FPackageName::LongPackageNameToFilename(
			AssetPath, FPackageName::GetMapPackageExtension());
		if (FPaths::FileExists(*MapDiskPath))
		{
			return MapDiskPath;
		}
	}

	return DiskPath;
}

bool UHotUpdatePatchPackageBuilder::LoadPreviousPatchManifest(
	const FString& ManifestPath,
	TArray<FHotUpdateContainerInfo>& OutContainers,
	TMap<FString, FString>& OutPatchFilesHash,
	TMap<FString, int64>& OutPatchFilesSize,
	FString& OutPatchVersion)
{
	// 优先读取 filemanifest.json（包含 files 信息）
	FString FileManifestPath = ManifestPath;
	if (FileManifestPath.EndsWith(TEXT(".manifest.json")))
	{
		FileManifestPath = FileManifestPath.Replace(TEXT(".manifest.json"), TEXT(".filemanifest.json"));
	}
	else if (FileManifestPath.EndsWith(TEXT("manifest.json")))
	{
		FileManifestPath = FileManifestPath.Replace(TEXT("manifest.json"), TEXT("filemanifest.json"));
	}

	FString JsonString;
	bool bLoaded = false;

	// 先尝试读取 filemanifest.json
	if (FPaths::FileExists(FileManifestPath))
	{
		bLoaded = FFileHelper::LoadFileToString(JsonString, *FileManifestPath);
		if (bLoaded)
		{
			UE_LOG(LogHotUpdateEditor, Log, TEXT("加载之前的 Patch fileManifest: %s"), *FileManifestPath);
		}
	}

	// 如果 filemanifest.json 不存在，尝试读取原始 manifest.json
	if (!bLoaded)
	{
		bLoaded = FFileHelper::LoadFileToString(JsonString, *ManifestPath);
		if (bLoaded)
		{
			UE_LOG(LogHotUpdateEditor, Log, TEXT("加载之前的 Patch manifest (旧格式): %s"), *ManifestPath);
		}
	}

	if (!bLoaded)
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("无法读取之前的 Patch Manifest: %s 或 %s"), *ManifestPath, *FileManifestPath);
		return false;
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("无法解析之前的 Patch Manifest JSON: %s"), *ManifestPath);
		return false;
	}

	// 获取版本信息（兼容新旧格式）
	const TSharedPtr<FJsonObject>* VersionObj;
	if (JsonObject->TryGetObjectField(TEXT("version"), VersionObj))
	{
		OutPatchVersion = VersionObj->Get()->GetStringField(TEXT("version"));
	}
	else if (JsonObject->TryGetObjectField(TEXT("versionInfo"), VersionObj))
	{
		OutPatchVersion = VersionObj->Get()->GetStringField(TEXT("versionString"));
	}

	// 获取容器列表
	const TArray<TSharedPtr<FJsonValue>>* ContainersArray;
	if (JsonObject->TryGetArrayField(TEXT("containers"), ContainersArray))
	{
		for (const TSharedPtr<FJsonValue>& ContainerValue : *ContainersArray)
		{
			TSharedPtr<FJsonObject> ContainerObj = ContainerValue->AsObject();
			if (!ContainerObj.IsValid()) continue;

			FHotUpdateContainerInfo ContainerInfo;
			ContainerInfo.ContainerName = ContainerObj->GetStringField(TEXT("containerName"));
			ContainerInfo.UtocPath = ContainerObj->GetStringField(TEXT("utocPath"));
			ContainerInfo.UtocSize = (int64)ContainerObj->GetNumberField(TEXT("utocSize"));
			ContainerInfo.UtocHash = ContainerObj->GetStringField(TEXT("utocHash"));

			// ucas 可选
			if (ContainerObj->HasField(TEXT("ucasPath")))
			{
				ContainerInfo.UcasPath = ContainerObj->GetStringField(TEXT("ucasPath"));
				ContainerInfo.UcasSize = (int64)ContainerObj->GetNumberField(TEXT("ucasSize"));
				ContainerInfo.UcasHash = ContainerObj->GetStringField(TEXT("ucasHash"));
			}

			// containerType
			FString ContainerTypeStr = ContainerObj->GetStringField(TEXT("containerType"));
			if (ContainerTypeStr.StartsWith(TEXT("patch")))
			{
				ContainerInfo.ContainerType = EHotUpdateContainerType::Patch;
			}
			else
			{
				ContainerInfo.ContainerType = EHotUpdateContainerType::Base;
			}

			ContainerInfo.ChunkId = (int32)ContainerObj->GetNumberField(TEXT("chunkId"));
			ContainerInfo.Version = OutPatchVersion;

			OutContainers.Add(ContainerInfo);
		}
	}

	// 获取文件列表（只提取 source=patch 的文件）
	const TArray<TSharedPtr<FJsonValue>>* FilesArray;
	if (JsonObject->TryGetArrayField(TEXT("files"), FilesArray))
	{
		for (const TSharedPtr<FJsonValue>& FileValue : *FilesArray)
		{
			TSharedPtr<FJsonObject> FileObj = FileValue->AsObject();
			if (!FileObj.IsValid()) continue;

			FString Source = FileObj->GetStringField(TEXT("source"));
			if (Source == TEXT("patch"))
			{
				FString Path = FileObj->GetStringField(TEXT("filePath"));
				FString Hash = FileObj->GetStringField(TEXT("fileHash"));
				int64 Size = (int64)FileObj->GetNumberField(TEXT("fileSize"));

				OutPatchFilesHash.Add(Path, Hash);
				OutPatchFilesSize.Add(Path, Size);
			}
		}
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("加载之前的 Patch Manifest 成功: %s, 版本: %s, 容器数: %d, Patch文件数: %d"),
		*ManifestPath, *OutPatchVersion, OutContainers.Num(), OutPatchFilesHash.Num());

	return OutContainers.Num() > 0;
}

bool UHotUpdatePatchPackageBuilder::LoadBaseContainers(
	const FString& ContainerDirectory,
	TArray<FHotUpdateContainerInfo>& OutContainers,
	TMap<FString, FString>& OutFilesHash,
	TMap<FString, int64>& OutFilesSize)
{
	IPlatformFile& PlatformFile = IPlatformFile::GetPlatformPhysical();

	if (!PlatformFile.DirectoryExists(*ContainerDirectory))
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("基础版本容器目录不存在: %s"), *ContainerDirectory);
		return false;
	}

	// 查找目录中的 manifest 文件
	TArray<FString> FoundManifests;
	PlatformFile.FindFilesRecursively(FoundManifests, *ContainerDirectory, TEXT(".manifest.json"));

	if (FoundManifests.Num() == 0)
	{
		// 尝试查找 filemanifest.json
		PlatformFile.FindFilesRecursively(FoundManifests, *ContainerDirectory, TEXT(".filemanifest.json"));
	}

	if (FoundManifests.Num() == 0)
	{
		UE_LOG(LogHotUpdateEditor, Warning, TEXT("未在目录中找到 manifest 文件: %s，尝试扫描容器文件"), *ContainerDirectory);

		// 直接扫描目录中的 .utoc 文件
		TArray<FString> FoundUtocFiles;
		PlatformFile.FindFilesRecursively(FoundUtocFiles, *ContainerDirectory, TEXT(".utoc"));

		for (const FString& UtocPath : FoundUtocFiles)
		{
			FHotUpdateContainerInfo ContainerInfo;
			ContainerInfo.ContainerName = FPaths::GetBaseFilename(UtocPath);
			ContainerInfo.UtocPath = FPaths::GetCleanFilename(UtocPath);
			ContainerInfo.UtocSize = IFileManager::Get().FileSize(*UtocPath);
			ContainerInfo.UtocHash = UHotUpdateFileUtils::CalculateFileHash(UtocPath);
			ContainerInfo.ContainerType = EHotUpdateContainerType::Base;
			ContainerInfo.ChunkId = OutContainers.Num(); // 使用索引作为 ChunkId

			// 查找对应的 .ucas 文件
			FString UcasPath = FPaths::ChangeExtension(UtocPath, TEXT("ucas"));
			if (FPaths::FileExists(*UcasPath))
			{
				ContainerInfo.UcasPath = FPaths::GetCleanFilename(UcasPath);
				ContainerInfo.UcasSize = IFileManager::Get().FileSize(*UcasPath);
				ContainerInfo.UcasHash = UHotUpdateFileUtils::CalculateFileHash(UcasPath);
			}

			OutContainers.Add(ContainerInfo);
			UE_LOG(LogHotUpdateEditor, Log, TEXT("扫描到容器: %s (utoc: %lld bytes)"),
				*ContainerInfo.ContainerName, ContainerInfo.UtocSize);
		}

		return OutContainers.Num() > 0;
	}

	// 加载找到的第一个 manifest 文件
	FString ManifestPath = FoundManifests[0];
	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *ManifestPath))
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("无法读取 Manifest 文件: %s"), *ManifestPath);
		return false;
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("无法解析 Manifest JSON: %s"), *ManifestPath);
		return false;
	}

	// 解析 chunks 或 containers 字段
	const TArray<TSharedPtr<FJsonValue>>* ChunksArray = nullptr;
	bool bHasChunks = JsonObject->TryGetArrayField(TEXT("chunks"), ChunksArray);

	const TArray<TSharedPtr<FJsonValue>>* ContainersArray = nullptr;
	bool bHasContainers = JsonObject->TryGetArrayField(TEXT("containers"), ContainersArray);

	const TArray<TSharedPtr<FJsonValue>>* TargetArray = bHasChunks ? ChunksArray : (bHasContainers ? ContainersArray : nullptr);

	if (TargetArray)
	{
		for (const TSharedPtr<FJsonValue>& ChunkValue : *TargetArray)
		{
			TSharedPtr<FJsonObject> ChunkObj = ChunkValue->AsObject();
			if (!ChunkObj.IsValid()) continue;

			FHotUpdateContainerInfo ContainerInfo;

			// 兼容两种格式
			if (ChunkObj->HasField(TEXT("ChunkName")))
			{
				ContainerInfo.ContainerName = ChunkObj->GetStringField(TEXT("ChunkName"));
			}
			else if (ChunkObj->HasField(TEXT("containerName")))
			{
				ContainerInfo.ContainerName = ChunkObj->GetStringField(TEXT("containerName"));
			}

			ContainerInfo.UtocPath = ChunkObj->GetStringField(TEXT("utocPath"));
			ContainerInfo.UtocSize = (int64)ChunkObj->GetNumberField(TEXT("utocSize"));
			ContainerInfo.UtocHash = ChunkObj->GetStringField(TEXT("utocHash"));

			if (ChunkObj->HasField(TEXT("ucasPath")))
			{
				ContainerInfo.UcasPath = ChunkObj->GetStringField(TEXT("ucasPath"));
				ContainerInfo.UcasSize = (int64)ChunkObj->GetNumberField(TEXT("ucasSize"));
				ContainerInfo.UcasHash = ChunkObj->GetStringField(TEXT("ucasHash"));
			}

			ContainerInfo.ContainerType = EHotUpdateContainerType::Base;

			if (ChunkObj->HasField(TEXT("chunkId")))
			{
				ContainerInfo.ChunkId = (int32)ChunkObj->GetNumberField(TEXT("chunkId"));
			}
			else
			{
				ContainerInfo.ChunkId = OutContainers.Num();
			}

			OutContainers.Add(ContainerInfo);
		}
	}

	// 解析 files 字段（如果存在）
	const TArray<TSharedPtr<FJsonValue>>* FilesArray;
	if (JsonObject->TryGetArrayField(TEXT("files"), FilesArray))
	{
		for (const TSharedPtr<FJsonValue>& FileValue : *FilesArray)
		{
			TSharedPtr<FJsonObject> FileObj = FileValue->AsObject();
			if (!FileObj.IsValid()) continue;

			FString Path = FileObj->GetStringField(TEXT("filePath"));
			FString Hash = FileObj->GetStringField(TEXT("fileHash"));
			int64 Size = (int64)FileObj->GetNumberField(TEXT("fileSize"));

			// 检查 source 字段，只添加 base 资源
			FString Source = TEXT("base");
			if (FileObj->HasField(TEXT("source")))
			{
				Source = FileObj->GetStringField(TEXT("source"));
			}

			if (Source == TEXT("base") || Source.IsEmpty())
			{
				OutFilesHash.Add(Path, Hash);
				OutFilesSize.Add(Path, Size);
			}
		}
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("加载基础版本容器成功: %s, 容器数: %d, 文件数: %d"),
		*ManifestPath, OutContainers.Num(), OutFilesHash.Num());

	return OutContainers.Num() > 0;
}

int32 UHotUpdatePatchPackageBuilder::CopyContainerFiles(
	const FString& SourceDir,
	const FString& DestDir,
	const TArray<FHotUpdateContainerInfo>& Containers)
{
	IPlatformFile& PlatformFile = IPlatformFile::GetPlatformPhysical();
	int32 CopiedCount = 0;

	// 确保目标目录存在
	if (!PlatformFile.DirectoryExists(*DestDir))
	{
		PlatformFile.CreateDirectoryTree(*DestDir);
	}

	for (const FHotUpdateContainerInfo& Container : Containers)
	{
		// 复制 .utoc 文件
		FString SourceUtocPath = FPaths::Combine(SourceDir, Container.UtocPath);
		FString DestUtocPath = FPaths::Combine(DestDir, Container.UtocPath);

		if (FPaths::FileExists(*SourceUtocPath))
		{
			if (PlatformFile.CopyFile(*DestUtocPath, *SourceUtocPath))
			{
				CopiedCount++;
				UE_LOG(LogHotUpdateEditor, Log, TEXT("复制容器: %s -> %s"), *SourceUtocPath, *DestUtocPath);

				// 复制对应的 .ucas 文件（如果存在）
				if (!Container.UcasPath.IsEmpty())
				{
					FString SourceUcasPath = FPaths::Combine(SourceDir, Container.UcasPath);
					FString DestUcasPath = FPaths::Combine(DestDir, Container.UcasPath);

					if (FPaths::FileExists(*SourceUcasPath))
					{
						PlatformFile.CopyFile(*DestUcasPath, *SourceUcasPath);
						UE_LOG(LogHotUpdateEditor, Log, TEXT("复制 ucas: %s"), *DestUcasPath);
					}
				}
			}
			else
			{
				UE_LOG(LogHotUpdateEditor, Warning, TEXT("复制容器失败: %s"), *SourceUtocPath);
			}
		}
		else
		{
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("源容器文件不存在: %s"), *SourceUtocPath);
		}
	}

	return CopiedCount;
}

bool UHotUpdatePatchPackageBuilder::CookAssets(const FHotUpdatePatchPackageConfig& Config)
{
	UE_LOG(LogHotUpdateEditor, Log, TEXT("开始 Cook 资源..."));

	// 使用子进程执行 Cook，避免在当前 Editor 进程中调用 CookCommandlet 导致的平台冲突
	FString EngineDir = FPaths::EngineDir();
	FString ExePath = FPaths::ConvertRelativePathToFull(EngineDir / TEXT("Binaries/Win64/UnrealEditor-Cmd.exe"));
	FString ProjectPath = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());

	FString CookPlatform = HotUpdateUtils::GetPlatformString(Config.Platform);
	FString Params = FString::Printf(TEXT("\"%s\" -run=cook -targetplatform=%s -NullRHI -unattended -NoSound"),
		*ProjectPath, *CookPlatform);

	UE_LOG(LogHotUpdateEditor, Log, TEXT("执行 Cook: %s %s"), *ExePath, *Params);

	int32 ReturnCode = -1;
	FProcHandle ProcHandle = FPlatformProcess::CreateProc(
		*ExePath, *Params, true, true, true, nullptr, 0, nullptr, nullptr);

	if (ProcHandle.IsValid())
	{
		FPlatformProcess::WaitForProc(ProcHandle);
		FPlatformProcess::GetProcReturnCode(ProcHandle, &ReturnCode);
		FPlatformProcess::CloseProc(ProcHandle);
	}
	else
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("无法启动 Cook 进程"));
		return false;
	}

	if (ReturnCode != 0)
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("Cook 失败，返回码: %d"), ReturnCode);
		return false;
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("Cook 完成"));
	return true;
}
