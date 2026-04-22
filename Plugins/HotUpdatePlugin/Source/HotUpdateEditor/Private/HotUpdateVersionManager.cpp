// Copyright czm. All Rights Reserved.

#include "HotUpdateVersionManager.h"
#include "HotUpdateEditor.h"
#include "HotUpdateUtils.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "JsonObjectConverter.h"

bool FHotUpdateVersionManager::RegisterVersion(const FHotUpdateEditorVersionInfo& VersionInfo)
{
	if (VersionInfo.VersionString.IsEmpty())
	{
		return false;
	}

	FScopeLock Lock(&RegistryLock);

	// 加载注册表
	if (!bRegistryLoaded)
	{
		LoadVersionRegistry();
	}

	// 添加或更新版本
	TMap<EHotUpdatePlatform, FHotUpdateEditorVersionInfo>& PlatformMap = VersionRegistry.FindOrAdd(VersionInfo.VersionString);
	PlatformMap.Add(VersionInfo.Platform, VersionInfo);

	// 保存注册表
	return SaveVersionRegistry();
}

bool FHotUpdateVersionManager::UnregisterVersion(const FString& VersionString, EHotUpdatePlatform Platform)
{
	if (VersionString.IsEmpty())
	{
		return false;
	}

	FScopeLock Lock(&RegistryLock);

	if (!bRegistryLoaded)
	{
		LoadVersionRegistry();
	}

	TMap<EHotUpdatePlatform, FHotUpdateEditorVersionInfo>* PlatformMap = VersionRegistry.Find(VersionString);
	if (!PlatformMap)
	{
		return false;
	}

	PlatformMap->Remove(Platform);

	if (PlatformMap->Num() == 0)
	{
		VersionRegistry.Remove(VersionString);
	}

	return SaveVersionRegistry();
}

TArray<FHotUpdateEditorVersionInfo> FHotUpdateVersionManager::GetVersionHistory(EHotUpdatePlatform Platform)
{
	FScopeLock Lock(&RegistryLock);

	if (!bRegistryLoaded)
	{
		LoadVersionRegistry();
	}

	TArray<FHotUpdateEditorVersionInfo> Result;

	for (const auto& Pair : VersionRegistry)
	{
		const FHotUpdateEditorVersionInfo* Info = Pair.Value.Find(Platform);
		if (Info)
		{
			Result.Add(*Info);
		}
	}

	// 按创建时间降序排序
	Result.Sort([](const FHotUpdateEditorVersionInfo& A, const FHotUpdateEditorVersionInfo& B)
	{
		return A.CreatedTime > B.CreatedTime;
	});

	return Result;
}

FHotUpdateEditorVersionInfo FHotUpdateVersionManager::GetVersionInfo(const FString& VersionString, EHotUpdatePlatform Platform)
{
	FScopeLock Lock(&RegistryLock);

	if (!bRegistryLoaded)
	{
		LoadVersionRegistry();
	}

	const TMap<EHotUpdatePlatform, FHotUpdateEditorVersionInfo>* PlatformMap = VersionRegistry.Find(VersionString);
	if (PlatformMap)
	{
		const FHotUpdateEditorVersionInfo* Info = PlatformMap->Find(Platform);
		if (Info)
		{
			return *Info;
		}
	}

	return FHotUpdateEditorVersionInfo();
}

FHotUpdateVersionChain FHotUpdateVersionManager::GetVersionChain(const FString& BaseVersion, EHotUpdatePlatform Platform)
{
	FHotUpdateVersionChain Chain;
	Chain.BaseVersion = BaseVersion;
	Chain.Platform = Platform;

	FScopeLock Lock(&RegistryLock);

	if (!bRegistryLoaded)
	{
		LoadVersionRegistry();
	}

	// 收集所有基于此基础版本的 Patch
	TArray<FHotUpdateEditorVersionInfo> AllVersions = GetVersionHistory(Platform);

	for (const FHotUpdateEditorVersionInfo& Info : AllVersions)
	{
		if (Info.PackageKind == EHotUpdatePackageKind::Patch && Info.BaseVersion == BaseVersion)
		{
			Chain.PatchChain.Add(Info.VersionString);
		}
	}

	// 排序 Patch 链
	Chain.PatchChain.Sort([this](const FString& A, const FString& B)
	{
		return CompareVersions(A, B) < 0;
	});

	// 当前版本是 Patch 链中最后一个
	if (Chain.PatchChain.Num() > 0)
	{
		Chain.CurrentVersion = Chain.PatchChain.Last();
	}
	else
	{
		Chain.CurrentVersion = BaseVersion;
	}

	return Chain;
}

FString FHotUpdateVersionManager::GetLatestVersion(EHotUpdatePlatform Platform)
{
	FScopeLock Lock(&RegistryLock);

	if (!bRegistryLoaded)
	{
		LoadVersionRegistry();
	}

	FString LatestVersion;

	for (const auto& Pair : VersionRegistry)
	{
		const FHotUpdateEditorVersionInfo* Info = Pair.Value.Find(Platform);
		if (Info)
		{
			if (LatestVersion.IsEmpty() || CompareVersions(Info->VersionString, LatestVersion) > 0)
			{
				LatestVersion = Info->VersionString;
			}
		}
	}

	return LatestVersion;
}

TArray<FString> FHotUpdateVersionManager::GetBaseVersions(EHotUpdatePlatform Platform)
{
	FScopeLock Lock(&RegistryLock);

	if (!bRegistryLoaded)
	{
		LoadVersionRegistry();
	}

	TArray<FString> Result;

	for (const auto& Pair : VersionRegistry)
	{
		const FHotUpdateEditorVersionInfo* Info = Pair.Value.Find(Platform);
		if (Info && Info->PackageKind == EHotUpdatePackageKind::Base)
		{
			Result.Add(Info->VersionString);
		}
	}

	// 排序
	Result.Sort([this](const FString& A, const FString& B)
	{
		return CompareVersions(A, B) > 0;
	});

	return Result;
}

TArray<FHotUpdateVersionSelectItem> FHotUpdateVersionManager::GetSelectableVersions(EHotUpdatePlatform Platform)
{
	FScopeLock Lock(&RegistryLock);

	if (!bRegistryLoaded)
	{
		LoadVersionRegistry();
	}

	TArray<FHotUpdateVersionSelectItem> Result;

	for (const auto& Pair : VersionRegistry)
	{
		const FHotUpdateEditorVersionInfo* Info = Pair.Value.Find(Platform);
		if (Info)
		{
			FHotUpdateVersionSelectItem Item;
			Item.VersionString = Info->VersionString;
			Item.PackageKind = Info->PackageKind;
			Item.BaseVersion = Info->BaseVersion;
			Item.ManifestPath = Info->ManifestPath;
			Item.CreatedTime = Info->CreatedTime;

			// 生成显示名称
			if (Info->PackageKind == EHotUpdatePackageKind::Base)
			{
				Item.DisplayName = FString::Printf(TEXT("%s (基础包)"), *Info->VersionString);
			}
			else
			{
				Item.DisplayName = FString::Printf(TEXT("%s (热更包，基于 %s)"),
					*Info->VersionString, *Info->BaseVersion);
			}

			Result.Add(Item);
		}
	}

	// 按创建时间降序排序
	Result.Sort([](const FHotUpdateVersionSelectItem& A, const FHotUpdateVersionSelectItem& B)
	{
		return A.CreatedTime > B.CreatedTime;
	});

	return Result;
}

FString FHotUpdateVersionManager::GetVersionRootDir()
{
	return FPaths::ProjectSavedDir() / TEXT("HotUpdateVersions");
}

FString FHotUpdateVersionManager::GetVersionDir(const FString& VersionString, EHotUpdatePlatform Platform)
{
	return FPaths::Combine(GetVersionRootDir(), VersionString, HotUpdateUtils::GetPlatformString(Platform));
}

FString FHotUpdateVersionManager::GetVersionDir(const FString& VersionString, EHotUpdatePlatform Platform, EHotUpdateAndroidTextureFormat AndroidTextureFormat)
{
	return FPaths::Combine(GetVersionRootDir(), VersionString, HotUpdateUtils::GetPlatformDirName(Platform, AndroidTextureFormat));
}

bool FHotUpdateVersionManager::VersionExists(const FString& VersionString, EHotUpdatePlatform Platform)
{
	FScopeLock Lock(&RegistryLock);

	if (!bRegistryLoaded)
	{
		LoadVersionRegistry();
	}

	const TMap<EHotUpdatePlatform, FHotUpdateEditorVersionInfo>* PlatformMap = VersionRegistry.Find(VersionString);
	return PlatformMap && PlatformMap->Contains(Platform);
}

bool FHotUpdateVersionManager::LoadVersionRegistry()
{
	FString RegistryPath = GetVersionRootDir() / TEXT("VersionRegistry.json");

	if (!FPaths::FileExists(RegistryPath))
	{
		bRegistryLoaded = true;
		return true;
	}

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *RegistryPath))
	{
		UE_LOG(LogHotUpdateEditor, Warning, TEXT("无法加载版本注册表: %s"), *RegistryPath);
		return false;
	}

	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		UE_LOG(LogHotUpdateEditor, Warning, TEXT("无法解析版本注册表: %s"), *RegistryPath);
		return false;
	}

	VersionRegistry.Empty();

	const TArray<TSharedPtr<FJsonValue>>* VersionsArray;
	if (RootObject->TryGetArrayField(TEXT("versions"), VersionsArray))
	{
		for (const TSharedPtr<FJsonValue>& VersionValue : *VersionsArray)
		{
			TSharedPtr<FJsonObject> VersionObj = VersionValue->AsObject();
			if (!VersionObj.IsValid()) continue;

			FHotUpdateEditorVersionInfo Info;
			Info.VersionString = VersionObj->GetStringField(TEXT("versionString"));
			Info.PackageKind = static_cast<EHotUpdatePackageKind>(VersionObj->GetIntegerField(TEXT("packageKind")));
			Info.BaseVersion = VersionObj->GetStringField(TEXT("baseVersion"));

			int32 PlatformIndex = VersionObj->GetIntegerField(TEXT("platform"));
			Info.Platform = static_cast<EHotUpdatePlatform>(PlatformIndex);

			FString CreatedTimeStr = VersionObj->GetStringField(TEXT("createdTime"));
			FDateTime::ParseIso8601(*CreatedTimeStr, Info.CreatedTime);

			Info.ManifestPath = VersionObj->GetStringField(TEXT("manifestPath"));
			Info.UtocPath = VersionObj->GetStringField(TEXT("utocPath"));
			Info.AssetCount = VersionObj->GetIntegerField(TEXT("assetCount"));
			Info.PackageSize = VersionObj->GetNumberField(TEXT("packageSize"));

			TMap<EHotUpdatePlatform, FHotUpdateEditorVersionInfo>& PlatformMap = VersionRegistry.FindOrAdd(Info.VersionString);
			PlatformMap.Add(Info.Platform, Info);
		}
	}

	bRegistryLoaded = true;
	return true;
}

bool FHotUpdateVersionManager::SaveVersionRegistry()
{
	FString RegistryDir = GetVersionRootDir();
	IPlatformFile::GetPlatformPhysical().CreateDirectoryTree(*RegistryDir);

	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);
	TArray<TSharedPtr<FJsonValue>> VersionsArray;

	for (const auto& Pair : VersionRegistry)
	{
		for (const auto& PlatformPair : Pair.Value)
		{
			const FHotUpdateEditorVersionInfo& Info = PlatformPair.Value;

			TSharedPtr<FJsonObject> VersionObj = MakeShareable(new FJsonObject);
			VersionObj->SetStringField(TEXT("versionString"), Info.VersionString);
			VersionObj->SetNumberField(TEXT("packageKind"), static_cast<int32>(Info.PackageKind));
			VersionObj->SetStringField(TEXT("baseVersion"), Info.BaseVersion);
			VersionObj->SetNumberField(TEXT("platform"), static_cast<int32>(Info.Platform));
			VersionObj->SetStringField(TEXT("createdTime"), Info.CreatedTime.ToIso8601());
			VersionObj->SetStringField(TEXT("manifestPath"), Info.ManifestPath);
			VersionObj->SetStringField(TEXT("utocPath"), Info.UtocPath);
			VersionObj->SetNumberField(TEXT("assetCount"), Info.AssetCount);
			VersionObj->SetNumberField(TEXT("packageSize"), Info.PackageSize);

			VersionsArray.Add(MakeShareable(new FJsonValueObject(VersionObj)));
		}
	}

	RootObject->SetArrayField(TEXT("versions"), VersionsArray);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

	FString RegistryPath = RegistryDir / TEXT("VersionRegistry.json");
	return FFileHelper::SaveStringToFile(OutputString, *RegistryPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool FHotUpdateVersionManager::ParseVersionString(const FString& VersionString, int32& OutMajor, int32& OutMinor, int32& OutPatch, int32& OutBuild)
{
	OutMajor = 0;
	OutMinor = 0;
	OutPatch = 0;
	OutBuild = 0;

	TArray<FString> Parts;
	VersionString.ParseIntoArray(Parts, TEXT("."));

	if (Parts.Num() >= 1) OutMajor = FCString::Atoi(*Parts[0]);
	if (Parts.Num() >= 2) OutMinor = FCString::Atoi(*Parts[1]);
	if (Parts.Num() >= 3) OutPatch = FCString::Atoi(*Parts[2]);
	if (Parts.Num() >= 4) OutBuild = FCString::Atoi(*Parts[3]);

	return Parts.Num() >= 1;
}

int32 FHotUpdateVersionManager::CompareVersions(const FString& A, const FString& B)
{
	int32 MajorA, MinorA, PatchA, BuildA;
	int32 MajorB, MinorB, PatchB, BuildB;

	ParseVersionString(A, MajorA, MinorA, PatchA, BuildA);
	ParseVersionString(B, MajorB, MinorB, PatchB, BuildB);

	if (MajorA != MajorB) return MajorA < MajorB ? -1 : 1;
	if (MinorA != MinorB) return MinorA < MinorB ? -1 : 1;
	if (PatchA != PatchB) return PatchA < PatchB ? -1 : 1;
	if (BuildA != BuildB) return BuildA < BuildB ? -1 : 1;

	return 0;
}