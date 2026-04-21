// Copyright czm. All Rights Reserved.

#include "Core/HotUpdateFileUtils.h"
#include "HotUpdate.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "Misc/SecureHash.h"
#include "Misc/PackageName.h"

FString UHotUpdateFileUtils::CalculateFileHash(const FString& FilePath)
{
	// 使用流式分块读取，避免将整个文件加载到内存
	FArchive* FileReader = IFileManager::Get().CreateFileReader(*FilePath);
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
			delete FileReader;
			return TEXT("");
		}

		HashState.Update(Buffer.GetData(), BytesToRead);
		Offset += BytesToRead;
	}

	delete FileReader;

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

	FString Result;
	Result.Reserve(Count * 2);

	for (int32 i = 0; i < Count; i++)
	{
		Result += FString::Printf(TEXT("%02x"), Bytes[i]);
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
			return false;
		}
		OutBytes[i] = static_cast<uint8>((V1 << 4) | V2);
	}

	return true;
}

bool UHotUpdateFileUtils::IsEngineAsset(const FString& PackagePath)
{
	// 项目资产不归入引擎
	if (PackagePath.StartsWith(TEXT("/Game/")))
	{
		return false;
	}

	// 转换为磁盘全路径，判断是否在 Engine 目录下
	FString Filename = FPackageName::LongPackageNameToFilename(PackagePath);
	FString FullPath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*Filename);

	// 全路径包含 /Engine/ 则为引擎资源
	return FullPath.Contains(TEXT("/Engine/"));
}