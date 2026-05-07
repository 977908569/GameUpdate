// Copyright czm. All Rights Reserved.

#include "Core/HotUpdateManager.h"
#include "HotUpdate.h"
#include "Core/HotUpdateSettings.h"
#include "Core/HotUpdateFileUtils.h"
#include "Core/HotUpdateVersionStorage.h"
#include "Download/HotUpdateDownloaderBase.h"
#include "HotUpdatePakManager.h"
#include "HotUpdateManifest.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

UHotUpdateManager::UHotUpdateManager()
	: CurrentState(EHotUpdateState::Idle)
{
}

void UHotUpdateManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UE_LOG(LogHotUpdate, Log, TEXT("HotUpdateManager initialized"));

	// 获取设置
	UHotUpdateSettings* Settings = UHotUpdateSettings::Get();

	// 创建版本存储管理器
	VersionStorage = NewObject<UHotUpdateVersionStorage>(this);
	if (VersionStorage)
	{
		VersionStorage->Initialize(Settings->GetLocalPakFullPath());

		// 加载本地版本信息
		VersionStorage->LoadLocalVersion(CurrentVersion);
	}

	// 创建下载器（通过工厂选择平台合适的实现）
	Downloader = UHotUpdateDownloaderBase::CreateDownloader(this);
	if (Downloader)
	{
		Downloader->Initialize(Settings->MaxConcurrentDownloads);

		// 绑定下载进度回调
		Downloader->OnProgress.AddDynamic(this, &UHotUpdateManager::HandleDownloadProgress);
		Downloader->OnComplete.AddDynamic(this, &UHotUpdateManager::HandleDownloadComplete);
	}

	// 创建 Pak 管理器
	PakManager = NewObject<UHotUpdatePakManager>(this);
	if (PakManager)
	{
		PakManager->Initialize(Settings->GetLocalPakFullPath());
	}

	// 启动时清理旧版本（如果配置启用）
	if (Settings->bAutoCleanupOldVersions)
	{
		CleanupOldVersions();
	}

	// 检查是否自动检查更新
	if (Settings->bAutoCheckOnStartup)
	{
		// 延迟检查更新，使用成员变量保存定时器句柄
		if (UGameInstance* GameInstance = GetGameInstance())
		{
			if (UWorld* World = GameInstance->GetWorld())
			{
				World->GetTimerManager().SetTimer(AutoCheckTimerHandle, [this]()
				{
					CheckForUpdate();
				}, 2.0f, false);
			}
		}
	}
}

void UHotUpdateManager::Deinitialize()
{
	UE_LOG(LogHotUpdate, Log, TEXT("HotUpdateManager deinitialized"));

	// 取消自动检查定时器
	if (AutoCheckTimerHandle.IsValid())
	{
		if (UGameInstance* GameInstance = GetGameInstance())
		{
			if (UWorld* World = GameInstance->GetWorld())
			{
				World->GetTimerManager().ClearTimer(AutoCheckTimerHandle);
			}
		}
	}

	if (Downloader)
	{
		Downloader->CancelDownload();
	}

	// 取消正在运行的 Flow
	if (Flow.IsValid() && Flow->IsRunning())
	{
		Flow->CancelFlow();
	}

	Super::Deinitialize();
}

void UHotUpdateManager::CheckForUpdate()
{
	UHotUpdateSettings* Settings = UHotUpdateSettings::Get();
	UE_LOG(LogHotUpdate, Log, TEXT("ManifestUrl = [%s], ResourceBaseUrl = [%s]"), *Settings->ManifestUrl, *Settings->ResourceBaseUrl);

	StartFlow();
}

bool UHotUpdateManager::StartDownload()
{
	if (CurrentState != EHotUpdateState::UpdateAvailable && CurrentState != EHotUpdateState::Downloaded)
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("Cannot start download in current state: %d"), (int32)CurrentState);
		return false;
	}

	if (VersionCheckResult.UpdateContainers.Num() == 0)
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("No containers to download"));
		return false;
	}

	if (!Downloader)
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Downloader not initialized"));
		return false;
	}

	SetState(EHotUpdateState::Downloading);

	UHotUpdateSettings* Settings = UHotUpdateSettings::Get();
	FString SaveDir = Settings->GetLocalPakFullPath() / LatestVersion.ToString();

	// 传入基础 URL（ResourceBaseUrl/），由下载器拼接 Version/Platform/Path
	FString ResourceBaseUrl = Settings->ResourceBaseUrl;
	if (!ResourceBaseUrl.EndsWith(TEXT("/")))
	{
		ResourceBaseUrl += TEXT("/");
	}
	FString DownloadBaseUrl = ResourceBaseUrl;

	Downloader->AddContainerDownloadTasks(VersionCheckResult.UpdateContainers, DownloadBaseUrl, SaveDir);
	Downloader->StartDownload();

	UE_LOG(LogHotUpdate, Log, TEXT("Starting container download for version %s, %d containers"),
		*LatestVersion.ToString(), VersionCheckResult.UpdateContainers.Num());
	return true;
}

void UHotUpdateManager::PauseDownload()
{
	if (Downloader && CurrentState == EHotUpdateState::Downloading)
	{
		Downloader->PauseDownload();
		SetState(EHotUpdateState::Paused);
		UE_LOG(LogHotUpdate, Log, TEXT("Download paused"));
	}
}

void UHotUpdateManager::ResumeDownload()
{
	if (Downloader && CurrentState == EHotUpdateState::Paused)
	{
		Downloader->ResumeDownload();
		SetState(EHotUpdateState::Downloading);
		UE_LOG(LogHotUpdate, Log, TEXT("Download resumed"));
	}
}

void UHotUpdateManager::CancelDownload()
{
	if (Downloader)
	{
		Downloader->CancelDownload();
		SetState(EHotUpdateState::Idle);
		UE_LOG(LogHotUpdate, Log, TEXT("Download cancelled"));
	}

	// 取消 Flow
	if (Flow.IsValid() && Flow->IsRunning())
	{
		Flow->CancelFlow();
	}
}

bool UHotUpdateManager::ApplyUpdate()
{
	if (CurrentState != EHotUpdateState::Downloaded)
	{
		return false;
	}

	SetState(EHotUpdateState::Installing);

	// 验证下载文件的完整性
	bool bSuccess = VerifyDownloadedFiles();

	if (bSuccess)
	{
		// 按 Manifest 中的容器列表挂载
		if (PakManager)
		{
			UHotUpdateSettings* Settings = UHotUpdateSettings::Get();
			FString BasePakDir = Settings->GetLocalPakFullPath();

			int32 MountedCount = 0;

			for (const FHotUpdateContainerInfo& Container : CachedServerManifest.Containers)
			{
				// 各容器从自己版本目录挂载，用自己的版本计算挂载顺序
				FString ContainerPakDir = Container.Version.IsEmpty() ? BasePakDir / LatestVersion.ToString() : BasePakDir / Container.Version;
				FHotUpdateVersionInfo ContainerVersion = Container.Version.IsEmpty() ? LatestVersion : FHotUpdateVersionInfo::FromString(Container.Version);
				int32 PakOrder = PakManager->CalculatePakOrder(ContainerVersion);

				// Pak 容器
				if (!Container.PakFile.Path.IsEmpty())
				{
					FString PakFilePath = ContainerPakDir / Container.PakFile.Path;
					if (PakManager->MountPak(PakFilePath, PakOrder))
					{
						MountedCount++;
						UE_LOG(LogHotUpdate, Log, TEXT("Mounted pak: %s"), *PakFilePath);
					}
					else
					{
						UE_LOG(LogHotUpdate, Warning, TEXT("Failed to mount pak: %s"), *PakFilePath);
					}
				}

				// IoStore 容器
				if (!Container.UtocFile.Path.IsEmpty())
				{
					FString UtocFilePath = ContainerPakDir / Container.UtocFile.Path;
					if (PakManager->MountPak(UtocFilePath, PakOrder))
					{
						MountedCount++;
						UE_LOG(LogHotUpdate, Log, TEXT("Mounted IoStore: %s"), *UtocFilePath);
					}
					else
					{
						UE_LOG(LogHotUpdate, Warning, TEXT("Failed to mount IoStore: %s"), *UtocFilePath);
					}
				}
			}

			if (MountedCount == 0 && CachedServerManifest.Containers.Num() > 0)
			{
				bSuccess = false;
				UE_LOG(LogHotUpdate, Error, TEXT("Failed to mount any pak/IoStore files"));
			}
		}

		if (bSuccess)
		{
			// 更新本地版本
			CurrentVersion = LatestVersion;
			if (VersionStorage)
			{
				VersionStorage->SaveLocalVersion(CurrentVersion);

				// 保存完整的服务器 Manifest 到本地缓存（用于下次增量下载对比）
				VersionStorage->SaveLocalManifest(CachedServerManifest);
			}

			// 清理旧版本
			CleanupOldVersions();

			SetState(EHotUpdateState::Success);
			UE_LOG(LogHotUpdate, Log, TEXT("Update applied successfully"));
		}
	}
	else
	{
		SetState(EHotUpdateState::Failed);
		UE_LOG(LogHotUpdate, Error, TEXT("Failed to apply update - verification failed"));
	}

	OnApplyComplete.Broadcast(bSuccess, bSuccess ? TEXT("") : TEXT("Update verification or installation failed"));
	return bSuccess;
}

void UHotUpdateManager::CleanupOldVersions()
{
	UHotUpdateSettings* Settings = UHotUpdateSettings::Get();
	if (!Settings->bAutoCleanupOldVersions)
	{
		return;
	}

	FString PakRootDir = Settings->GetLocalPakFullPath();
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	if (!PlatformFile.DirectoryExists(*PakRootDir))
	{
		return;
	}

	// 获取所有版本目录
	TArray<FString> VersionDirs;
	PlatformFile.IterateDirectory(*PakRootDir, [&VersionDirs, &PlatformFile](const TCHAR* Path, bool bIsDirectory)
	{
		if (bIsDirectory)
		{
			VersionDirs.Add(Path);
		}
		return true;
	});

	// 按版本号排序（最新的在前）
	VersionDirs.Sort([](const FString& A, const FString& B)
	{
		FHotUpdateVersionInfo VersionA = FHotUpdateVersionInfo::FromString(FPaths::GetCleanFilename(A));
		FHotUpdateVersionInfo VersionB = FHotUpdateVersionInfo::FromString(FPaths::GetCleanFilename(B));
		return VersionA > VersionB;
	});

	// 保留最新的 N 个版本
	int32 VersionsToKeep = FMath::Max(1, Settings->MaxLocalVersionCount);
	int32 DeletedCount = 0;

	for (int32 i = VersionsToKeep; i < VersionDirs.Num(); i++)
	{
		// 删除前先卸载该目录下所有已挂载的 Pak/IoStore
		if (PakManager)
		{
			TArray<FString> PakFiles;
			PlatformFile.FindFilesRecursively(PakFiles, *VersionDirs[i], TEXT(".pak"));

			TArray<FString> UtocFiles;
			PlatformFile.FindFilesRecursively(UtocFiles, *VersionDirs[i], TEXT(".utoc"));

			for (const FString& PakFile : PakFiles)
			{
				if (PakManager->IsPakMounted(PakFile))
				{
					PakManager->UnmountPak(PakFile);
					UE_LOG(LogHotUpdate, Log, TEXT("Unmounted before cleanup: %s"), *PakFile);
				}
			}

			for (const FString& UtocFile : UtocFiles)
			{
				if (PakManager->IsPakMounted(UtocFile))
				{
					PakManager->UnmountPak(UtocFile);
					UE_LOG(LogHotUpdate, Log, TEXT("Unmounted before cleanup: %s"), *UtocFile);
				}
			}
		}

		if (PlatformFile.DeleteDirectoryRecursively(*VersionDirs[i]))
		{
			DeletedCount++;
			UE_LOG(LogHotUpdate, Log, TEXT("Cleaned up old version: %s"), *VersionDirs[i]);
		}
	}

	if (DeletedCount > 0)
	{
		UE_LOG(LogHotUpdate, Log, TEXT("Cleanup complete: removed %d old versions, keeping %d versions"), DeletedCount, VersionsToKeep);
	}
}

void UHotUpdateManager::SetState(EHotUpdateState NewState)
{
	if (CurrentState != NewState)
	{
		EHotUpdateState OldState = CurrentState;
		CurrentState = NewState;
		UE_LOG(LogHotUpdate, Log, TEXT("State changed: %d -> %d"), (int32)OldState, (int32)NewState);
		OnStateChanged.Broadcast(NewState);
	}
}

bool UHotUpdateManager::VerifyDownloadedFiles()
{
	UHotUpdateSettings* Settings = UHotUpdateSettings::Get();
	FString BaseSaveDir = Settings->GetLocalPakFullPath();
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	int32 VerifiedCount = 0;
	int32 FailedCount = 0;

	// 验证单个文件
	auto VerifyFile = [&](const FString& FilePath, int64 ExpectedSize, const FString& ExpectedHash) -> bool
	{
		if (!PlatformFile.FileExists(*FilePath))
		{
			UE_LOG(LogHotUpdate, Error, TEXT("File missing: %s"), *FilePath);
			return false;
		}

		if (ExpectedSize > 0)
		{
			int64 ActualSize = IFileManager::Get().FileSize(*FilePath);
			if (ActualSize != ExpectedSize)
			{
				UE_LOG(LogHotUpdate, Error, TEXT("File size mismatch: %s (expected: %lld, actual: %lld)"),
					*FilePath, ExpectedSize, ActualSize);
				return false;
			}
		}

		if (!ExpectedHash.IsEmpty())
		{
			FString ActualHash = UHotUpdateFileUtils::CalculateFileHash(FilePath);
			if (ActualHash != ExpectedHash)
			{
				UE_LOG(LogHotUpdate, Error, TEXT("File hash mismatch: %s (expected: %s, actual: %s)"),
					*FilePath, *ExpectedHash, *ActualHash);
				return false;
			}
		}

		return true;
	};

	// 验证容器文件
	for (const FHotUpdateContainerInfo& Container : VersionCheckResult.UpdateContainers)
	{
		FString ContainerSaveDir = Container.Version.IsEmpty() ? BaseSaveDir / LatestVersion.ToString() : BaseSaveDir / Container.Version;

		if (!Container.UtocFile.Path.IsEmpty())
		{
			FString UtocFilePath = ContainerSaveDir / Container.UtocFile.Path;
			if (VerifyFile(UtocFilePath, Container.UtocFile.Size, Container.UtocFile.Hash))
			{
				VerifiedCount++;
				UE_LOG(LogHotUpdate, Verbose, TEXT("Verified container utoc: %s"), *UtocFilePath);
			}
			else
			{
				FailedCount++;
			}
		}

		if (!Container.UcasFile.Path.IsEmpty())
		{
			FString UcasFilePath = ContainerSaveDir / Container.UcasFile.Path;
			if (VerifyFile(UcasFilePath, Container.UcasFile.Size, Container.UcasFile.Hash))
			{
				VerifiedCount++;
				UE_LOG(LogHotUpdate, Verbose, TEXT("Verified container ucas: %s"), *UcasFilePath);
			}
			else
			{
				FailedCount++;
			}
		}

		if (!Container.PakFile.Path.IsEmpty())
		{
			FString PakFilePath = ContainerSaveDir / Container.PakFile.Path;
			if (VerifyFile(PakFilePath, Container.PakFile.Size, Container.PakFile.Hash))
			{
				VerifiedCount++;
				UE_LOG(LogHotUpdate, Verbose, TEXT("Verified container pak: %s"), *PakFilePath);
			}
			else
			{
				FailedCount++;
			}
		}
	}

	if (VerifiedCount == 0 && FailedCount == 0)
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("No containers to verify"));
		return true;
	}

	UE_LOG(LogHotUpdate, Log, TEXT("Verification complete: %d verified, %d failed"), VerifiedCount, FailedCount);
	return FailedCount == 0;
}

void UHotUpdateManager::HandleDownloadProgress(const FHotUpdateProgress& Progress)
{
	DownloadProgress = Progress;
	OnDownloadProgress.Broadcast(Progress);
}

void UHotUpdateManager::HandleDownloadComplete(bool bSuccess, const FString& ErrorMessage)
{
	if (bSuccess)
	{
		UE_LOG(LogHotUpdate, Log, TEXT("Download completed successfully"));
		SetState(EHotUpdateState::Downloaded);
	}
	else
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Download failed: %s"), *ErrorMessage);
		OnError.Broadcast(EHotUpdateError::DownloadFailed, ErrorMessage);
		SetState(EHotUpdateState::Failed);
	}

	OnDownloadComplete.Broadcast(bSuccess);

	// 推进 Flow
	if (bSuccess)
	{
		if (DownloadFlowHandle.IsValid())
		{
			DownloadFlowHandle->ContinueFlow();
			DownloadFlowHandle.Reset();
		}
	}
	else
	{
		// 下载失败，取消整个 Flow
		if (Flow.IsValid() && Flow->IsRunning())
		{
			Flow->CancelFlow();
		}
		DownloadFlowHandle.Reset();
	}
}

void UHotUpdateManager::CalculateIncrementalDownload(
	const FHotUpdateManifest& ServerManifest,
	const FHotUpdateManifest& LocalManifest,
	FHotUpdateVersionCheckResult& OutResult)
{
	// 构建本地 Container 索引（ContainerName -> Container）
	TMap<FString, const FHotUpdateContainerInfo*> LocalContainerIndex;
	for (const FHotUpdateContainerInfo& Container : LocalManifest.Containers)
	{
		LocalContainerIndex.Add(Container.ContainerName, &Container);
	}

	// 遍历服务端 Containers，分析差异
	for (const FHotUpdateContainerInfo& ServerContainer : ServerManifest.Containers)
	{
		const FHotUpdateContainerInfo* const* LocalContainerPtr = LocalContainerIndex.Find(ServerContainer.ContainerName);

		bool bNeedDownload = false;
		FString Reason;

		if (LocalContainerPtr == nullptr)
		{
			// 新增 Container
			bNeedDownload = true;
			Reason = TEXT("new container");
			OutResult.AddedContainerCount++;
		}
		else
		{
			const FHotUpdateContainerInfo* LocalContainer = *LocalContainerPtr;

			// 对比 Hash 判断是否需要更新
			if (LocalContainer->UcasFile.Hash != ServerContainer.UcasFile.Hash)
			{
				bNeedDownload = true;
				Reason = TEXT("ucas hash changed");
				OutResult.ModifiedContainerCount++;
			}
			else if (LocalContainer->UtocFile.Hash != ServerContainer.UtocFile.Hash)
			{
				bNeedDownload = true;
				Reason = TEXT("utoc hash changed");
				OutResult.ModifiedContainerCount++;
			}
			else if (!ServerContainer.PakFile.Hash.IsEmpty() &&
					 LocalContainer->PakFile.Hash != ServerContainer.PakFile.Hash)
			{
				bNeedDownload = true;
				Reason = TEXT("pak hash changed");
				OutResult.ModifiedContainerCount++;
			}
		}

		if (bNeedDownload)
		{
			OutResult.UpdateContainers.Add(ServerContainer);
			int64 ContainerSize = ServerContainer.UtocFile.Size + ServerContainer.UcasFile.Size + ServerContainer.PakFile.Size;
			OutResult.IncrementalDownloadSize += ContainerSize;

			UE_LOG(LogHotUpdate, Log, TEXT("Need download container: %s (reason: %s, size: %.2f MB)"),
				*ServerContainer.ContainerName, *Reason,
				ContainerSize / (1024.0 * 1024.0));
		}
		else
		{
			OutResult.SkippedContainerCount++;
			OutResult.SkippedTotalSize += ServerContainer.UtocFile.Size + ServerContainer.UcasFile.Size + ServerContainer.PakFile.Size;
			UE_LOG(LogHotUpdate, Verbose, TEXT("Skipped container: %s (unchanged)"),
				*ServerContainer.ContainerName);
		}
	}

	// 检测已删除的 Container（存在于本地但不在服务端）
	TSet<FString> ServerContainerNames;
	for (const FHotUpdateContainerInfo& ServerContainer : ServerManifest.Containers)
	{
		ServerContainerNames.Add(ServerContainer.ContainerName);
	}
	for (const FHotUpdateContainerInfo& LocalContainer : LocalManifest.Containers)
	{
		if (!ServerContainerNames.Contains(LocalContainer.ContainerName))
		{
			OutResult.DeletedContainerCount++;
			UE_LOG(LogHotUpdate, Verbose, TEXT("Deleted container: %s"),
				*LocalContainer.ContainerName);
		}
	}

	UE_LOG(LogHotUpdate, Log, TEXT("Incremental analysis complete: %d added, %d modified, %d deleted, %d skipped"),
		OutResult.AddedContainerCount, OutResult.ModifiedContainerCount, OutResult.DeletedContainerCount, OutResult.SkippedContainerCount);

	UE_LOG(LogHotUpdate, Log, TEXT("Required containers: %d, total download size: %.2f MB"),
		OutResult.UpdateContainers.Num(), OutResult.IncrementalDownloadSize / (1024.0 * 1024.0));
}

// ============================================================
// Flow 控制
// ============================================================
void UHotUpdateManager::StartFlow()
{
	// 取消正在运行的旧 Flow
	if (Flow.IsValid() && Flow->IsRunning())
	{
		UE_LOG(LogHotUpdate, Log, TEXT("Flow: Cancelling previous flow"));
		Flow->CancelFlow();
	}

	// 重置状态
	bVersionCheckHasUpdate = false;
	LatestJsonResponse.Empty();
	ManifestJsonResponse.Empty();
	LatestFlowHandle.Reset();
	ManifestFlowHandle.Reset();
	DownloadFlowHandle.Reset();

	// 构建并启动 Flow
	BuildFlow();
	Flow->ExecuteFlow();
}

void UHotUpdateManager::BuildFlow()
{
	Flow = MakeShared<FControlFlow>(TEXT("HotUpdate"));

	Flow->QueueStep(TEXT("FetchLatest"), this, &UHotUpdateManager::StepFetchLatest)
		.QueueStep(TEXT("FetchManifest"), this, &UHotUpdateManager::StepFetchManifest)
		.QueueStep(TEXT("ProcessVersionCheck"), this, &UHotUpdateManager::StepProcessVersionCheck)
		.QueueStep(TEXT("Download"), this, &UHotUpdateManager::StepDownload)
		.QueueStep(TEXT("Apply"), this, &UHotUpdateManager::StepApply);

	Flow->OnFlowComplete().AddUObject(this, &UHotUpdateManager::OnFlowComplete);
	Flow->OnFlowCancel().AddUObject(this, &UHotUpdateManager::OnFlowCancel);
}

// ============================================================
// Step 1: 请求 latest.json
// ============================================================
void UHotUpdateManager::StepFetchLatest(FControlFlowNodeRef FlowHandle)
{
	LatestFlowHandle = FlowHandle;

	UHotUpdateSettings* Settings = UHotUpdateSettings::Get();

	if (Settings->ManifestUrl.IsEmpty())
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("Flow: ManifestUrl is empty"));
		OnError.Broadcast(EHotUpdateError::EmptyUrl, TEXT("Manifest URL is empty"));
		FlowHandle->CancelFlow();
		return;
	}

	FString UrlError;
	if (!UHotUpdateSettings::ValidateUrl(Settings->ManifestUrl, UrlError))
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Flow: RL validation failed: %s"), *UrlError);
		VersionCheckResult.ErrorMessage = UrlError;
		OnVersionCheckComplete.Broadcast(VersionCheckResult);
		OnError.Broadcast(EHotUpdateError::InvalidUrl, UrlError);
		FlowHandle->CancelFlow();
		return;
	}

	SetState(EHotUpdateState::CheckingVersion);

	TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Settings->ManifestUrl);
	Request->SetVerb(TEXT("GET"));
	Request->SetTimeout(Settings->RequestTimeout);
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->OnProcessRequestComplete().BindUObject(this, &UHotUpdateManager::OnLatestResponse);
	Request->ProcessRequest();

	UE_LOG(LogHotUpdate, Log, TEXT("Flow: Fetching latest version from %s"), *Settings->ManifestUrl);
}

void UHotUpdateManager::OnLatestResponse(TSharedPtr<IHttpRequest> Request, TSharedPtr<IHttpResponse> Response, bool bSuccess)
{
	if (!bSuccess || !Response.IsValid())
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Flow: Latest version fetch failed"));
		SetState(EHotUpdateState::Failed);
		VersionCheckResult.ErrorMessage = TEXT("Network request failed");
		OnError.Broadcast(EHotUpdateError::NetworkError, TEXT("Network request failed"));
		OnVersionCheckComplete.Broadcast(VersionCheckResult);
		if (LatestFlowHandle.IsValid()) LatestFlowHandle->CancelFlow();
		return;
	}

	LatestJsonResponse = Response->GetContentAsString();

	// 解析 latest.json，检查是否有 manifestUrl
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(LatestJsonResponse);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Flow: Failed to parse latest version response"));
		SetState(EHotUpdateState::Failed);
		VersionCheckResult.ErrorMessage = TEXT("Invalid latest version format");
		OnError.Broadcast(EHotUpdateError::ParseError, TEXT("Invalid latest version format"));
		OnVersionCheckComplete.Broadcast(VersionCheckResult);
		if (LatestFlowHandle.IsValid()) LatestFlowHandle->CancelFlow();
		return;
	}

	// 查找 platforms[当前平台].manifestUrl
	const FString CurrentPlatform = FPlatformProperties::PlatformName();
	const TSharedPtr<FJsonObject>* PlatformsObj = nullptr;
	if (!JsonObject->TryGetObjectField(TEXT("platforms"), PlatformsObj) || !PlatformsObj)
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Flow: latest.json missing 'platforms' field"));
		SetState(EHotUpdateState::Failed);
		OnError.Broadcast(EHotUpdateError::ParseError, TEXT("latest.json missing 'platforms' field"));
		if (LatestFlowHandle.IsValid()) LatestFlowHandle->CancelFlow();
		return;
	}

	const TSharedPtr<FJsonObject>* PlatformObj = nullptr;
	if (!(*PlatformsObj)->TryGetObjectField(CurrentPlatform, PlatformObj) || !PlatformObj)
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Flow: Platform '%s' not found in latest.json"), *CurrentPlatform);
		SetState(EHotUpdateState::Failed);
		OnError.Broadcast(EHotUpdateError::ParseError, FString::Printf(TEXT("Platform '%s' not supported"), *CurrentPlatform));
		if (LatestFlowHandle.IsValid()) LatestFlowHandle->CancelFlow();
		return;
	}

	FString ManifestUrl;
	if (!(*PlatformObj)->TryGetStringField(TEXT("manifestUrl"), ManifestUrl) || ManifestUrl.IsEmpty())
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Flow: Platform '%s' missing 'manifestUrl'"), *CurrentPlatform);
		SetState(EHotUpdateState::Failed);
		OnError.Broadcast(EHotUpdateError::ParseError, TEXT("Platform manifestUrl is empty"));
		if (LatestFlowHandle.IsValid()) LatestFlowHandle->CancelFlow();
		return;
	}

	ManifestJsonResponse = ManifestUrl;
	UE_LOG(LogHotUpdate, Log, TEXT("Flow: Found manifestUrl for %s: %s"), *CurrentPlatform, *ManifestUrl);

	if (LatestFlowHandle.IsValid())
	{
		LatestFlowHandle->ContinueFlow();
		LatestFlowHandle.Reset();
	}
}

// ============================================================
// Step 2: 请求 manifest.json（条件执行）
// ============================================================
void UHotUpdateManager::StepFetchManifest(FControlFlowNodeRef FlowHandle)
{
	ManifestFlowHandle = FlowHandle;

	// Step1 存入的可能是 URL（有 manifestUrl）或完整 JSON（无 manifestUrl）
	if (ManifestJsonResponse.StartsWith(TEXT("http://")) || ManifestJsonResponse.StartsWith(TEXT("https://")))
	{
		FString ManifestUrl = ManifestJsonResponse;
		ManifestJsonResponse.Empty();

		UHotUpdateSettings* Settings = UHotUpdateSettings::Get();
		TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
		Request->SetURL(ManifestUrl);
		Request->SetVerb(TEXT("GET"));
		Request->SetTimeout(Settings->RequestTimeout);
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		Request->OnProcessRequestComplete().BindUObject(this, &UHotUpdateManager::OnManifestResponse);
		Request->ProcessRequest();

		UE_LOG(LogHotUpdate, Log, TEXT("Flow: Fetching manifest from %s"), *ManifestUrl);
		return;
	}

	// 已经是完整的 manifest JSON，直接继续
	UE_LOG(LogHotUpdate, Log, TEXT("Flow: Manifest already available, continuing"));
	ManifestFlowHandle->ContinueFlow();
}

void UHotUpdateManager::OnManifestResponse(TSharedPtr<IHttpRequest> Request, TSharedPtr<IHttpResponse> Response, bool bSuccess)
{
	if (!bSuccess || !Response.IsValid())
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Flow: Manifest fetch failed"));
		SetState(EHotUpdateState::Failed);
		VersionCheckResult.ErrorMessage = TEXT("Network request failed");
		OnError.Broadcast(EHotUpdateError::NetworkError, TEXT("Network request failed"));
		OnVersionCheckComplete.Broadcast(VersionCheckResult);
		if (ManifestFlowHandle.IsValid()) ManifestFlowHandle->CancelFlow();
		return;
	}

	ManifestJsonResponse = Response->GetContentAsString();

	if (ManifestFlowHandle.IsValid())
	{
		ManifestFlowHandle->ContinueFlow();
		ManifestFlowHandle.Reset();
	}
}

// ============================================================
// Step 3: 处理版本检查结果
// ============================================================
void UHotUpdateManager::StepProcessVersionCheck(FControlFlowNodeRef FlowHandle)
{
	// 解析 manifest
	FHotUpdateManifest ServerManifest;
	if (!UHotUpdateManifestParser::ParseFromJson(ManifestJsonResponse, ServerManifest))
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Flow: Failed to parse manifest"));
		SetState(EHotUpdateState::Failed);
		VersionCheckResult.ErrorMessage = TEXT("Invalid manifest format");
		OnError.Broadcast(EHotUpdateError::ParseError, TEXT("Invalid manifest format"));
		OnVersionCheckComplete.Broadcast(VersionCheckResult);
		FlowHandle->CancelFlow();
		return;
	}

	// 缓存服务器 Manifest
	CachedServerManifest = ServerManifest;

	// 版本校验
	FHotUpdateVersionInfo ServerVersion = ServerManifest.VersionInfo;
	if (ServerVersion.MajorVersion == 0 && ServerVersion.MinorVersion == 0 && ServerVersion.PatchVersion == 0)
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Flow: Invalid version info in manifest"));
		SetState(EHotUpdateState::Failed);
		VersionCheckResult.ErrorMessage = TEXT("Invalid version info in manifest");
		OnError.Broadcast(EHotUpdateError::InvalidVersion, TEXT("Invalid version info in manifest"));
		OnVersionCheckComplete.Broadcast(VersionCheckResult);
		FlowHandle->CancelFlow();
		return;
	}

	LatestVersion = ServerVersion;
	VersionCheckResult.LatestVersion = ServerVersion;
	VersionCheckResult.CurrentVersion = CurrentVersion;

	// 比较版本
	VersionCheckResult.bHasUpdate = ServerVersion > CurrentVersion;
	bVersionCheckHasUpdate = VersionCheckResult.bHasUpdate;

	// 初始化增量下载统计
	VersionCheckResult.UpdateContainers.Empty();
	VersionCheckResult.SkippedContainerCount = 0;
	VersionCheckResult.SkippedTotalSize = 0;
	VersionCheckResult.AddedContainerCount = 0;
	VersionCheckResult.ModifiedContainerCount = 0;
	VersionCheckResult.DeletedContainerCount = 0;
	VersionCheckResult.IncrementalDownloadSize = 0;

	// 增量对比
	FHotUpdateManifest LocalManifest;
	bool bHasLocalManifest = VersionStorage && VersionStorage->LoadLocalManifest(LocalManifest);

	if (bHasLocalManifest)
	{
		CalculateIncrementalDownload(ServerManifest, LocalManifest, VersionCheckResult);
	}
	else
	{
		UE_LOG(LogHotUpdate, Log, TEXT("Flow: No local manifest, downloading all containers"));
		for (const FHotUpdateContainerInfo& Container : ServerManifest.Containers)
		{
			VersionCheckResult.UpdateContainers.Add(Container);
			VersionCheckResult.IncrementalDownloadSize += Container.UtocFile.Size + Container.UcasFile.Size;
		}
		VersionCheckResult.AddedContainerCount = ServerManifest.Containers.Num();
	}

	UE_LOG(LogHotUpdate, Log, TEXT("Flow: Version check done - %s, %d containers to download"),
		*ServerVersion.ToString(), VersionCheckResult.UpdateContainers.Num());

	// 设置状态
	SetState(bVersionCheckHasUpdate ? EHotUpdateState::UpdateAvailable : EHotUpdateState::Idle);
	OnVersionCheckComplete.Broadcast(VersionCheckResult);

	FlowHandle->ContinueFlow();
}

// ============================================================
// Step 4: 下载
// ============================================================
void UHotUpdateManager::StepDownload(FControlFlowNodeRef FlowHandle)
{
	if (!bVersionCheckHasUpdate)
	{
		UE_LOG(LogHotUpdate, Log, TEXT("Flow: No update, skipping download"));
		FlowHandle->ContinueFlow();
		return;
	}

	// 如果下载已完成，直接跳过
	if (GetCurrentState() == EHotUpdateState::Downloaded)
	{
		UE_LOG(LogHotUpdate, Log, TEXT("Flow: Download already complete, skipping"));
		FlowHandle->ContinueFlow();
		return;
	}

	DownloadFlowHandle = FlowHandle;

	UHotUpdateSettings* Settings = UHotUpdateSettings::Get();

	if (Settings->bAutoDownload)
	{
		// bAutoDownload=True：StepProcessVersionCheck 之后，OnVersionCheckComplete 回调可能已经
		// 触发了 StartDownload。如果下载已在进行中，这里直接等待完成。
		if (GetCurrentState() != EHotUpdateState::Downloading)
		{
			if (!StartDownload())
			{
				UE_LOG(LogHotUpdate, Error, TEXT("Flow: Auto-download failed to start, cancelling flow"));
				FlowHandle->CancelFlow();
				return;
			}
		}
		UE_LOG(LogHotUpdate, Log, TEXT("Flow: Download step (auto), waiting for completion..."));
	}
	else
	{
		// bAutoDownload=False：等待用户调用 StartDownload()。
		// StartDownload() 会通过 HandleDownloadComplete → ContinueFlow。
		UE_LOG(LogHotUpdate, Log, TEXT("Flow: Download step (manual), waiting for user StartDownload()..."));
	}
}

// ============================================================
// Step 5: 应用更新
// ============================================================
void UHotUpdateManager::StepApply(FControlFlowNodeRef FlowHandle)
{
	if (!bVersionCheckHasUpdate)
	{
		UE_LOG(LogHotUpdate, Log, TEXT("Flow: No update to apply"));
		FlowHandle->ContinueFlow();
		return;
	}

	ApplyUpdate();
	FlowHandle->ContinueFlow();
}

// ============================================================
// Flow 事件
// ============================================================
void UHotUpdateManager::OnFlowComplete()
{
	UE_LOG(LogHotUpdate, Log, TEXT("Flow: Flow completed"));
}

void UHotUpdateManager::OnFlowCancel()
{
	UE_LOG(LogHotUpdate, Log, TEXT("Flow: Flow cancelled"));
}