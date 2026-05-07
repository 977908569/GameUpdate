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