// Copyright czm. All Rights Reserved.

#include "Core/HotUpdateManager.h"
#include "HotUpdate.h"
#include "Core/HotUpdateSettings.h"
#include "Core/HotUpdateFileUtils.h"
#include "Core/HotUpdateVersionStorage.h"
#include "Download/HotUpdateDownloaderBase.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "HotUpdatePakManager.h"
#include "HotUpdateManifest.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

UHotUpdateManager::UHotUpdateManager()
	: CurrentState(EHotUpdateState::Idle)
	, bHasUpdateAvailable(false)
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

	// 取消正在进行的请求
	if (VersionCheckRequest.IsValid())
	{
		VersionCheckRequest->CancelRequest();
		VersionCheckRequest.Reset();
	}

	if (Downloader)
	{
		Downloader->CancelDownload();
	}

	Super::Deinitialize();
}

void UHotUpdateManager::CheckForUpdate()
{
	UHotUpdateSettings* Settings = UHotUpdateSettings::Get();
	UE_LOG(LogHotUpdate, Log, TEXT("ManifestUrl = [%s], ResourceBaseUrl = [%s]"),
		*Settings->ManifestUrl, *Settings->ResourceBaseUrl);
	if (Settings->ManifestUrl.IsEmpty())
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("Manifest URL is empty"));
		OnError.Broadcast(EHotUpdateError::EmptyUrl, TEXT("Manifest URL is empty"));
		return;
	}

	// 验证 URL 安全性
	FString UrlErrorMessage;
	if (!UHotUpdateSettings::ValidateUrl(Settings->ManifestUrl, UrlErrorMessage))
	{
		UE_LOG(LogHotUpdate, Error, TEXT("URL validation failed: %s"), *UrlErrorMessage);
		VersionCheckResult.ErrorMessage = UrlErrorMessage;
		OnVersionCheckComplete.Broadcast(VersionCheckResult);
		OnError.Broadcast(EHotUpdateError::InvalidUrl, UrlErrorMessage);
		return;
	}

	SetState(EHotUpdateState::CheckingVersion);

	// 先请求 latest.json 获取最新版本信息
	TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Settings->ManifestUrl);
	Request->SetVerb(TEXT("GET"));
	Request->SetTimeout(Settings->RequestTimeout);
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

	Request->OnProcessRequestComplete().BindUObject(this, &UHotUpdateManager::HandleLatestVersionResponse);

	VersionCheckRequest = Request;
	Request->ProcessRequest();

	UE_LOG(LogHotUpdate, Log, TEXT("Fetching latest version from %s"), *Settings->ManifestUrl);
}

void UHotUpdateManager::HandleLatestVersionResponse(TSharedPtr<IHttpRequest> Request, TSharedPtr<IHttpResponse> Response, bool bSuccess)
{
	VersionCheckRequest.Reset();

	if (!bSuccess || !Response.IsValid())
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Latest version fetch failed"));
		SetState(EHotUpdateState::Failed);
		VersionCheckResult.ErrorMessage = TEXT("Network request failed");
		OnError.Broadcast(EHotUpdateError::NetworkError, TEXT("Network request failed"));
		OnVersionCheckComplete.Broadcast(VersionCheckResult);
		return;
	}

	FString ResponseContent = Response->GetContentAsString();

	// 解析 latest.json
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseContent);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Failed to parse latest version response"));
		SetState(EHotUpdateState::Failed);
		VersionCheckResult.ErrorMessage = TEXT("Invalid latest version format");
		OnError.Broadcast(EHotUpdateError::ParseError, TEXT("Invalid latest version format"));
		OnVersionCheckComplete.Broadcast(VersionCheckResult);
		return;
	}

	// 从 latest.json 获取 manifest URL
	FString ManifestUrl;
	if (JsonObject->TryGetStringField(TEXT("manifestUrl"), ManifestUrl) && !ManifestUrl.IsEmpty())
	{
		// 使用 latest.json 提供的 manifest URL
		UE_LOG(LogHotUpdate, Log, TEXT("Latest manifest URL: %s"), *ManifestUrl);
	}
	else
	{
		// latest.json 没有提供 manifestUrl，直接将当前响应当作 manifest 解析
		UE_LOG(LogHotUpdate, Log, TEXT("No manifestUrl in latest.json, treating response as manifest"));
		HandleVersionCheckResponse(Request, Response, bSuccess);
		return;
	}

	// 用获取到的 manifest URL 下载 manifest
	TSharedRef<IHttpRequest> ManifestRequest = FHttpModule::Get().CreateRequest();
	ManifestRequest->SetURL(ManifestUrl);
	ManifestRequest->SetVerb(TEXT("GET"));
	ManifestRequest->SetTimeout(UHotUpdateSettings::Get()->RequestTimeout);
	ManifestRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

	ManifestRequest->OnProcessRequestComplete().BindUObject(this, &UHotUpdateManager::HandleVersionCheckResponse);

	VersionCheckRequest = ManifestRequest;
	ManifestRequest->ProcessRequest();

	UE_LOG(LogHotUpdate, Log, TEXT("Fetching manifest from %s"), *ManifestUrl);
}

void UHotUpdateManager::HandleVersionCheckResponse(TSharedPtr<IHttpRequest> Request, TSharedPtr<IHttpResponse> Response, bool bSuccess)
{
	VersionCheckRequest.Reset();

	if (!bSuccess || !Response.IsValid())
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Manifest fetch failed"));
		SetState(EHotUpdateState::Failed);
		VersionCheckResult.ErrorMessage = TEXT("Network request failed");
		OnError.Broadcast(EHotUpdateError::NetworkError, TEXT("Network request failed"));
		OnVersionCheckComplete.Broadcast(VersionCheckResult);
		return;
	}

	FString ResponseContent = Response->GetContentAsString();

	// 使用 Manifest 解析器解析响应
	FHotUpdateManifest ServerManifest;
	if (!UHotUpdateManifestParser::ParseFromJson(ResponseContent, ServerManifest))
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Failed to parse manifest response"));
		SetState(EHotUpdateState::Failed);
		VersionCheckResult.ErrorMessage = TEXT("Invalid manifest format");
		OnError.Broadcast(EHotUpdateError::ParseError, TEXT("Invalid manifest format"));
		OnVersionCheckComplete.Broadcast(VersionCheckResult);
		return;
	}

	// 缓存服务器 Manifest（用于更新成功后保存到本地）
	CachedServerManifest = ServerManifest;

	// 从 Manifest 获取版本信息
	FHotUpdateVersionInfo ServerVersion = ServerManifest.VersionInfo;
	if (ServerVersion.MajorVersion == 0 && ServerVersion.MinorVersion == 0 && ServerVersion.PatchVersion == 0)
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Invalid version info in manifest"));
		SetState(EHotUpdateState::Failed);
		VersionCheckResult.ErrorMessage = TEXT("Invalid version info in manifest");
		OnError.Broadcast(EHotUpdateError::InvalidVersion, TEXT("Invalid version info in manifest"));
		OnVersionCheckComplete.Broadcast(VersionCheckResult);
		return;
	}

	LatestVersion = ServerVersion;
	VersionCheckResult.LatestVersion = ServerVersion;
	VersionCheckResult.CurrentVersion = CurrentVersion;

	// 比较版本
	VersionCheckResult.bHasUpdate = ServerVersion > CurrentVersion;
	bHasUpdateAvailable = VersionCheckResult.bHasUpdate;

	// 初始化增量下载统计
	VersionCheckResult.UpdateContainers.Empty();
	VersionCheckResult.SkippedContainerCount = 0;
	VersionCheckResult.SkippedTotalSize = 0;
	VersionCheckResult.AddedContainerCount = 0;
	VersionCheckResult.ModifiedContainerCount = 0;
	VersionCheckResult.DeletedContainerCount = 0;
	VersionCheckResult.IncrementalDownloadSize = 0;

	// 加载本地 Manifest 缓存进行增量对比
	FHotUpdateManifest LocalManifest;
	bool bHasLocalManifest = VersionStorage && VersionStorage->LoadLocalManifest(LocalManifest);

	if (bHasLocalManifest)
	{
		// 计算需要下载的 Container（基于 Hash 对比）
		CalculateIncrementalDownload(ServerManifest, LocalManifest, VersionCheckResult);
	}
	else
	{
		// 没有本地 Manifest，下载所有 Container
		UE_LOG(LogHotUpdate, Log, TEXT("No local manifest found, downloading all containers"));

		for (const FHotUpdateContainerInfo& Container : ServerManifest.Containers)
		{
			VersionCheckResult.UpdateContainers.Add(Container);
			VersionCheckResult.IncrementalDownloadSize += Container.UtocSize + Container.UcasSize;
			UE_LOG(LogHotUpdate, Log, TEXT("Container to download: %s (size: %.2f MB)"),
				*Container.ContainerName, (Container.UtocSize + Container.UcasSize) / (1024.0 * 1024.0));
		}

		VersionCheckResult.AddedContainerCount = ServerManifest.Containers.Num();
	}

	UE_LOG(LogHotUpdate, Log, TEXT("Manifest parsed: version %s, %d containers, %d to download (%.2f MB), has update: %s"),
		*ServerVersion.ToString(),
		ServerManifest.Containers.Num(),
		VersionCheckResult.UpdateContainers.Num(),
		VersionCheckResult.IncrementalDownloadSize / (1024.0 * 1024.0),
		bHasUpdateAvailable ? TEXT("true") : TEXT("false"));

	UE_LOG(LogHotUpdate, Log, TEXT("Incremental stats: Added=%d, Modified=%d, Skipped=%d (saved %.2f MB)"),
		VersionCheckResult.AddedContainerCount,
		VersionCheckResult.ModifiedContainerCount,
		VersionCheckResult.SkippedContainerCount,
		VersionCheckResult.SkippedTotalSize / (1024.0 * 1024.0));

	// 根据是否有更新设置对应状态
	SetState(bHasUpdateAvailable ? EHotUpdateState::UpdateAvailable : EHotUpdateState::Idle);
	OnVersionCheckComplete.Broadcast(VersionCheckResult);

	// 自动下载：检测到更新且开启自动下载时，自动开始下载
	if (bHasUpdateAvailable && UHotUpdateSettings::Get()->bAutoDownload)
	{
		UE_LOG(LogHotUpdate, Log, TEXT("Auto-download enabled, starting download automatically"));
		StartDownload();
	}
}

bool UHotUpdateManager::StartDownload()
{
	if (CurrentState != EHotUpdateState::Idle && CurrentState != EHotUpdateState::UpdateAvailable)
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
	FString DownloadBaseUrl = BuildDownloadBaseUrl();

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
}

bool UHotUpdateManager::ApplyUpdate()
{
	if (CurrentState != EHotUpdateState::Idle && CurrentState != EHotUpdateState::UpdateAvailable)
	{
		return false;
	}

	SetState(EHotUpdateState::Installing);

	// 验证下载文件的完整性
	bool bSuccess = VerifyDownloadedFiles();

	if (bSuccess)
	{
		// 挂载新的 Pak/IoStore 文件
		if (PakManager)
		{
			UHotUpdateSettings* Settings = UHotUpdateSettings::Get();
			FString PakDir = Settings->GetLocalPakFullPath() / LatestVersion.ToString();

			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

			// 查找该版本目录下的所有 .pak 文件
			TArray<FString> PakFiles;
			PlatformFile.FindFilesRecursively(PakFiles, *PakDir, TEXT(".pak"));

			// 查找该版本目录下的所有 .utoc 文件（IoStore 容器）
			TArray<FString> UtocFiles;
			PlatformFile.FindFilesRecursively(UtocFiles, *PakDir, TEXT(".utoc"));

			int32 MountedCount = 0;

			// 挂载 .pak 文件
			for (const FString& PakFile : PakFiles)
			{
				FHotUpdatePakMetadata Metadata = PakManager->ParsePakMetadata(PakFile);
				int32 PakOrder = PakManager->CalculatePakOrder(Metadata.PakName, Metadata.Version);
				if (PakManager->MountPak(PakFile, PakOrder))
				{
					MountedCount++;
					UE_LOG(LogHotUpdate, Log, TEXT("Mounted pak file: %s"), *PakFile);
				}
				else
				{
					UE_LOG(LogHotUpdate, Warning, TEXT("Failed to mount pak file: %s"), *PakFile);
				}
			}

			// 挂载 IoStore 容器（.utoc）
			for (const FString& UtocFile : UtocFiles)
			{
				FHotUpdatePakMetadata UtocMetadata = PakManager->ParsePakMetadata(UtocFile);
				int32 UtocPakOrder = PakManager->CalculatePakOrder(UtocMetadata.PakName, UtocMetadata.Version);
				if (PakManager->MountPak(UtocFile, UtocPakOrder))
				{
					MountedCount++;
					UE_LOG(LogHotUpdate, Log, TEXT("Mounted IoStore container: %s"), *UtocFile);
				}
				else
				{
					UE_LOG(LogHotUpdate, Warning, TEXT("Failed to mount IoStore container: %s"), *UtocFile);
				}
			}

			if (MountedCount == 0 && (PakFiles.Num() > 0 || UtocFiles.Num() > 0))
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
	FString SaveDir = Settings->GetLocalPakFullPath() / LatestVersion.ToString();
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

	// 验证容器文件（IoStore 模式）
	for (const FHotUpdateContainerInfo& Container : VersionCheckResult.UpdateContainers)
	{
		if (!Container.UtocPath.IsEmpty())
		{
			FString UtocFilePath = SaveDir / Container.UtocPath;
			if (VerifyFile(UtocFilePath, Container.UtocSize, Container.UtocHash))
			{
				VerifiedCount++;
				UE_LOG(LogHotUpdate, Verbose, TEXT("Verified container utoc: %s"), *UtocFilePath);
			}
			else
			{
				FailedCount++;
			}
		}

		if (!Container.UcasPath.IsEmpty())
		{
			FString UcasFilePath = SaveDir / Container.UcasPath;
			if (VerifyFile(UcasFilePath, Container.UcasSize, Container.UcasHash))
			{
				VerifiedCount++;
				UE_LOG(LogHotUpdate, Verbose, TEXT("Verified container ucas: %s"), *UcasFilePath);
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

FString UHotUpdateManager::BuildDownloadBaseUrl() const
{
	UHotUpdateSettings* Settings = UHotUpdateSettings::Get();
	FString Url = Settings->ResourceBaseUrl;
	if (!Url.EndsWith(TEXT("/")))
	{
		Url += TEXT("/");
	}
	Url += LatestVersion.VersionString + TEXT("/") + CachedServerManifest.VersionInfo.Platform + TEXT("/");
	return Url;
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
		SetState(EHotUpdateState::Idle);
	}
	else
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Download failed: %s"), *ErrorMessage);
		OnError.Broadcast(EHotUpdateError::DownloadFailed, ErrorMessage);
		SetState(EHotUpdateState::Failed);
	}

	OnDownloadComplete.Broadcast(bSuccess);
}

void UHotUpdateManager::CalculateIncrementalDownload(
	const FHotUpdateManifest& ServerManifest,
	const FHotUpdateManifest& LocalManifest,
	FHotUpdateVersionCheckResult& OutResult)
{
	// 构建本地 Container 索引（ChunkId -> Container）
	TMap<int32, const FHotUpdateContainerInfo*> LocalContainerIndex;
	for (const FHotUpdateContainerInfo& Container : LocalManifest.Containers)
	{
		LocalContainerIndex.Add(Container.ChunkId, &Container);
	}

	// 遍历服务端 Containers，分析差异
	for (const FHotUpdateContainerInfo& ServerContainer : ServerManifest.Containers)
	{
		const FHotUpdateContainerInfo* const* LocalContainerPtr = LocalContainerIndex.Find(ServerContainer.ChunkId);

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
			if (LocalContainer->UcasHash != ServerContainer.UcasHash)
			{
				bNeedDownload = true;
				Reason = TEXT("ucas hash changed");
				OutResult.ModifiedContainerCount++;
			}
			else if (LocalContainer->UtocHash != ServerContainer.UtocHash)
			{
				bNeedDownload = true;
				Reason = TEXT("utoc hash changed");
				OutResult.ModifiedContainerCount++;
			}
		}

		if (bNeedDownload)
		{
			OutResult.UpdateContainers.Add(ServerContainer);
			OutResult.IncrementalDownloadSize += ServerContainer.UtocSize + ServerContainer.UcasSize;

			UE_LOG(LogHotUpdate, Log, TEXT("Need download container: %s (chunkId: %d, reason: %s, size: %.2f MB)"),
				*ServerContainer.ContainerName, ServerContainer.ChunkId, *Reason,
				(ServerContainer.UtocSize + ServerContainer.UcasSize) / (1024.0 * 1024.0));
		}
		else
		{
			OutResult.SkippedContainerCount++;
			OutResult.SkippedTotalSize += ServerContainer.UtocSize + ServerContainer.UcasSize;
			UE_LOG(LogHotUpdate, Verbose, TEXT("Skipped container: %s (chunkId: %d, unchanged)"),
				*ServerContainer.ContainerName, ServerContainer.ChunkId);
		}
	}

	// 检测已删除的 Container（存在于本地但不在服务端）
	for (const FHotUpdateContainerInfo& LocalContainer : LocalManifest.Containers)
	{
		bool bFound = false;
		for (const FHotUpdateContainerInfo& ServerContainer : ServerManifest.Containers)
		{
			if (ServerContainer.ChunkId == LocalContainer.ChunkId)
			{
				bFound = true;
				break;
			}
		}

		if (!bFound)
		{
			OutResult.DeletedContainerCount++;
			UE_LOG(LogHotUpdate, Verbose, TEXT("Deleted container: %s (chunkId: %d)"),
				*LocalContainer.ContainerName, LocalContainer.ChunkId);
		}
	}

	UE_LOG(LogHotUpdate, Log, TEXT("Incremental analysis complete: %d added, %d modified, %d deleted, %d skipped"),
		OutResult.AddedContainerCount, OutResult.ModifiedContainerCount, OutResult.DeletedContainerCount, OutResult.SkippedContainerCount);

	UE_LOG(LogHotUpdate, Log, TEXT("Required containers: %d, total download size: %.2f MB"),
		OutResult.UpdateContainers.Num(), OutResult.IncrementalDownloadSize / (1024.0 * 1024.0));
}