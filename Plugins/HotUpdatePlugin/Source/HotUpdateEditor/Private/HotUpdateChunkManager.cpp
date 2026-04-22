// Copyright czm. All Rights Reserved.

#include "HotUpdateChunkManager.h"
#include "HotUpdateEditor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

int32 FHotUpdateChunkManager::NextAutoChunkId = 0;
FCriticalSection FHotUpdateChunkManager::ChunkIdLock;

FHotUpdateChunkManager::FHotUpdateChunkManager()
{
}

FHotUpdateChunkAnalysisResult FHotUpdateChunkManager::AnalyzeAndCreateChunks(
	const TArray<FString>& AssetPaths,
	const TMap<FString, FString>& AssetDiskPaths,
	const FHotUpdateChunkAnalysisConfig& Config)
{
	FHotUpdateChunkAnalysisResult Result;

	if (AssetPaths.Num() == 0)
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("资源列表为空");
		return Result;
	}

	// 获取 AssetRegistry
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry* AssetRegistry = &AssetRegistryModule.Get();

	TArray<FHotUpdateChunkDefinition> Chunks;
	TMap<FString, int32> AssetToChunk;

	// 根据分包策略选择不同的划分方法
	switch (Config.ChunkStrategy)
	{
	case EHotUpdateChunkStrategy::None:
		{
			// 不分包，所有资源打包成一个 Chunk
			int32 ChunkId = Config.DefaultChunkId >= 0 ? Config.DefaultChunkId : AllocateNextChunkId();
			CreateSingleChunk(AssetPaths, AssetDiskPaths, Config.DefaultChunkName, ChunkId, Chunks, AssetToChunk);
		}
		break;

	case EHotUpdateChunkStrategy::Size:
		{
			// 按大小分包
			DivideBySizeWithConfig(AssetPaths, AssetDiskPaths, Config.SizeBasedConfig, Chunks, AssetToChunk);
		}
		break;


	default:
		{
			// 未知策略，回退到不分包
			int32 DefaultId = Config.DefaultChunkId >= 0 ? Config.DefaultChunkId : AllocateNextChunkId();
			CreateSingleChunk(AssetPaths, AssetDiskPaths, Config.DefaultChunkName, DefaultId, Chunks, AssetToChunk);
		}
		break;
	}

	if (Chunks.Num() == 0)
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("Chunk 划分失败");
		return Result;
	}

	// 构建依赖关系
	if (Config.bAnalyzeDependencies)
	{
		BuildDependencies(Chunks, AssetToChunk, AssetRegistry);
	}

	// 计算统计信息
	int64 TotalSize = 0;
	for (const FString& AssetPath : AssetPaths)
	{
		TotalSize += GetAssetSize(AssetPath, AssetDiskPaths);
	}

	Result.Chunks = MoveTemp(Chunks);
	Result.AssetToChunkMap = MoveTemp(AssetToChunk);
	Result.TotalAssetCount = AssetPaths.Num();
	Result.TotalChunkCount = Result.Chunks.Num();
	Result.TotalSize = TotalSize;
	Result.bSuccess = true;

	UE_LOG(LogHotUpdateEditor, Log, TEXT("Chunk 分析完成: 策略=%s, %d 个资源, %d 个 Chunk, 总大小 %lld 字节"),
		*UEnum::GetValueAsString(Config.ChunkStrategy),
		Result.TotalAssetCount, Result.TotalChunkCount, Result.TotalSize);

	return Result;
}

FHotUpdateChunkAnalysisResult FHotUpdateChunkManager::CreatePatchChunks(
	const TArray<FString>& ChangedAssets,
	const TMap<FString, FString>& AssetDiskPaths,
	const FHotUpdateChunkAnalysisConfig& Config)
{
	FHotUpdateChunkAnalysisResult Result;

	if (ChangedAssets.Num() == 0)
	{
		Result.bSuccess = true;
		Result.ErrorMessage = TEXT("没有变更资源");
		return Result;
	}

	// 创建单个 Patch Chunk
	TArray<FHotUpdateChunkDefinition> Chunks;
	TMap<FString, int32> AssetToChunk;

	// 使用 Patch Chunk ID 起始值
	int32 PatchChunkId = Config.PatchChunkIdStart;

	FHotUpdateChunkDefinition Chunk;
	Chunk.ChunkId = AllocateNextChunkId() + PatchChunkId;
	Chunk.ChunkName = TEXT("Patch");
	Chunk.AssetPaths = ChangedAssets;

	int64 ChunkSize = 0;
	for (const FString& AssetPath : ChangedAssets)
	{
		AssetToChunk.Add(AssetPath, Chunk.ChunkId);
		ChunkSize += GetAssetSize(AssetPath, AssetDiskPaths);
	}

	Chunk.UncompressedSize = ChunkSize;
	Chunk.CompressedSize = ChunkSize; // 简化处理

	Chunks.Add(Chunk);

	// 计算统计信息
	int64 TotalSize = 0;
	for (const FString& AssetPath : ChangedAssets)
	{
		TotalSize += GetAssetSize(AssetPath, AssetDiskPaths);
	}

	Result.Chunks = MoveTemp(Chunks);
	Result.AssetToChunkMap = MoveTemp(AssetToChunk);
	Result.TotalAssetCount = ChangedAssets.Num();
	Result.TotalChunkCount = Result.Chunks.Num();
	Result.TotalSize = TotalSize;
	Result.bSuccess = true;

	return Result;
}

bool FHotUpdateChunkManager::BuildDependencies(TArray<FHotUpdateChunkDefinition>& Chunks, const TMap<FString, int32>& AssetToChunk, IAssetRegistry* AssetRegistry)
{
	if (!AssetRegistry)
	{
		return false;
	}

	// 构建反向映射
	TMap<int32, int32> ChunkIdToIndex;
	for (int32 i = 0; i < Chunks.Num(); i++)
	{
		ChunkIdToIndex.Add(Chunks[i].ChunkId, i);
	}

	// 分析每个 Chunk 中资源的依赖
	for (int32 i = 0; i < Chunks.Num(); i++)
	{
		TSet<int32> DependentChunks;

		for (const FString& AssetPath : Chunks[i].AssetPaths)
		{
			TArray<FName> Dependencies;
			if (AssetRegistry->GetDependencies(FName(*AssetPath), Dependencies))
			{
				for (const FName& Dep : Dependencies)
				{
					FString DepStr = Dep.ToString();
					const int32* ChunkId = AssetToChunk.Find(DepStr);
					if (ChunkId && *ChunkId != Chunks[i].ChunkId)
					{
						DependentChunks.Add(*ChunkId);
					}
				}
			}
		}
	}

	return true;
}

int64 FHotUpdateChunkManager::GetAssetSize(const FString& AssetPath, const TMap<FString, FString>& AssetDiskPaths)
{
	const FString* DiskPath = AssetDiskPaths.Find(AssetPath);
	if (DiskPath && FPaths::FileExists(*DiskPath))
	{
		return IFileManager::Get().FileSize(**DiskPath);
	}
	return 0;
}

int32 FHotUpdateChunkManager::AllocateNextChunkId()
{
	FScopeLock Lock(&ChunkIdLock);
	return NextAutoChunkId++;
}

bool FHotUpdateChunkManager::DivideBySizeWithConfig(
	const TArray<FString>& AssetPaths,
	const TMap<FString, FString>& AssetDiskPaths,
	const FHotUpdateSizeBasedChunkConfig& Config,
	TArray<FHotUpdateChunkDefinition>& OutChunks,
	TMap<FString, int32>& OutAssetToChunk)
{
	int64 MaxChunkSize = static_cast<int64>(Config.MaxChunkSizeMB) * 1024 * 1024;
	if (MaxChunkSize <= 0)
	{
		MaxChunkSize = 256LL * 1024 * 1024; // 默认 256MB
	}

	TArray<FString> SortedAssets = AssetPaths;

	// 按大小排序（大的优先）
	if (Config.bSortBySize)
	{
		SortedAssets.Sort([&AssetDiskPaths](const FString& A, const FString& B)
		{
			int64 SizeA = GetAssetSize(A, AssetDiskPaths);
			int64 SizeB = GetAssetSize(B, AssetDiskPaths);
			return SizeA > SizeB;
		});
	}

	int32 ChunkIndex = 0;
	int64 CurrentChunkSize = 0;
	int32 BaseChunkId = Config.ChunkIdStart >= 0 ? Config.ChunkIdStart : AllocateNextChunkId();

	FHotUpdateChunkDefinition CurrentChunk;
	CurrentChunk.ChunkId = BaseChunkId;
	CurrentChunk.ChunkName = FString::Printf(TEXT("%s_%d"), *Config.ChunkNamePrefix, ChunkIndex);

	for (const FString& AssetPath : SortedAssets)
	{
		int64 AssetSize = GetAssetSize(AssetPath, AssetDiskPaths);

		// 检查是否需要新 Chunk
		if (CurrentChunkSize + AssetSize > MaxChunkSize && CurrentChunk.AssetPaths.Num() > 0)
		{
			CurrentChunk.UncompressedSize = CurrentChunkSize;
			CurrentChunk.CompressedSize = CurrentChunkSize;
			OutChunks.Add(CurrentChunk);

			// 开始新 Chunk
			ChunkIndex++;
			CurrentChunk = FHotUpdateChunkDefinition();
			CurrentChunk.ChunkId = BaseChunkId + ChunkIndex;
			CurrentChunk.ChunkName = FString::Printf(TEXT("%s_%d"), *Config.ChunkNamePrefix, ChunkIndex);
			CurrentChunkSize = 0;
		}

		CurrentChunk.AssetPaths.Add(AssetPath);
		OutAssetToChunk.Add(AssetPath, CurrentChunk.ChunkId);
		CurrentChunkSize += AssetSize;
	}

	// 添加最后一个 Chunk
	if (CurrentChunk.AssetPaths.Num() > 0)
	{
		CurrentChunk.UncompressedSize = CurrentChunkSize;
		CurrentChunk.CompressedSize = CurrentChunkSize;
		OutChunks.Add(CurrentChunk);
	}

	return OutChunks.Num() > 0;
}

bool FHotUpdateChunkManager::CreateSingleChunk(
	const TArray<FString>& AssetPaths,
	const TMap<FString, FString>& AssetDiskPaths,
	const FString& ChunkName,
	int32 ChunkId,
	TArray<FHotUpdateChunkDefinition>& OutChunks,
	TMap<FString, int32>& OutAssetToChunk)
{
	if (AssetPaths.Num() == 0)
	{
		return false;
	}

	FHotUpdateChunkDefinition Chunk;
	Chunk.ChunkId = ChunkId >= 0 ? ChunkId : AllocateNextChunkId();
	Chunk.ChunkName = ChunkName.IsEmpty() ? TEXT("Default") : ChunkName;
	Chunk.AssetPaths = AssetPaths;

	int64 TotalSize = 0;
	for (const FString& AssetPath : AssetPaths)
	{
		OutAssetToChunk.Add(AssetPath, Chunk.ChunkId);
		TotalSize += GetAssetSize(AssetPath, AssetDiskPaths);
	}

	Chunk.UncompressedSize = TotalSize;
	Chunk.CompressedSize = TotalSize;

	OutChunks.Add(Chunk);

	return true;
}