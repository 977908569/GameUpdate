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

			// 检查密钥是否已注册
			FGuid KeyGuid;
			if (const FGuid* ExistingGuid = RegisteredEncryptionKeys.Find(EncryptionKey))
			{
				KeyGuid = *ExistingGuid;
				UE_LOG(LogHotUpdate, Log, TEXT("Reusing registered encryption key for Pak: %s"), *PakPath);
			}
			else
			{
				// 生成确定性 GUID（基于密钥内容的哈希）
				KeyGuid = FGuid(
					FCrc::MemCrc32(KeyBytes.GetData(), 4),
					FCrc::MemCrc32(KeyBytes.GetData() + 4, 4),
					FCrc::MemCrc32(KeyBytes.GetData() + 8, 4),
					FCrc::MemCrc32(KeyBytes.GetData() + 12, 4)
				);
				RegisteredEncryptionKeys.Add(EncryptionKey, KeyGuid);
				UE_LOG(LogHotUpdate, Log, TEXT("Registered new encryption key with engine for Pak: %s"), *PakPath);
			}

			FCoreDelegates::GetRegisterEncryptionKeyMulticastDelegate().Broadcast(KeyGuid, AesKey);
		}
		else
		{
			UE_LOG(LogHotUpdate, Warning, TEXT("Failed to convert encryption key to bytes: %s"), *EncryptionKey);
		}
	}

	const bool bSuccess = PakPlatformFile->Mount(*PakPath, PakOrder);
	if (bSuccess)
	{
		// 添加到已挂载列表并更新索引
		FHotUpdatePakMetadata Metadata = ParsePakMetadata(PakPath);
		Metadata.bIsMounted = true;
		const int32 NewIndex = MountedPaks.Add(Metadata);
		PakPathToIndex.Add(PakPath, NewIndex);

		UE_LOG(LogHotUpdate, Log, TEXT("Mounted Pak: %s (Order: %d, Encrypted: %s)"), *PakPath, PakOrder, bUseEncryption ? TEXT("true") : TEXT("false"));
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
		// 从已挂载列表移除并更新索引
		if (int32* IndexPtr = PakPathToIndex.Find(PakPath))
		{
			int32 Index = *IndexPtr;
			MountedPaks.RemoveAt(Index);
			PakPathToIndex.Remove(PakPath);

			// 更新后续元素的索引
			for (auto& Pair : PakPathToIndex)
			{
				if (Pair.Value > Index)
				{
					Pair.Value--;
				}
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
	return PakPathToIndex.Contains(PakPath);
}

FHotUpdatePakMetadata FHotUpdatePakManager::ParsePakMetadata(const FString& PakPath)
{
	FHotUpdatePakMetadata Metadata;
	Metadata.PakPath = PakPath;
	Metadata.PakName = FPaths::GetCleanFilename(PakPath);
	Metadata.bIsMounted = false;

	// 获取文件大小
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	const int64 RawSize = PlatformFile.FileSize(*PakPath);
	Metadata.PakSize = RawSize > 0 ? RawSize : 0;

	// 尝试从文件名解析版本信息（后备机制，优先从 Manifest 获取）
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

			// 至少需要 2 个版本部分（如 1.2）
			if (VersionParts.Num() >= 2)
			{
				// 验证所有部分都是数字
				bool bAllDigits = true;
				for (const FString& VP : VersionParts)
				{
					if (VP.IsEmpty() || !VP.IsNumeric())
					{
						bAllDigits = false;
						break;
					}
				}

				if (bAllDigits)
				{
					Metadata.Version = FHotUpdateVersionInfo::FromString(Part);
				}
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
