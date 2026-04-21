// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetManager.h"
#include "HotUpdateAssetManager.generated.h"

/**
 * Custom AssetManager that supports minimal package mode for chunk assignment.
 *
 * Chunk assignment rules:
 * - Chunk 0: Whitelist resources (base package, ships with exe/apk)
 * - Chunk 1+: Other resources (hot update package, downloaded later)
 *
 * Usage:
 * 1. Configure in DefaultEngine.ini: [/Script/Engine.Engine] AssetManagerClassName=/Script/HotUpdate.HotUpdateAssetManager
 * 2. Use HotUpdateCommandlet with -minimal -whitelist="Game/UI" parameters
 * 3. Config is passed via temp file (Intermediate/MinimalPackageConfig.json)
 */
UCLASS(Config = Game)
class HOTUPDATE_API UHotUpdateAssetManager : public UAssetManager
{
	GENERATED_BODY()

public:
	UHotUpdateAssetManager();

#if WITH_EDITOR
	//~UAssetManager interface
	/**
	 * Override chunk assignment function.
	 * Only overrides Chunk 0 assignment for whitelist packages and their dependencies.
	 * Non-whitelist packages keep the engine's default chunk assignment.
	 */
	virtual bool GetPackageChunkIds(FName PackageName, const ITargetPlatform* TargetPlatform,
		TArrayView<const int32> ExistingChunkList, TArray<int32>& OutChunkList,
		TArray<int32>* OutOverrideChunkList = nullptr) const override;
	//~End of UAssetManager interface
#endif
};