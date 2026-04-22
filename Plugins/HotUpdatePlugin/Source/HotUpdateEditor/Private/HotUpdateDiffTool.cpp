// Copyright czm. All Rights Reserved.

#include "HotUpdateDiffTool.h"
#include "HotUpdateEditor.h"
#include "Core/HotUpdateFileUtils.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "JsonObjectConverter.h"
#include "IPlatformFilePak.h"

FHotUpdateDiffReport FHotUpdateDiffTool::CompareManifests(
	const FString& BaseManifestPath,
	const FString& TargetManifestPath)
{
	UE_LOG(LogHotUpdateEditor, Log, TEXT("CompareManifests: Base='%s', Target='%s'"),
		*BaseManifestPath, *TargetManifestPath);

	FHotUpdateDiffReport Report;
	Report.BaseVersion = BaseManifestPath;
	Report.TargetVersion = TargetManifestPath;

	// 解析基础Manifest
	TMap<FString, FHotUpdateManifestEntry> BaseEntries;
	if (!ParseManifestFile(BaseManifestPath, BaseEntries))
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("无法解析基础Manifest: %s"), *BaseManifestPath);
		return Report;
	}

	// 解析目标Manifest
	TMap<FString, FHotUpdateManifestEntry> TargetEntries;
	if (!ParseManifestFile(TargetManifestPath, TargetEntries))
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("无法解析目标Manifest: %s"), *TargetManifestPath);
		return Report;
	}

	// 收集所有路径
	TSet<FString> AllPaths;
	for (const auto& Pair : BaseEntries)
	{
		AllPaths.Add(Pair.Key);
	}
	for (const auto& Pair : TargetEntries)
	{
		AllPaths.Add(Pair.Key);
	}

	// 比较差异
	for (const FString& Path : AllPaths)
	{
		bool bInBase = BaseEntries.Contains(Path);
		bool bInTarget = TargetEntries.Contains(Path);

		FHotUpdateAssetDiff Diff;
		Diff.AssetPath = Path;

		if (!bInBase && bInTarget)
		{
			const FHotUpdateManifestEntry& Entry = TargetEntries[Path];
			Diff.ChangeType = EHotUpdateFileChangeType::Added;
			Diff.NewSize = Entry.FileSize;
			Diff.NewHash = Entry.FileHash;
			Diff.AssetType = GetAssetTypeFromExtension(FPaths::GetExtension(Path));
			Diff.ChangeDescription = FString::Printf(TEXT("新增资源 (%s)"), *FormatFileSize(Diff.NewSize));
			Report.AddedAssets.Add(Diff);
		}
		else if (bInBase && !bInTarget)
		{
			const FHotUpdateManifestEntry& Entry = BaseEntries[Path];
			Diff.ChangeType = EHotUpdateFileChangeType::Deleted;
			Diff.OldSize = Entry.FileSize;
			Diff.OldHash = Entry.FileHash;
			Diff.AssetType = GetAssetTypeFromExtension(FPaths::GetExtension(Path));
			Diff.ChangeDescription = FString::Printf(TEXT("删除资源 (%s)"), *FormatFileSize(Diff.OldSize));
			Report.DeletedAssets.Add(Diff);
		}
		else if (bInBase && bInTarget)
		{
			const FHotUpdateManifestEntry& BaseEntry = BaseEntries[Path];
			const FHotUpdateManifestEntry& TargetEntry = TargetEntries[Path];

			if (BaseEntry.FileHash != TargetEntry.FileHash)
			{
				Diff.ChangeType = EHotUpdateFileChangeType::Modified;
				Diff.OldSize = BaseEntry.FileSize;
				Diff.OldHash = BaseEntry.FileHash;
				Diff.NewSize = TargetEntry.FileSize;
				Diff.NewHash = TargetEntry.FileHash;
				Diff.AssetType = GetAssetTypeFromExtension(FPaths::GetExtension(Path));

				int64 SizeDiff = Diff.NewSize - Diff.OldSize;
				FString SizeDiffStr = SizeDiff >= 0
					? FString::Printf(TEXT("+%s"), *FormatFileSize(SizeDiff))
					: FString::Printf(TEXT("-%s"), *FormatFileSize(-SizeDiff));

				Diff.ChangeDescription = FString::Printf(TEXT("已修改 (大小变化: %s)"), *SizeDiffStr);
				Report.ModifiedAssets.Add(Diff);
			}
			else
			{
				Diff.ChangeType = EHotUpdateFileChangeType::Unchanged;
				Diff.ChangeDescription = TEXT("未变更");
				Report.UnchangedAssets.Add(Diff);
			}
		}
	}

	// 从 manifest JSON 中提取版本信息
	auto ExtractVersionFromManifest = [](const FString& ManifestPath) -> FString {
		FString JsonString;
		if (!FFileHelper::LoadFileToString(JsonString, *ManifestPath)) return TEXT("");

		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
		if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid()) return TEXT("");

		// 新格式：version.version
		const TSharedPtr<FJsonObject>* VersionObj;
		if (JsonObject->TryGetObjectField(TEXT("version"), VersionObj))
		{
			if (VersionObj->Get()->HasField(TEXT("version")))
			{
				return VersionObj->Get()->GetStringField(TEXT("version"));
			}
		}
		// 旧格式：versionInfo.versionString
		const TSharedPtr<FJsonObject>* VersionInfoObj;
		if (JsonObject->TryGetObjectField(TEXT("versionInfo"), VersionInfoObj))
		{
			if (VersionInfoObj->Get()->HasField(TEXT("versionString")))
			{
				return VersionInfoObj->Get()->GetStringField(TEXT("versionString"));
			}
		}
		return TEXT("");
	};

	FString BaseVersionStr = ExtractVersionFromManifest(BaseManifestPath);
	FString TargetVersionStr = ExtractVersionFromManifest(TargetManifestPath);

	if (!BaseVersionStr.IsEmpty()) Report.BaseVersion = BaseVersionStr;
	if (!TargetVersionStr.IsEmpty()) Report.TargetVersion = TargetVersionStr;

	UE_LOG(LogHotUpdateEditor, Log, TEXT("CompareManifests result: Added=%d, Modified=%d, Deleted=%d, Unchanged=%d (Base=%s, Target=%s)"),
		Report.AddedAssets.Num(), Report.ModifiedAssets.Num(), Report.DeletedAssets.Num(), Report.UnchangedAssets.Num(),
		*Report.BaseVersion, *Report.TargetVersion);

	OnDiffComplete.Broadcast(Report);
	return Report;
}


FName FHotUpdateDiffTool::GetAssetIconName(const FString& AssetPath)
{
	FString Extension = FPaths::GetExtension(AssetPath).ToLower();

	static TMap<FString, FName> ExtensionToIcon;
	if (ExtensionToIcon.Num() == 0)
	{
		ExtensionToIcon.Add(TEXT("uasset"), FName("ClassIcon.BlueprintCore"));
		ExtensionToIcon.Add(TEXT("umap"), FName("ClassIcon.World"));
		ExtensionToIcon.Add(TEXT("png"), FName("ClassIcon.Texture2D"));
		ExtensionToIcon.Add(TEXT("tga"), FName("ClassIcon.Texture2D"));
		ExtensionToIcon.Add(TEXT("jpg"), FName("ClassIcon.Texture2D"));
		ExtensionToIcon.Add(TEXT("wav"), FName("ClassIcon.SoundWave"));
		ExtensionToIcon.Add(TEXT("fbx"), FName("ClassIcon.StaticMesh"));
		ExtensionToIcon.Add(TEXT("obj"), FName("ClassIcon.StaticMesh"));
	}

	if (ExtensionToIcon.Contains(Extension))
	{
		return ExtensionToIcon[Extension];
	}

	return FName("ClassIcon.Object");
}

void FHotUpdateDiffTool::ScanDirectory(
	const FString& Directory,
	bool bIncludeHiddenFiles,
	TMap<FString, FHotUpdateAssetDiff>& OutAssets)
{
	// 规范化目录路径：统一分隔符，移除尾部斜杠
	FString NormalizedDir = Directory;
	FPaths::NormalizeDirectoryName(NormalizedDir);
	// 统一使用正斜杠，确保与 FindFilesRecursive 返回的路径格式匹配
	NormalizedDir.ReplaceInline(TEXT("\\"), TEXT("/"));

	IPlatformFile& PlatformFile = IPlatformFile::GetPlatformPhysical();

	TArray<FString> FoundFiles;
	// 注意：FindFilesRecursive 始终递归搜索，第 6 个参数是 bFindHidden（是否包含隐藏文件）
	IFileManager::Get().FindFilesRecursive(FoundFiles, *NormalizedDir, TEXT("*.*"), true, false, bIncludeHiddenFiles);

	UE_LOG(LogHotUpdateEditor, Log, TEXT("ScanDirectory: Scanning '%s' (normalized from '%s'), found %d files"),
		*NormalizedDir, *Directory, FoundFiles.Num());

	for (const FString& File : FoundFiles)
	{
		// 使用字符串截取获取相对路径，而非 MakePathRelativeTo
		// 因为 MakePathRelativeTo 内部会取 InRelativeTo 的父目录，导致相对路径包含目录名本身
		FString RelativePath = File;
		// 统一使用正斜杠，确保与 NormalizedDir 的 StartsWith 匹配
		RelativePath.ReplaceInline(TEXT("\\"), TEXT("/"));
		if (RelativePath.StartsWith(NormalizedDir))
		{
			RelativePath = RelativePath.RightChop(NormalizedDir.Len());
			// 移除开头的路径分隔符
			while (RelativePath.Len() > 0 && (RelativePath[0] == TEXT('/') || RelativePath[0] == TEXT('\\')))
			{
				RelativePath.RightChopInline(1);
			}
		}
		else
		{
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("ScanDirectory: File '%s' does not start with dir '%s', using full path"), *File, *NormalizedDir);
		}

		UE_LOG(LogHotUpdateEditor, Verbose, TEXT("ScanDirectory: RelativePath='%s' (from '%s')"), *RelativePath, *File);

		FHotUpdateAssetDiff Diff;
		Diff.AssetPath = RelativePath;
		Diff.NewSize = PlatformFile.FileSize(*File);
		Diff.NewHash = UHotUpdateFileUtils::CalculateFileHash(File);
		Diff.AssetType = GetAssetTypeFromExtension(FPaths::GetExtension(File));

		OutAssets.Add(RelativePath, Diff);
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("ScanDirectory: Added %d assets from '%s'"), OutAssets.Num(), *NormalizedDir);
}

bool FHotUpdateDiffTool::ParseManifestFile(
	const FString& ManifestPath,
	TMap<FString, FHotUpdateManifestEntry>& OutEntries)
{
	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *ManifestPath))
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("ParseManifestFile: Cannot load file '%s'"), *ManifestPath);
		return false;
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("ParseManifestFile: Cannot parse JSON '%s"), *ManifestPath);
		return false;
	}

	// 优先解析 files 数组（filemanifest.json 格式）
	const TArray<TSharedPtr<FJsonValue>>* FilesArray;
	if (JsonObject->TryGetArrayField(TEXT("files"), FilesArray))
	{
		for (const TSharedPtr<FJsonValue>& FileValue : *FilesArray)
		{
			TSharedPtr<FJsonObject> FileObj = FileValue->AsObject();
			if (!FileObj.IsValid()) continue;

			FHotUpdateManifestEntry Entry;
			Entry.FilePath = FileObj->GetStringField(TEXT("filePath"));
			Entry.FileSize = (int64)FileObj->GetNumberField(TEXT("fileSize"));
			Entry.FileHash = FileObj->GetStringField(TEXT("fileHash"));

			// 可选字段
			if (FileObj->HasField(TEXT("chunkId")))
			{
				Entry.ChunkId = FileObj->GetIntegerField(TEXT("chunkId"));
			}

			OutEntries.Add(Entry.FilePath, Entry);
		}

		UE_LOG(LogHotUpdateEditor, Log, TEXT("ParseManifestFile: Parsed %d entries from 'files' array in '%s'"),
			OutEntries.Num(), *ManifestPath);
		return true;
	}

	// 兼容旧格式 assets 数组
	const TArray<TSharedPtr<FJsonValue>>* AssetsArray;
	if (JsonObject->TryGetArrayField(TEXT("assets"), AssetsArray))
	{
		for (const TSharedPtr<FJsonValue>& AssetValue : *AssetsArray)
		{
			TSharedPtr<FJsonObject> AssetObj = AssetValue->AsObject();
			if (!AssetObj.IsValid()) continue;

			FHotUpdateManifestEntry Entry;
			Entry.FilePath = AssetObj->GetStringField(TEXT("path"));
			Entry.FileSize = (int64)AssetObj->GetNumberField(TEXT("size"));
			Entry.FileHash = AssetObj->GetStringField(TEXT("hash"));

			OutEntries.Add(Entry.FilePath, Entry);
		}

		UE_LOG(LogHotUpdateEditor, Log, TEXT("ParseManifestFile: Parsed %d entries from 'assets' array (legacy format) in '%s'"),
			OutEntries.Num(), *ManifestPath);
		return true;
	}

	UE_LOG(LogHotUpdateEditor, Warning, TEXT("ParseManifestFile: No 'files' or 'assets' array found in '%s'"), *ManifestPath);
	return false;
}

TMap<FString, FString> FHotUpdateDiffTool::GetPakFileHashes(const FString& PakPath)
{
	TMap<FString, FString> FileHashes;

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.FileExists(*PakPath))
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("Pak file not found: %s"), *PakPath);
		return FileHashes;
	}

	TRefCountPtr<FPakFile> PakFile = new FPakFile(&PlatformFile, *PakPath, false);
	if (!PakFile.IsValid() || !PakFile->IsValid())
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("Failed to open Pak file: %s"), *PakPath);
		return FileHashes;
	}

	// 使用 FFilenameIterator 遍历 Pak 条目
	for (FPakFile::FFilenameIterator It(*PakFile); It; ++It)
	{
		const FPakEntry& Entry = It.Info();
		FString Hash = UHotUpdateFileUtils::BytesToHex(Entry.Hash, sizeof(Entry.Hash));
		FileHashes.Add(It.Filename(), Hash);
	}

	// 降级方案：使用 FPakEntryIterator
	if (FileHashes.Num() == 0 && PakFile->GetNumFiles() > 0)
	{
		for (FPakFile::FPakEntryIterator It(*PakFile); It; ++It)
		{
			const FString* Filename = It.TryGetFilename();
			if (Filename && !Filename->IsEmpty())
			{
				const FPakEntry& Entry = It.Info();
				FString Hash = UHotUpdateFileUtils::BytesToHex(Entry.Hash, sizeof(Entry.Hash));
				FileHashes.Add(*Filename, Hash);
			}
		}
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("Extracted %d file hashes from Pak: %s"), FileHashes.Num(), *PakPath);

	return FileHashes;
}

FString FHotUpdateDiffTool::GetAssetTypeFromExtension(const FString& Extension)
{
	FString Ext = Extension.ToLower();

	static TMap<FString, FString> ExtensionToType;
	if (ExtensionToType.Num() == 0)
	{
		ExtensionToType.Add(TEXT("uasset"), TEXT("Asset"));
		ExtensionToType.Add(TEXT("umap"), TEXT("Map"));
		ExtensionToType.Add(TEXT("png"), TEXT("Texture"));
		ExtensionToType.Add(TEXT("tga"), TEXT("Texture"));
		ExtensionToType.Add(TEXT("jpg"), TEXT("Texture"));
		ExtensionToType.Add(TEXT("wav"), TEXT("Sound"));
		ExtensionToType.Add(TEXT("fbx"), TEXT("Mesh"));
		ExtensionToType.Add(TEXT("obj"), TEXT("Mesh"));
		ExtensionToType.Add(TEXT("pak"), TEXT("Pak"));
		ExtensionToType.Add(TEXT("utoc"), TEXT("IoStore"));
		ExtensionToType.Add(TEXT("ucas"), TEXT("IoStore"));
	}

	return ExtensionToType.Contains(Ext) ? ExtensionToType[Ext] : TEXT("Unknown");
}

FString FHotUpdateDiffTool::FormatFileSize(int64 Size)
{
	if (Size < 1024)
	{
		return FString::Printf(TEXT("%lld B"), Size);
	}
	else if (Size < 1024 * 1024)
	{
		return FString::Printf(TEXT("%.2f KB"), Size / 1024.0);
	}
	else if (Size < 1024 * 1024 * 1024)
	{
		return FString::Printf(TEXT("%.2f MB"), Size / (1024.0 * 1024.0));
	}
	else
	{
		return FString::Printf(TEXT("%.2f GB"), Size / (1024.0 * 1024.0 * 1024.0));
	}
}

FString FHotUpdateDiffTool::FindFileManifestPath(const FString& VersionDirectory)
{
	// 规范化目录路径
	FString NormalizedDir = VersionDirectory;
	FPaths::NormalizeDirectoryName(NormalizedDir);
	NormalizedDir.ReplaceInline(TEXT("\\"), TEXT("/"));

	// 查找顺序
	TArray<FString> SearchPaths = {
		NormalizedDir / TEXT("Windows") / TEXT("filemanifest.json"),
		NormalizedDir / TEXT("filemanifest.json"),
		NormalizedDir / TEXT("Windows") / TEXT("manifest.json"),
		NormalizedDir / TEXT("manifest.json")
	};

	for (const FString& SearchPath : SearchPaths)
	{
		if (FPaths::FileExists(SearchPath))
		{
			UE_LOG(LogHotUpdateEditor, Log, TEXT("FindFileManifestPath: Found manifest at '%s'"), *SearchPath);
			return SearchPath;
		}
	}

	UE_LOG(LogHotUpdateEditor, Warning, TEXT("FindFileManifestPath: No manifest found in '%s'"), *NormalizedDir);
	return TEXT("");
}