// Copyright czm. All Rights Reserved.

#include "Core/HotUpdateVersionStorage.h"
#include "Core/HotUpdateFileUtils.h"
#include "HotUpdate.h"
#include "HotUpdateManifest.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

UHotUpdateVersionStorage::UHotUpdateVersionStorage()
{
}

void UHotUpdateVersionStorage::Initialize(const FString& InStoragePath)
{
	StoragePath = InStoragePath;

	// 确保存储目录存在
	UHotUpdateFileUtils::EnsureDirectoryExists(StoragePath);
}

bool UHotUpdateVersionStorage::LoadLocalVersion(FHotUpdateVersionInfo& OutVersion)
{
	FString VersionFilePath = GetVersionFilePath();

	if (!IFileManager::Get().FileExists(*VersionFilePath))
	{
		UE_LOG(LogHotUpdate, Log, TEXT("Version file not found: %s"), *VersionFilePath);
		OutVersion = FHotUpdateVersionInfo();
		return false;
	}

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *VersionFilePath))
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("Failed to read version file: %s"), *VersionFilePath);
		return false;
	}

	// 解析 JSON
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("Failed to parse version file JSON"));
		return false;
	}

	// 解析版本信息
	JsonObject->TryGetNumberField(TEXT("majorVersion"), OutVersion.MajorVersion);
	JsonObject->TryGetNumberField(TEXT("minorVersion"), OutVersion.MinorVersion);
	JsonObject->TryGetNumberField(TEXT("patchVersion"), OutVersion.PatchVersion);
	JsonObject->TryGetNumberField(TEXT("buildNumber"), OutVersion.BuildNumber);
	JsonObject->TryGetStringField(TEXT("versionString"), OutVersion.VersionString);
	JsonObject->TryGetStringField(TEXT("platform"), OutVersion.Platform);
	JsonObject->TryGetNumberField(TEXT("timestamp"), OutVersion.Timestamp);

	UE_LOG(LogHotUpdate, Log, TEXT("Loaded local version: %s"), *OutVersion.ToString());
	return true;
}

bool UHotUpdateVersionStorage::SaveLocalVersion(const FHotUpdateVersionInfo& Version)
{
	FString VersionFilePath = GetVersionFilePath();

	// 确保目录存在
	UHotUpdateFileUtils::EnsureDirectoryExists(FPaths::GetPath(VersionFilePath));

	// 构建 JSON
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
	JsonObject->SetNumberField(TEXT("majorVersion"), Version.MajorVersion);
	JsonObject->SetNumberField(TEXT("minorVersion"), Version.MinorVersion);
	JsonObject->SetNumberField(TEXT("patchVersion"), Version.PatchVersion);
	JsonObject->SetNumberField(TEXT("buildNumber"), Version.BuildNumber);
	JsonObject->SetStringField(TEXT("versionString"), Version.VersionString);
	JsonObject->SetStringField(TEXT("platform"), Version.Platform);
	JsonObject->SetNumberField(TEXT("timestamp"), Version.Timestamp);

	// 序列化
	FString JsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

	// 保存文件
	if (FFileHelper::SaveStringToFile(JsonString, *VersionFilePath))
	{
		UE_LOG(LogHotUpdate, Log, TEXT("Saved local version: %s"), *Version.ToString());
		return true;
	}
	else
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Failed to save version file: %s"), *VersionFilePath);
		return false;
	}
}

bool UHotUpdateVersionStorage::LoadLocalManifest(FHotUpdateManifest& OutManifest)
{
	FString ManifestPath = GetManifestFilePath();

	if (!IFileManager::Get().FileExists(*ManifestPath))
	{
		UE_LOG(LogHotUpdate, Verbose, TEXT("Local manifest not found: %s"), *ManifestPath);
		return false;
	}

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *ManifestPath))
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("Failed to read local manifest: %s"), *ManifestPath);
		return false;
	}

	if (!UHotUpdateManifestParser::ParseFromJson(JsonString, OutManifest))
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("Failed to parse local manifest"));
		return false;
	}

	UE_LOG(LogHotUpdate, Log, TEXT("Loaded local manifest: version %s, %d containers"),
		*OutManifest.VersionInfo.ToString(), OutManifest.Containers.Num());
	return true;
}

bool UHotUpdateVersionStorage::SaveLocalManifest(const FHotUpdateManifest& Manifest)
{
	FString ManifestPath = GetManifestFilePath();

	if (UHotUpdateManifestParser::SaveToFile(ManifestPath, Manifest))
	{
		UE_LOG(LogHotUpdate, Log, TEXT("Saved local manifest: %s"), *ManifestPath);
		return true;
	}
	else
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Failed to save manifest: %s"), *ManifestPath);
		return false;
	}
}