// Copyright czm. All Rights Reserved.

#include "HotUpdateChunkManager.h"
#include "HotUpdateEditor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

int32 UHotUpdateChunkManager::NextAutoChunkId = 0;
FCriticalSection UHotUpdateChunkManager::ChunkIdLock;

UHotUpdateChunkManager::UHotUpdateChunkManager()
{
}

FHotUpdateChunkAnalysisResult UHotUpdateChunkManager::AnalyzeAndCreateChunks(
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

	case EHotUpdateChunkStrategy::Directory:
		{
			// 按目录分包
			TArray<FString> UnmatchedAssets;
			DivideByDirectory(AssetPaths, AssetDiskPaths, Config.DirectoryChunkRules, AssetRegistry, Chunks, AssetToChunk, UnmatchedAssets);

			// 未匹配的资源放入默认 Chunk
			if (UnmatchedAssets.Num() > 0)
			{
				int32 DefaultId = Config.DefaultChunkId >= 0 ? Config.DefaultChunkId : AllocateNextChunkId();
				CreateSingleChunk(UnmatchedAssets, AssetDiskPaths, Config.DefaultChunkName, DefaultId, Chunks, AssetToChunk);
			}
		}
		break;


	case EHotUpdateChunkStrategy::PrimaryAsset:
		{
			// UE5 标准分包
			bool bSuccess = DivideByPrimaryAsset(AssetPaths, AssetRegistry, Chunks, AssetToChunk);
			if (!bSuccess || Chunks.Num() == 0)
			{
				UE_LOG(LogHotUpdateEditor, Log, TEXT("PrimaryAsset 划分失败，回退到单 Chunk"));
				int32 DefaultId = Config.DefaultChunkId >= 0 ? Config.DefaultChunkId : AllocateNextChunkId();
				CreateSingleChunk(AssetPaths, AssetDiskPaths, TEXT("DefaultChunk"), DefaultId, Chunks, AssetToChunk);
			}

			// 如果配置了最大 Chunk 大小，进一步细分
			if (Config.MaxChunkSizeMB > 0)
			{
				TArray<FHotUpdateChunkDefinition> SizeBasedChunks;
				TMap<FString, int32> SizeBasedAssetToChunk;

				if (DivideBySize(AssetPaths, AssetDiskPaths, Config.MaxChunkSizeMB, SizeBasedChunks, SizeBasedAssetToChunk))
				{
					Chunks = MoveTemp(SizeBasedChunks);
					AssetToChunk = MoveTemp(SizeBasedAssetToChunk);
				}
			}
		}
		break;

	case EHotUpdateChunkStrategy::Hybrid:
		{
			// 混合模式：目录分包优先 + 其余按大小分包
			TArray<FString> UnmatchedAssets;
			DivideByDirectory(AssetPaths, AssetDiskPaths, Config.DirectoryChunkRules, AssetRegistry, Chunks, AssetToChunk, UnmatchedAssets);

			// 未匹配的资源按大小分包
			if (UnmatchedAssets.Num() > 0)
			{
				if (Config.SizeBasedConfig.MaxChunkSizeMB > 0)
				{
					TArray<FHotUpdateChunkDefinition> SizeChunks;
					TMap<FString, int32> SizeAssetToChunk;

					DivideBySizeWithConfig(UnmatchedAssets, AssetDiskPaths, Config.SizeBasedConfig, SizeChunks, SizeAssetToChunk);

					for (const FHotUpdateChunkDefinition& SizeChunk : SizeChunks)
					{
						Chunks.Add(SizeChunk);
					}
					for (const auto& Pair : SizeAssetToChunk)
					{
						AssetToChunk.Add(Pair.Key, Pair.Value);
					}
				}
				else
				{
					// 无大小限制时，放入默认 Chunk
					int32 DefaultId = Config.DefaultChunkId >= 0 ? Config.DefaultChunkId : AllocateNextChunkId();
					CreateSingleChunk(UnmatchedAssets, AssetDiskPaths, Config.DefaultChunkName, DefaultId, Chunks, AssetToChunk);
				}
			}
		}
		break;

	default:
		{
			// 默认使用 PrimaryAsset 策略
			bool bSuccess = DivideByPrimaryAsset(AssetPaths, AssetRegistry, Chunks, AssetToChunk);
			if (!bSuccess || Chunks.Num() == 0)
			{
				int32 DefaultId = Config.DefaultChunkId >= 0 ? Config.DefaultChunkId : AllocateNextChunkId();
			CreateSingleChunk(AssetPaths, AssetDiskPaths, TEXT("DefaultChunk"), DefaultId, Chunks, AssetToChunk);
			}
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

FHotUpdateChunkAnalysisResult UHotUpdateChunkManager::CreatePatchChunks(
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
	Chunk.Priority = 100;
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

bool UHotUpdateChunkManager::DivideByPrimaryAsset(
	const TArray<FString>& AssetPaths,
	IAssetRegistry* AssetRegistry,
	TArray<FHotUpdateChunkDefinition>& OutChunks,
	TMap<FString, int32>& OutAssetToChunk)
{
	// UE5 标准的 PrimaryAsset 划分
	// 按 Level、Character、Weapon 等 PrimaryAsset 类型分组

	if (!AssetRegistry)
	{
		return false;
	}

	// 收集 Primary Asset
	TMap<FString, TArray<FString>> PrimaryAssetGroups;

	for (const FString& AssetPath : AssetPaths)
	{
		FAssetData AssetData = AssetRegistry->GetAssetByObjectPath(FSoftObjectPath(AssetPath));
		if (!AssetData.IsValid())
		{
			continue;
		}

		// 检查是否是 Level（最重要的 PrimaryAsset）
		FString AssetType = AssetData.AssetClassPath.ToString();

		if (AssetType == TEXT("World") || AssetType == TEXT("Level"))
		{
			PrimaryAssetGroups.FindOrAdd(TEXT("Levels")).Add(AssetPath);
		}
		else if (AssetType == TEXT("Blueprint") || AssetType == TEXT("WidgetBlueprint"))
		{
			// Blueprint 可能是各种 PrimaryAsset
			PrimaryAssetGroups.FindOrAdd(TEXT("Blueprints")).Add(AssetPath);
		}
		else
		{
			// 其他资源按类型分组
			PrimaryAssetGroups.FindOrAdd(AssetType).Add(AssetPath);
		}
	}

	// 创建 Chunk
	for (const auto& Pair : PrimaryAssetGroups)
	{
		FHotUpdateChunkDefinition Chunk;
		Chunk.ChunkId = AllocateNextChunkId();
		Chunk.ChunkName = Pair.Key;
		Chunk.Priority = Pair.Key == TEXT("Levels") ? 0 : 10;
		Chunk.AssetPaths = Pair.Value;

		for (const FString& AssetPath : Pair.Value)
		{
			OutAssetToChunk.Add(AssetPath, Chunk.ChunkId);
		}

		OutChunks.Add(Chunk);
	}

	return OutChunks.Num() > 0;
}

bool UHotUpdateChunkManager::DivideBySize(
	const TArray<FString>& AssetPaths,
	const TMap<FString, FString>& AssetDiskPaths,
	int32 MaxChunkSizeMB,
	TArray<FHotUpdateChunkDefinition>& OutChunks,
	TMap<FString, int32>& OutAssetToChunk)
{
	int64 MaxChunkSize = static_cast<int64>(MaxChunkSizeMB) * 1024 * 1024;

	TArray<FString> SortedAssets = AssetPaths;
	// 按大小排序（大的优先）
	SortedAssets.Sort([&AssetDiskPaths](const FString& A, const FString& B)
	{
		int64 SizeA = GetAssetSize(A, AssetDiskPaths);
		int64 SizeB = GetAssetSize(B, AssetDiskPaths);
		return SizeA > SizeB;
	});

	TArray<FHotUpdateChunkDefinition> CurrentChunks;
	int32 CurrentChunkIndex = 0;
	int64 CurrentChunkSize = 0;

	FHotUpdateChunkDefinition CurrentChunk;
	CurrentChunk.ChunkId = AllocateNextChunkId();
	CurrentChunk.ChunkName = FString::Printf(TEXT("Chunk_%d"), CurrentChunk.ChunkId);
	CurrentChunk.Priority = 10;

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
			CurrentChunk = FHotUpdateChunkDefinition();
			CurrentChunk.ChunkId = AllocateNextChunkId();
			CurrentChunk.ChunkName = FString::Printf(TEXT("Chunk_%d"), CurrentChunk.ChunkId);
			CurrentChunk.Priority = 10;
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

bool UHotUpdateChunkManager::BuildDependencies(
	TArray<FHotUpdateChunkDefinition>& Chunks,
	const TMap<FString, int32>& AssetToChunk,
	IAssetRegistry* AssetRegistry)
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

		// 设置父 Chunk
		Chunks[i].ParentChunks = DependentChunks.Array();

		// 更新子 Chunk 映射
		for (int32 ParentChunkId : Chunks[i].ParentChunks)
		{
			const int32* ParentIndex = ChunkIdToIndex.Find(ParentChunkId);
			if (ParentIndex)
			{
				Chunks[*ParentIndex].ChildChunks.AddUnique(Chunks[i].ChunkId);
			}
		}
	}

	return true;
}

int64 UHotUpdateChunkManager::GetAssetSize(const FString& AssetPath, const TMap<FString, FString>& AssetDiskPaths)
{
	const FString* DiskPath = AssetDiskPaths.Find(AssetPath);
	if (DiskPath && FPaths::FileExists(*DiskPath))
	{
		return IFileManager::Get().FileSize(**DiskPath);
	}
	return 0;
}

int32 UHotUpdateChunkManager::AllocateNextChunkId()
{
	FScopeLock Lock(&ChunkIdLock);
	return NextAutoChunkId++;
}

bool UHotUpdateChunkManager::DivideBySizeWithConfig(
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
	CurrentChunk.Priority = 10;

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
			CurrentChunk.Priority = 10;
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

bool UHotUpdateChunkManager::DivideByDirectory(
	const TArray<FString>& AssetPaths,
	const TMap<FString, FString>& AssetDiskPaths,
	const TArray<FHotUpdateDirectoryChunkRule>& DirectoryRules,
	IAssetRegistry* AssetRegistry,
	TArray<FHotUpdateChunkDefinition>& OutChunks,
	TMap<FString, int32>& OutAssetToChunk,
	TArray<FString>& OutUnmatchedAssets)
{
	if (DirectoryRules.Num() == 0)
	{
		OutUnmatchedAssets = AssetPaths;
		return true;
	}

	// 记录已匹配的资源
	TSet<FString> MatchedAssets;

	// 为每个规则处理资源
	for (const FHotUpdateDirectoryChunkRule& Rule : DirectoryRules)
	{
		if (!Rule.IsValid())
		{
			continue;
		}

		TArray<FString> MatchedForThisRule;

		for (const FString& AssetPath : AssetPaths)
		{
			// 跳过已匹配的资源
			if (MatchedAssets.Contains(AssetPath))
			{
				continue;
			}

			// 检查是否匹配目录规则
			if (MatchesDirectoryRule(AssetPath, Rule))
			{
				// 检查是否在排除列表中
				bool bExcluded = false;
				for (const FString& ExcludedDir : Rule.ExcludedSubDirs)
				{
					if (AssetPath.StartsWith(ExcludedDir))
					{
						bExcluded = true;
						break;
					}
				}

				if (!bExcluded)
				{
					MatchedForThisRule.Add(AssetPath);
					MatchedAssets.Add(AssetPath);
				}
			}
		}

		// 为匹配的资源创建 Chunk
		if (MatchedForThisRule.Num() > 0)
		{
			// 如果设置了最大大小限制，需要进一步拆分
			if (Rule.MaxSizeMB > 0)
			{
				DivideDirectoryAssetsBySize(MatchedForThisRule, AssetDiskPaths, Rule, OutChunks, OutAssetToChunk);
			}
			else
			{
				// 创建单个 Chunk
				FHotUpdateChunkDefinition Chunk;
				Chunk.ChunkId = Rule.ChunkId >= 0 ? Rule.ChunkId : AllocateNextChunkId();
				Chunk.ChunkName = Rule.ChunkName;
				Chunk.Priority = Rule.Priority;
				Chunk.AssetPaths = MatchedForThisRule;

				int64 ChunkSize = 0;
				for (const FString& AssetPath : MatchedForThisRule)
				{
					OutAssetToChunk.Add(AssetPath, Chunk.ChunkId);
					ChunkSize += GetAssetSize(AssetPath, AssetDiskPaths);
				}

				Chunk.UncompressedSize = ChunkSize;
				Chunk.CompressedSize = ChunkSize;

				OutChunks.Add(Chunk);
			}
		}
	}

	// 收集未匹配的资源
	for (const FString& AssetPath : AssetPaths)
	{
		if (!MatchedAssets.Contains(AssetPath))
		{
			OutUnmatchedAssets.Add(AssetPath);
		}
	}

	return OutChunks.Num() > 0 || OutUnmatchedAssets.Num() > 0;
}

bool UHotUpdateChunkManager::MatchesDirectoryRule(const FString& AssetPath, const FHotUpdateDirectoryChunkRule& Rule)
{
	if (Rule.DirectoryPath.IsEmpty())
	{
		return false;
	}

	// 规范化路径
	FString NormalizedAssetPath = AssetPath;
	FString NormalizedRulePath = Rule.DirectoryPath;

	// 确保路径以 / 开头
	if (!NormalizedAssetPath.StartsWith(TEXT("/")))
	{
		NormalizedAssetPath = TEXT("/") + NormalizedAssetPath;
	}
	if (!NormalizedRulePath.StartsWith(TEXT("/")))
	{
		NormalizedRulePath = TEXT("/") + NormalizedRulePath;
	}

	// 确保规则路径以 / 结尾（便于前缀匹配）
	if (!NormalizedRulePath.EndsWith(TEXT("/")))
	{
		NormalizedRulePath += TEXT("/");
	}

	// 支持通配符匹配
	if (Rule.DirectoryPath.Contains(TEXT("*")))
	{
		// 简化的通配符匹配：* 匹配任意字符
		FString Pattern = Rule.DirectoryPath;
		Pattern.ReplaceInline(TEXT("*"), TEXT(""));

		// 检查是否以模式开头
		if (AssetPath.StartsWith(Pattern))
		{
			return true;
		}

		return false;
	}

	// 精确目录匹配
	if (Rule.bRecursive)
	{
		// 递归匹配：资源路径以目录路径开头
		return AssetPath.StartsWith(Rule.DirectoryPath);
	}
	else
	{
		// 非递归匹配：资源必须在目录下，但不在子目录中
		if (AssetPath.StartsWith(Rule.DirectoryPath))
		{
			FString RemainingPath = AssetPath.RightChop(Rule.DirectoryPath.Len());
			// 检查是否只有一个层级（不包含额外的 /）
			RemainingPath.RemoveFromEnd(TEXT(".uasset"));
			RemainingPath.RemoveFromEnd(TEXT(".umap"));
			return !RemainingPath.Contains(TEXT("/"));
		}
	}

	return false;
}

bool UHotUpdateChunkManager::DivideDirectoryAssetsBySize(
	const TArray<FString>& Assets,
	const TMap<FString, FString>& AssetDiskPaths,
	const FHotUpdateDirectoryChunkRule& Rule,
	TArray<FHotUpdateChunkDefinition>& OutChunks,
	TMap<FString, int32>& OutAssetToChunk)
{
	int64 MaxSize = static_cast<int64>(Rule.MaxSizeMB) * 1024 * 1024;
	if (MaxSize <= 0)
	{
		return false;
	}

	// 获取每个资源的大小并排序
	TArray<TPair<FString, int64>> AssetsWithSize;
	for (const FString& AssetPath : Assets)
	{
		int64 Size = GetAssetSize(AssetPath, AssetDiskPaths);
		AssetsWithSize.Add(TPair<FString, int64>(AssetPath, Size));
	}

	// 按大小排序（大的优先）
	AssetsWithSize.Sort([](const TPair<FString, int64>& A, const TPair<FString, int64>& B)
	{
		return A.Value > B.Value;
	});

	// 使用贪心算法分包
	int32 ChunkIndex = 0;
	int64 CurrentChunkSize = 0;
	int32 BaseChunkId = Rule.ChunkId >= 0 ? Rule.ChunkId : AllocateNextChunkId();

	FHotUpdateChunkDefinition CurrentChunk;
	CurrentChunk.ChunkId = BaseChunkId;
	CurrentChunk.ChunkName = FString::Printf(TEXT("%s_%d"), *Rule.ChunkName, ChunkIndex);
	CurrentChunk.Priority = Rule.Priority;

	for (const auto& AssetWithSize : AssetsWithSize)
	{
		const FString& AssetPath = AssetWithSize.Key;
		int64 AssetSize = AssetWithSize.Value;

		// 检查是否需要新 Chunk
		if (CurrentChunkSize + AssetSize > MaxSize && CurrentChunk.AssetPaths.Num() > 0)
		{
			CurrentChunk.UncompressedSize = CurrentChunkSize;
			CurrentChunk.CompressedSize = CurrentChunkSize;
			OutChunks.Add(CurrentChunk);

			// 开始新 Chunk
			ChunkIndex++;
			CurrentChunk = FHotUpdateChunkDefinition();
			CurrentChunk.ChunkId = BaseChunkId + ChunkIndex;
			CurrentChunk.ChunkName = FString::Printf(TEXT("%s_%d"), *Rule.ChunkName, ChunkIndex);
			CurrentChunk.Priority = Rule.Priority;
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

bool UHotUpdateChunkManager::CreateSingleChunk(
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
	Chunk.Priority = 0;
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