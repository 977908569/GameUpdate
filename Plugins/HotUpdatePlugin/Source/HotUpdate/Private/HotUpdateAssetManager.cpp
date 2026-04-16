// Copyright czm. All Rights Reserved.

#include "HotUpdateAssetManager.h"
#include "HotUpdate.h"
#include "Core/HotUpdateSettings.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

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

				// Pre-compute all dependencies of whitelist packages
				if (bMinimalPackageEnabled && CachedWhitelistDirs.Num() > 0)
				{
					FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
					IAssetRegistry* AssetRegistry = &AssetRegistryModule.Get();

					if (AssetRegistry->IsLoadingAssets())
					{
						AssetRegistry->SearchAllAssets(true);
						while (AssetRegistry->IsLoadingAssets())
						{
							FPlatformProcess::Sleep(0.1f);
						}
					}

					// Collect all whitelist packages and their dependencies
					TSet<FString> Visited;
					for (const FString& WhitelistDir : CachedWhitelistDirs)
					{
						TArray<FAssetData> Assets;
						AssetRegistry->GetAssetsByPath(*WhitelistDir, Assets, true);
						for (const FAssetData& Asset : Assets)
						{
							FString AssetPath = Asset.PackageName.ToString();
							CachedChunk0Packages.Add(AssetPath);
							CollectDependenciesRecursive(AssetPath, AssetRegistry, Visited, 50);
						}
					}
					CachedChunk0Packages.Append(Visited);

					UE_LOG(LogHotUpdate, Log, TEXT("GetPackageChunkIds: Collected %d packages for Chunk 0 (whitelist + dependencies)"),
						CachedChunk0Packages.Num());
				}
			}

			UE_LOG(LogHotUpdate, Log, TEXT("GetPackageChunkIds config loaded: MinimalPackage=%s, WhitelistDirs=%d, Chunk0Packages=%d"),
				bMinimalPackageEnabled ? TEXT("True") : TEXT("False"), CachedWhitelistDirs.Num(), CachedChunk0Packages.Num());
		}
	}

	// If minimal package mode is enabled, check if this package should be in Chunk 0
	if (bMinimalPackageEnabled)
	{
		FString PackageStr = PackageName.ToString();

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

		// For non-whitelist packages: DO NOT override.
		// Let the engine's default chunk assignment (from Super) decide.
		// Engine resources, default materials, etc. will naturally stay in Chunk 0.
		// Only explicitly whitelisted packages are forced to Chunk 0;
		// everything else follows standard UE chunk rules.
	}

	// Return engine's default assignment
	return bHasChunkIds;
}

void UHotUpdateAssetManager::CollectDependenciesRecursive(
	const FString& PackagePath,
	IAssetRegistry* AssetRegistry,
	TSet<FString>& Visited,
	int32 MaxDepth)
{
	if (MaxDepth <= 0 || Visited.Contains(PackagePath))
	{
		return;
	}

	// Only collect dependencies for valid asset paths
	// Includes /Game/ (project), /Engine/ (engine), and plugin paths (e.g. /NNE/, /Water/)
	// Excludes /Script/ (C++ type references, not assets)
	if (!(PackagePath.StartsWith(TEXT("/Game/")) || PackagePath.StartsWith(TEXT("/Engine/")) ||
		(PackagePath.StartsWith(TEXT("/")) && !PackagePath.StartsWith(TEXT("/Script/")))))
	{
		return;
	}

	Visited.Add(PackagePath);

	// Get package dependencies
	TArray<FAssetIdentifier> Dependencies;
	AssetRegistry->GetDependencies(FAssetIdentifier(*PackagePath), Dependencies, UE::AssetRegistry::EDependencyCategory::Package);

	for (const FAssetIdentifier& Dep : Dependencies)
	{
		FString DepPackageName = Dep.PackageName.ToString();
		if (!Visited.Contains(DepPackageName))
		{
			CollectDependenciesRecursive(DepPackageName, AssetRegistry, Visited, MaxDepth - 1);
		}
	}
}
#endif
