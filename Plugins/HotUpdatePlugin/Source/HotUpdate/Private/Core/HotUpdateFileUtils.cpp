// Copyright czm. All Rights Reserved.

#include "Core/HotUpdateFileUtils.h"
#include "HotUpdate.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "Misc/SecureHash.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

FString UHotUpdateFileUtils::CalculateFileHash(const FString& FilePath)
{
	TUniquePtr<FArchive> FileReader(IFileManager::Get().CreateFileReader(*FilePath));
	if (!FileReader)
	{
		return TEXT("");
	}

	constexpr int64 ChunkSize = 1024 * 1024; // 1MB per chunk
	TArray<uint8> Buffer;
	FSHA1 HashState;
	int64 TotalSize = FileReader->TotalSize();
	int64 Offset = 0;

	while (Offset < TotalSize)
	{
		int64 BytesToRead = FMath::Min(ChunkSize, TotalSize - Offset);
		Buffer.SetNumUninitialized(BytesToRead);
		FileReader->Serialize(Buffer.GetData(), BytesToRead);

		// 检查读取是否成功
		if (FileReader->IsError())
		{
			UE_LOG(LogHotUpdate, Error, TEXT("Failed to read file for hashing: %s (at offset %lld)"), *FilePath, Offset);
			return TEXT("");
		}

		HashState.Update(Buffer.GetData(), BytesToRead);
		Offset += BytesToRead;
	}

	HashState.Final();

	// 获取 Hash 值
	uint8 HashBytes[FSHA1::DigestSize];
	HashState.GetHash(HashBytes);

	// 转换为十六进制字符串
	return BytesToHex(HashBytes, FSHA1::DigestSize);
}

bool UHotUpdateFileUtils::EnsureDirectoryExists(const FString& DirectoryPath)
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	if (PlatformFile.DirectoryExists(*DirectoryPath))
	{
		return true;
	}

	return PlatformFile.CreateDirectoryTree(*DirectoryPath);
}

FString UHotUpdateFileUtils::BytesToHex(const uint8* Bytes, int32 Count)
{
	if (Bytes == nullptr || Count <= 0)
	{
		return TEXT("");
	}

	static const TCHAR HexDigits[] = TEXT("0123456789abcdef");

	FString Result;
	Result.Reserve(Count * 2);

	for (int32 i = 0; i < Count; i++)
	{
		Result.AppendChar(HexDigits[Bytes[i] >> 4]);
		Result.AppendChar(HexDigits[Bytes[i] & 0x0F]);
	}

	return Result;
}

bool UHotUpdateFileUtils::HexToBytes(const FString& HexString, TArray<uint8>& OutBytes)
{
	FString CleanHex = HexString;

	// 移除 0x 前缀
	if (CleanHex.StartsWith(TEXT("0x"), ESearchCase::IgnoreCase))
	{
		CleanHex = CleanHex.RightChop(2);
	}

	// 检查长度是否为偶数
	if (CleanHex.Len() % 2 != 0)
	{
		return false;
	}

	// 辅助函数：将十六进制字符转换为数值
	auto HexCharToValue = [](TCHAR C) -> int32 {
		if (C >= '0' && C <= '9') return C - '0';
		if (C >= 'a' && C <= 'f') return 10 + C - 'a';
		if (C >= 'A' && C <= 'F') return 10 + C - 'A';
		return -1;
	};

	int32 ByteCount = CleanHex.Len() / 2;
	OutBytes.SetNumUninitialized(ByteCount);

	for (int32 i = 0; i < ByteCount; i++)
	{
		int32 V1 = HexCharToValue(CleanHex[i * 2]);
		int32 V2 = HexCharToValue(CleanHex[i * 2 + 1]);
		if (V1 < 0 || V2 < 0)
		{
			OutBytes.Empty();
			return false;
		}
		OutBytes[i] = static_cast<uint8>((V1 << 4) | V2);
	}

	return true;
}

bool UHotUpdateFileUtils::IsEngineAsset(const FString& PackagePath)
{
	return PackagePath.Contains(TEXT("/Engine/"));
}

bool UHotUpdateFileUtils::ParseManifestFromJson(const FString& JsonString, FHotUpdateManifest& OutManifest)
{
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Failed to parse manifest JSON"));
		return false;
	}

	// 解析 packageKind（整数→枚举：0=Base, 1=Patch）
	double PackageKindValue = 0;
	if (JsonObject->TryGetNumberField(TEXT("packageKind"), PackageKindValue))
	{
		OutManifest.PackageKind = static_cast<int32>(PackageKindValue) == 1
			? EHotUpdatePackageKind::Patch
			: EHotUpdatePackageKind::Base;
	}

	// 解析版本信息
	const TSharedPtr<FJsonObject>* VersionObject;
	if (JsonObject->TryGetObjectField(TEXT("version"), VersionObject))
	{
		(*VersionObject)->TryGetStringField(TEXT("version"), OutManifest.VersionInfo.VersionString);
		(*VersionObject)->TryGetStringField(TEXT("platform"), OutManifest.VersionInfo.Platform);
		(*VersionObject)->TryGetNumberField(TEXT("timestamp"), OutManifest.VersionInfo.Timestamp);

		// 从版本字符串解析整数版本号
		if (!OutManifest.VersionInfo.VersionString.IsEmpty())
		{
			FHotUpdateVersionInfo Parsed = FHotUpdateVersionInfo::FromString(OutManifest.VersionInfo.VersionString);
			OutManifest.VersionInfo.MajorVersion = Parsed.MajorVersion;
			OutManifest.VersionInfo.MinorVersion = Parsed.MinorVersion;
			OutManifest.VersionInfo.PatchVersion = Parsed.PatchVersion;
			OutManifest.VersionInfo.BuildNumber = Parsed.BuildNumber;
			OutManifest.VersionInfo.bIsValid = Parsed.bIsValid;
		}
	}

	// 解析基础版本号
	JsonObject->TryGetStringField(TEXT("baseVersion"), OutManifest.BaseVersion);

	// 解析 containers 数组
	const TArray<TSharedPtr<FJsonValue>>* ContainersArray;
	if (JsonObject->TryGetArrayField(TEXT("containers"), ContainersArray))
	{
		OutManifest.Containers.Empty();
		for (const TSharedPtr<FJsonValue>& ContainerValue : *ContainersArray)
		{
			TSharedPtr<FJsonObject> ContainerObject = ContainerValue->AsObject();
			if (!ContainerObject.IsValid()) continue;

			FHotUpdateContainerInfo Container;
			ContainerObject->TryGetStringField(TEXT("containerName"), Container.ContainerName);

			// IoStore 格式字段
			ContainerObject->TryGetStringField(TEXT("utocPath"), Container.UtocFile.Path);
			ContainerObject->TryGetNumberField(TEXT("utocSize"), Container.UtocFile.Size);
			ContainerObject->TryGetStringField(TEXT("utocHash"), Container.UtocFile.Hash);
			ContainerObject->TryGetStringField(TEXT("ucasPath"), Container.UcasFile.Path);
			ContainerObject->TryGetNumberField(TEXT("ucasSize"), Container.UcasFile.Size);
			ContainerObject->TryGetStringField(TEXT("ucasHash"), Container.UcasFile.Hash);

			// 传统 Pak 格式字段
			ContainerObject->TryGetStringField(TEXT("pakPath"), Container.PakFile.Path);
			ContainerObject->TryGetNumberField(TEXT("pakSize"), Container.PakFile.Size);
			ContainerObject->TryGetStringField(TEXT("pakHash"), Container.PakFile.Hash);

			// 解析容器类型（字符串格式：base_xxx / patch_xxx）
			FString ContainerTypeStr;
			if (ContainerObject->TryGetStringField(TEXT("containerType"), ContainerTypeStr))
			{
				if (ContainerTypeStr.StartsWith(TEXT("base")))
				{
					Container.ContainerType = EHotUpdateContainerType::Base;
				}
				else if (ContainerTypeStr.StartsWith(TEXT("patch")))
				{
					Container.ContainerType = EHotUpdateContainerType::Patch;
				}
			}

			ContainerObject->TryGetStringField(TEXT("version"), Container.Version);
			OutManifest.Containers.Add(Container);
		}
	}

	UE_LOG(LogHotUpdate, Log, TEXT("Parsed manifest: version %s, %d containers"),
		*OutManifest.VersionInfo.VersionString,
		OutManifest.Containers.Num());

	return true;
}

bool UHotUpdateFileUtils::SaveManifestToFile(const FString& FilePath, const FHotUpdateManifest& Manifest)
{
	FString JsonString = ManifestToJsonString(Manifest);
	if (JsonString.IsEmpty()) return false;

	FString Directory = FPaths::GetPath(FilePath);
	EnsureDirectoryExists(Directory);

	return FFileHelper::SaveStringToFile(JsonString, *FilePath);
}

FString UHotUpdateFileUtils::ManifestToJsonString(const FHotUpdateManifest& Manifest)
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject());

	// packageKind（枚举→整数：Base=0, Patch=1）
	JsonObject->SetNumberField(TEXT("packageKind"),
		Manifest.PackageKind == EHotUpdatePackageKind::Patch ? 1 : 0);

	// 版本信息
	TSharedPtr<FJsonObject> VersionObject = MakeShareable(new FJsonObject());
	VersionObject->SetStringField(TEXT("version"), Manifest.VersionInfo.VersionString);
	VersionObject->SetStringField(TEXT("platform"), Manifest.VersionInfo.Platform);
	VersionObject->SetNumberField(TEXT("timestamp"), Manifest.VersionInfo.Timestamp);
	JsonObject->SetObjectField(TEXT("version"), VersionObject);

	// 基础版本号
	if (!Manifest.BaseVersion.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("baseVersion"), Manifest.BaseVersion);
	}

	// containers 数组
	TArray<TSharedPtr<FJsonValue>> ContainersArray;
	for (const FHotUpdateContainerInfo& Container : Manifest.Containers)
	{
		TSharedPtr<FJsonObject> ContainerObject = MakeShareable(new FJsonObject());
		ContainerObject->SetStringField(TEXT("containerName"), Container.ContainerName);

		// IoStore 格式字段
		if (!Container.UtocFile.Path.IsEmpty())
		{
			ContainerObject->SetStringField(TEXT("utocPath"), Container.UtocFile.Path);
			ContainerObject->SetNumberField(TEXT("utocSize"), Container.UtocFile.Size);
			ContainerObject->SetStringField(TEXT("utocHash"), Container.UtocFile.Hash);
		}
		if (!Container.UcasFile.Path.IsEmpty())
		{
			ContainerObject->SetStringField(TEXT("ucasPath"), Container.UcasFile.Path);
			ContainerObject->SetNumberField(TEXT("ucasSize"), Container.UcasFile.Size);
			ContainerObject->SetStringField(TEXT("ucasHash"), Container.UcasFile.Hash);
		}

		// 传统 Pak 格式字段
		if (!Container.PakFile.Path.IsEmpty())
		{
			ContainerObject->SetStringField(TEXT("pakPath"), Container.PakFile.Path);
			ContainerObject->SetNumberField(TEXT("pakSize"), Container.PakFile.Size);
			ContainerObject->SetStringField(TEXT("pakHash"), Container.PakFile.Hash);
		}

		// 容器类型
		ContainerObject->SetStringField(TEXT("containerType"),
			Container.ContainerType == EHotUpdateContainerType::Base ? TEXT("base") : TEXT("patch"));

		ContainerObject->SetStringField(TEXT("version"), Container.Version);

		ContainersArray.Add(MakeShareable(new FJsonValueObject(ContainerObject)));
	}
	JsonObject->SetArrayField(TEXT("containers"), ContainersArray);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

	return OutputString;
}