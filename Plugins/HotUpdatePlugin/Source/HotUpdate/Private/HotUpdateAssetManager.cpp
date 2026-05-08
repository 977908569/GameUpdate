// Copyright czm. All Rights Reserved.
#include "HotUpdateAssetManager.h"
#include "HotUpdate.h"
#include "Core/HotUpdateFileUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

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
	const bool bHasChunkIds = Super::GetPackageChunkIds(PackageName, TargetPlatform, ExistingChunkList, OutChunkList, OutOverrideChunkList);

	if (!bMinimalPackageEnabled)
	{
		return bHasChunkIds;
	}

	const FString PackageStr = PackageName.ToString();

	// 引擎资源 → Chunk 0
	if (UHotUpdateFileUtils::IsEngineAsset(PackageStr))
	{
		OutChunkList.Empty();
		OutChunkList.Add(0);
		return true;
	}

	// 查找映射
	if (const int32* MappedChunkId = ChunkMapping.Find(PackageStr))
	{
		OutChunkList.Empty();
		OutChunkList.Add(*MappedChunkId);
		return true;
	}

	// 未找到 → 兜底 Chunk 1（cook 时发现的额外资产归入主 patch chunk）
	OutChunkList.Empty();
	OutChunkList.Add(1);
	return true;
}

TMap<int32, FAssetManagerChunkInfo> UHotUpdateAssetManager::BuildChunkMap(const TSet<FName>& PackagesToUpdateChunksFor,
	const TSet<FName>& StartupPackages, const TSet<FName>& PackageThatWereCooked) const
{
	ChunkMapping.Empty();
	bMinimalPackageEnabled = false;
	// 加载最小包配置
	const FString ConfigFile = FPaths::ProjectIntermediateDir() / TEXT("MinimalPackageConfig.json");
	if (FPaths::FileExists(ConfigFile))
	{
		FString JsonStr;
		if (FFileHelper::LoadFileToString(JsonStr, *ConfigFile))
		{
			TSharedPtr<FJsonObject> JsonObj;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
			if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
			{
				bMinimalPackageEnabled = JsonObj->GetBoolField(TEXT("bEnableMinimalPackage"));

				const TSharedPtr<FJsonObject>* MappingObj;
				if (JsonObj->TryGetObjectField(TEXT("ChunkMapping"), MappingObj))
				{
					for (const auto& Pair : (*MappingObj)->Values)
					{
						ChunkMapping.Add(Pair.Key, (int32)Pair.Value->AsNumber());
					}
				}

				UE_LOG(LogHotUpdate, Log, TEXT("BuildChunkMap: MinimalPackage Enabled=%s, ChunkMapping=%d"),
					bMinimalPackageEnabled ? TEXT("True") : TEXT("False"),
					ChunkMapping.Num());
			}
		}
	}

	return Super::BuildChunkMap(PackagesToUpdateChunksFor, StartupPackages, PackageThatWereCooked);
}
#endif