// Copyright czm. All Rights Reserved.

#include "HotUpdateAssetManager.h"
#include "HotUpdate.h"
#include "Core/HotUpdateSettings.h"
#include "Core/HotUpdateFileUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/PackageName.h"

// Config file path for minimal package mode
static FString GetMinimalPackageConfigFilePath()
{
	return FPaths::ProjectIntermediateDir() / TEXT("MinimalPackageConfig.json");
}

UHotUpdateAssetManager::UHotUpdateAssetManager()
{
	UE_LOG(LogHotUpdate, Log, TEXT("UHotUpdateAssetManager constructed"));
}

#if WITH_EDITOR
bool UHotUpdateAssetManager::GetPackageChunkIds(
	FName PackageName, const ITargetPlatform* TargetPlatform,
	TArrayView<const int32> ExistingChunkList, TArray<int32>& OutChunkList,
	TArray<int32>* OutOverrideChunkList) const
{
	// Get engine's default chunk assignment first
	bool bHasChunkIds = Super::GetPackageChunkIds(PackageName, TargetPlatform, ExistingChunkList, OutChunkList, OutOverrideChunkList);

	// Cached state (persisted across calls via static)
	static bool bMinimalPackageEnabled = false;
	static TArray<FString> CachedWhitelistDirs;
	static TSet<FString> CachedChunk0Packages;  // Expanded set: whitelist dirs + dependencies
	static FDateTime CachedFileTimestamp = FDateTime(0);
	static TMap<FString, int32> CachedChunkMapping;

	FString ConfigFile = GetMinimalPackageConfigFilePath();

	// Check if config file exists and needs reload
	bool bNeedReload = false;
	if (FPaths::FileExists(ConfigFile))
	{
		FFileStatData StatData = IFileManager::Get().GetStatData(*ConfigFile);
		if (StatData.bIsValid && StatData.ModificationTime != CachedFileTimestamp)
		{
			bNeedReload = true;
			CachedFileTimestamp = StatData.ModificationTime;
		}
	}
	else
	{
		// Config file doesn't exist, use engine defaults
		CachedWhitelistDirs.Empty();
		CachedChunk0Packages.Empty();
		CachedChunkMapping.Empty();
		bMinimalPackageEnabled = false;
		CachedFileTimestamp = FDateTime(0);
		return bHasChunkIds;
	}

	// Reload config if needed
	if (bNeedReload)
	{
		FString JsonStr;
		if (FFileHelper::LoadFileToString(JsonStr, *ConfigFile))
		{
			UE_LOG(LogHotUpdate, Log, TEXT("GetPackageChunkIds: Loading config from %s"), *ConfigFile);

			TSharedPtr<FJsonObject> JsonObj;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
			if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
			{
				bMinimalPackageEnabled = JsonObj->GetBoolField(TEXT("bEnableMinimalPackage"));
				CachedWhitelistDirs.Empty();
				CachedChunk0Packages.Empty();
				CachedChunkMapping.Empty();

				// Read whitelist directories
				const TArray<TSharedPtr<FJsonValue>>* WhitelistArray;
				if (JsonObj->TryGetArrayField(TEXT("WhitelistDirectories"), WhitelistArray))
				{
					for (const TSharedPtr<FJsonValue>& Value : *WhitelistArray)
					{
						FString Dir = Value->AsString();
						if (!Dir.StartsWith(TEXT("/")))
						{
							Dir = TEXT("/") + Dir;
						}
						CachedWhitelistDirs.Add(Dir);
					}
				}

				// Read ChunkMapping (pre-computed by Editor process)
				CachedChunkMapping.Empty();
				const TSharedPtr<FJsonObject>* MappingObj;
				if (JsonObj->TryGetObjectField(TEXT("ChunkMapping"), MappingObj))
				{
					for (const auto& Pair : (*MappingObj)->Values)
					{
						CachedChunkMapping.Add(Pair.Key, (int32)Pair.Value->AsNumber());
					}
					UE_LOG(LogHotUpdate, Log, TEXT("GetPackageChunkIds: Loaded %d ChunkMapping entries"), CachedChunkMapping.Num());
				}

				// Read Chunk0Packages (pre-computed by Editor process: whitelist + dependencies)
				const TArray<TSharedPtr<FJsonValue>>* Chunk0Array;
				if (JsonObj->TryGetArrayField(TEXT("Chunk0Packages"), Chunk0Array))
				{
					for (const TSharedPtr<FJsonValue>& Value : *Chunk0Array)
					{
						CachedChunk0Packages.Add(Value->AsString());
					}
					UE_LOG(LogHotUpdate, Log, TEXT("GetPackageChunkIds: Loaded %d Chunk0Packages"), CachedChunk0Packages.Num());
				}

				UE_LOG(LogHotUpdate, Log, TEXT("GetPackageChunkIds config loaded: MinimalPackage=%s, WhitelistDirs=%d, Chunk0Packages=%d"),
					bMinimalPackageEnabled ? TEXT("True") : TEXT("False"), CachedWhitelistDirs.Num(), CachedChunk0Packages.Num());
			}
		}
	}

	// If minimal package mode is enabled, check if this package should be in Chunk 0
	if (bMinimalPackageEnabled)
	{
		FString PackageStr = PackageName.ToString();

		// 引擎资源始终分配到 Chunk 0（首包）
		if (UHotUpdateFileUtils::IsEngineAsset(PackageStr))
		{
			OutChunkList.Empty();
			OutChunkList.Add(0);
			return true;
		}

		bool bShouldBeInChunk0 = false;

		// Check if package is in the expanded Chunk 0 set (whitelist + dependencies)
		if (CachedChunk0Packages.Contains(PackageStr))
		{
			bShouldBeInChunk0 = true;
		}
		else
		{
			// Fallback: check if package is in a whitelist directory
			for (const FString& WhitelistDir : CachedWhitelistDirs)
			{
				if (PackageStr.StartsWith(WhitelistDir))
				{
					bShouldBeInChunk0 = true;
					break;
				}
			}
		}

		if (bShouldBeInChunk0)
		{
			// Whitelist resources + dependencies -> ensure Chunk 0
			OutChunkList.Empty();
			OutChunkList.Add(0);
			return true;
		}

		// Non-whitelist packages: lookup ChunkMapping for actual Chunk ID
		const int32* MappedChunkId = CachedChunkMapping.Find(PackageStr);
		if (MappedChunkId)
		{
			OutChunkList.Empty();
			OutChunkList.Add(*MappedChunkId);
			return true;
		}

		// Fallback: no mapping found, assign to Chunk 11 (hot update patch)
		OutChunkList.Empty();
		OutChunkList.Add(11);
		return true;
	}

	// Return engine's default assignment
	return bHasChunkIds;
}
#endif