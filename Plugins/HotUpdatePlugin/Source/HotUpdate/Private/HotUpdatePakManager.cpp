// Copyright czm. All Rights Reserved.

#include "HotUpdatePakManager.h"
#include "HotUpdate.h"
#include "Core/HotUpdateFileUtils.h"
#include "IPlatformFilePak.h"
#include "Misc/AES.h"
#include "Misc/CoreDelegates.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

void FHotUpdatePakManager::Initialize(const FString& InPakDirectory)
{
	PakDirectory = InPakDirectory;

	// 确保目录存在
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*PakDirectory))
	{
		PlatformFile.CreateDirectoryTree(*PakDirectory);
	}

	UE_LOG(LogHotUpdate, Log, TEXT("PakManager initialized. Directory: %s"), *PakDirectory);
}

bool FHotUpdatePakManager::MountPak(const FString& PakPath, int32 PakOrder, const FString& EncryptionKey)
{
	// 检查是否已挂载
	if (IsPakMounted(PakPath))
	{
		UE_LOG(LogHotUpdate, Log, TEXT("Pak already mounted, skipping: %s"), *PakPath);
		return true;
	}

	// 获取 Pak 平台文件
	IPlatformFile* FoundFile = FPlatformFileManager::Get().FindPlatformFile(TEXT("PakFile"));
	FPakPlatformFile* PakPlatformFile = FoundFile ? static_cast<FPakPlatformFile*>(FoundFile) : nullptr;
	if (!PakPlatformFile)
	{
		UE_LOG(LogHotUpdate, Error, TEXT("PakPlatformFile not found"));
		return false;
	}

	// 检查文件是否存在
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.FileExists(*PakPath))
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Pak file not found: %s"), *PakPath);
		return false;
	}

	// 处理加密密钥
	bool bUseEncryption = false;

	if (!EncryptionKey.IsEmpty())
	{
		bUseEncryption = true;

		// 将密钥注册到引擎
		TArray<uint8> KeyBytes;
		if (UHotUpdateFileUtils::HexToBytes(EncryptionKey, KeyBytes))
		{
			constexpr int32 AESKeySize = 32;
			if (KeyBytes.Num() < AESKeySize)
			{
				KeyBytes.SetNumZeroed(AESKeySize);
			}
			else if (KeyBytes.Num() > AESKeySize)
			{
				KeyBytes.SetNum(AESKeySize);
			}

			FAES::FAESKey AesKey;
			FMemory::Memcpy(AesKey.Key, KeyBytes.GetData(), AESKeySize);

			FGuid TempGuid = FGuid::NewGuid();
			FCoreDelegates::GetRegisterEncryptionKeyMulticastDelegate().Broadcast(TempGuid, AesKey);
			UE_LOG(LogHotUpdate, Log, TEXT("Registered encryption key with engine for Pak: %s"), *PakPath);
		}
		else
		{
			UE_LOG(LogHotUpdate, Warning, TEXT("Failed to convert encryption key to bytes: %s"), *EncryptionKey);
		}
	}

	// 使用 UE5.7 的 Mount API
	bool bSuccess = PakPlatformFile->Mount(*PakPath, PakOrder);
	if (bSuccess)
	{
		// 添加到已挂载列表
		FHotUpdatePakMetadata Metadata = ParsePakMetadata(PakPath);
		Metadata.bIsMounted = true;
		MountedPaks.Add(Metadata);

		UE_LOG(LogHotUpdate, Log, TEXT("Mounted Pak: %s (Order: %d, Encrypted: %s)"),
			*PakPath, PakOrder, bUseEncryption ? TEXT("true") : TEXT("false"));
	}
	else
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Failed to mount Pak: %s"), *PakPath);
	}

	return bSuccess;
}

bool FHotUpdatePakManager::UnmountPak(const FString& PakPath)
{
	FPakPlatformFile* PakPlatformFile = static_cast<FPakPlatformFile*>(FPlatformFileManager::Get().FindPlatformFile(TEXT("PakFile")));
	if (!PakPlatformFile)
	{
		return false;
	}

	bool bSuccess = PakPlatformFile->Unmount(*PakPath);
	if (bSuccess)
	{
		// 从已挂载列表移除
		for (int32 i = MountedPaks.Num() - 1; i >= 0; i--)
		{
			if (MountedPaks[i].PakPath == PakPath)
			{
				MountedPaks.RemoveAt(i);
			}
		}

		UE_LOG(LogHotUpdate, Log, TEXT("Unmounted Pak: %s"), *PakPath);
	}
	else
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("Failed to unmount Pak: %s"), *PakPath);
	}

	return bSuccess;
}

bool FHotUpdatePakManager::IsPakMounted(const FString& PakPath) const
{
	for (const FHotUpdatePakMetadata& Metadata : MountedPaks)
	{
		if (Metadata.PakPath == PakPath)
		{
			return true;
		}
	}
	return false;
}

FHotUpdatePakMetadata FHotUpdatePakManager::ParsePakMetadata(const FString& PakPath)
{
	FHotUpdatePakMetadata Metadata;
	Metadata.PakPath = PakPath;
	Metadata.PakName = FPaths::GetCleanFilename(PakPath);
	Metadata.bIsMounted = false;

	// 获取文件大小
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	int64 RawSize = PlatformFile.FileSize(*PakPath);
	Metadata.PakSize = RawSize > 0 ? RawSize : 0;

	// 尝试从文件名解析版本信息
	// 假设文件名格式: "HotUpdate_1.2.3.pak" 或 "Chunk_100_1.2.3.pak" 或 "Patch_1.2.3.utoc"
	FString Filename = Metadata.PakName;
	Filename.RemoveFromEnd(TEXT(".pak"));
	Filename.RemoveFromEnd(TEXT(".utoc"));

	TArray<FString> Parts;
	Filename.ParseIntoArray(Parts, TEXT("_"));

	for (const FString& Part : Parts)
	{
		// 检查是否为版本号格式 (x.x.x)
		if (Part.Contains(TEXT(".")))
		{
			TArray<FString> VersionParts;
			Part.ParseIntoArray(VersionParts, TEXT("."));

			if (VersionParts.Num() >= 2)
			{
				Metadata.Version = FHotUpdateVersionInfo::FromString(Part);
			}
		}
	}

	return Metadata;
}

int32 FHotUpdatePakManager::CalculatePakOrder(const FHotUpdateVersionInfo& Version)
{
	// Pak 顺序规则：
	// 1. 基础 Pak (Chunk 0) 优先级最低
	// 2. 更高版本的 Pak 优先级更高
	// 3. 补丁 Pak 优先级最高

	int32 BaseOrder = 100; // 基础顺序

	// 版本号影响顺序
	BaseOrder += Version.MajorVersion * 10000;
	BaseOrder += Version.MinorVersion * 100;
	BaseOrder += Version.PatchVersion;

	return BaseOrder;
}
