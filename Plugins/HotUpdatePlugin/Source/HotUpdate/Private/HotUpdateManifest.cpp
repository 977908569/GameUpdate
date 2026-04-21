// Copyright czm. All Rights Reserved.

#include "HotUpdateManifest.h"
#include "HotUpdate.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool UHotUpdateManifestParser::ParseFromJson(const FString& JsonString, FHotUpdateManifest& OutManifest)
{
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Failed to parse manifest JSON"));
		return false;
	}

	// 解析 manifestVersion
	JsonObject->TryGetNumberField(TEXT("manifestVersion"), OutManifest.ManifestVersion);

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
		}
	}

	// 解析基础版本号
	JsonObject->TryGetStringField(TEXT("baseVersion"), OutManifest.BaseVersion);

	// 解析 containers/chunks 数组（兼容两种字段名）
	const TArray<TSharedPtr<FJsonValue>>* ContainersArray;
	if (JsonObject->TryGetArrayField(TEXT("containers"), ContainersArray) ||
		JsonObject->TryGetArrayField(TEXT("chunks"), ContainersArray))
	{
		OutManifest.Containers.Empty();
		for (const TSharedPtr<FJsonValue>& ContainerValue : *ContainersArray)
		{
			TSharedPtr<FJsonObject> ContainerObject = ContainerValue->AsObject();
			if (!ContainerObject.IsValid()) continue;

			FHotUpdateContainerInfo Container;
			ContainerObject->TryGetStringField(TEXT("containerName"), Container.ContainerName);
			ContainerObject->TryGetStringField(TEXT("ChunkName"), Container.ContainerName);  // 兼容 ChunkName

			// IoStore 格式字段
			ContainerObject->TryGetStringField(TEXT("utocPath"), Container.UtocPath);
			ContainerObject->TryGetNumberField(TEXT("utocSize"), Container.UtocSize);
			ContainerObject->TryGetStringField(TEXT("utocHash"), Container.UtocHash);
			ContainerObject->TryGetStringField(TEXT("ucasPath"), Container.UcasPath);
			ContainerObject->TryGetNumberField(TEXT("ucasSize"), Container.UcasSize);
			ContainerObject->TryGetStringField(TEXT("ucasHash"), Container.UcasHash);

			// 传统 Pak 格式字段
			ContainerObject->TryGetStringField(TEXT("pakPath"), Container.PakPath);
			ContainerObject->TryGetNumberField(TEXT("pakSize"), Container.PakSize);
			ContainerObject->TryGetStringField(TEXT("pakHash"), Container.PakHash);

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

			// 解析 ChunkId
			ContainerObject->TryGetNumberField(TEXT("chunkId"), Container.ChunkId);

			ContainerObject->TryGetStringField(TEXT("version"), Container.Version);
			OutManifest.Containers.Add(Container);
		}
	}

	UE_LOG(LogHotUpdate, Log, TEXT("Parsed manifest: version %s, %d containers"),
		*OutManifest.VersionInfo.VersionString,
		OutManifest.Containers.Num());

	return true;
}

bool UHotUpdateManifestParser::SaveToFile(const FString& FilePath, const FHotUpdateManifest& Manifest)
{
	FString JsonString = ToJsonString(Manifest);
	if (JsonString.IsEmpty()) return false;

	FString Directory = FPaths::GetPath(FilePath);
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*Directory))
	{
		PlatformFile.CreateDirectoryTree(*Directory);
	}

	return FFileHelper::SaveStringToFile(JsonString, *FilePath);
}

FString UHotUpdateManifestParser::ToJsonString(const FHotUpdateManifest& Manifest)
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject());

	// manifestVersion
	JsonObject->SetNumberField(TEXT("manifestVersion"), Manifest.ManifestVersion);

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
		if (!Container.UtocPath.IsEmpty())
		{
			ContainerObject->SetStringField(TEXT("utocPath"), Container.UtocPath);
			ContainerObject->SetNumberField(TEXT("utocSize"), Container.UtocSize);
			ContainerObject->SetStringField(TEXT("utocHash"), Container.UtocHash);
		}
		if (!Container.UcasPath.IsEmpty())
		{
			ContainerObject->SetStringField(TEXT("ucasPath"), Container.UcasPath);
			ContainerObject->SetNumberField(TEXT("ucasSize"), Container.UcasSize);
			ContainerObject->SetStringField(TEXT("ucasHash"), Container.UcasHash);
		}

		// 传统 Pak 格式字段
		if (!Container.PakPath.IsEmpty())
		{
			ContainerObject->SetStringField(TEXT("pakPath"), Container.PakPath);
			ContainerObject->SetNumberField(TEXT("pakSize"), Container.PakSize);
			ContainerObject->SetStringField(TEXT("pakHash"), Container.PakHash);
		}

		// 容器类型
		ContainerObject->SetStringField(TEXT("containerType"),
			Container.ContainerType == EHotUpdateContainerType::Base ? TEXT("base") : TEXT("patch"));

		// Chunk ID
		ContainerObject->SetNumberField(TEXT("chunkId"), Container.ChunkId);
		ContainerObject->SetStringField(TEXT("version"), Container.Version);

		ContainersArray.Add(MakeShareable(new FJsonValueObject(ContainerObject)));
	}
	JsonObject->SetArrayField(TEXT("containers"), ContainersArray);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

	return OutputString;
}