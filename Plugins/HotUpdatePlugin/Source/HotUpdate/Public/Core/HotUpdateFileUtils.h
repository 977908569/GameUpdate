// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/HotUpdateTypes.h"
#include "HotUpdateFileUtils.generated.h"

/**
 * 热更新文件工具类
 * 提供文件操作的公共静态方法
 */
UCLASS(BlueprintType)
class HOTUPDATE_API UHotUpdateFileUtils : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * 计算文件 SHA1 Hash
	 * @param FilePath 文件路径
	 * @return SHA1 Hash 十六进制字符串，失败返回空字符串
	 */
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|File")
	static FString CalculateFileHash(const FString& FilePath);

	/**
	 * 确保目录存在
	 * @param DirectoryPath 目录路径
	 * @return 是否成功创建或目录已存在
	 */
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|File")
	static bool EnsureDirectoryExists(const FString& DirectoryPath);

	/**
	 * 字节数组转十六进制字符串
	 * @param Bytes 字节数组指针
	 * @param Count 字节数量
	 * @return 十六进制字符串
	 */
	static FString BytesToHex(const uint8* Bytes, int32 Count);

	/**
	 * 十六进制字符串转字节数组
	 * @param HexString 十六进制字符串
	 * @param OutBytes 输出字节数组
	 * @return 是否转换成功
	 */
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|File")
	static bool HexToBytes(const FString& HexString, TArray<uint8>& OutBytes);

	/**
	 * 判断资产路径是否属于引擎（应归入首包）
	 * 通过将包名转换为全路径，检查是否包含 /Engine/
	 * @param PackagePath 资产路径（如 /Engine/EngineMaterials/DefaultMaterial）
	 * @return true 如果是引擎资源
	 */
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|File")
	static bool IsEngineAsset(const FString& PackagePath);

	/**
	 * 从 JSON 字符串解析 Manifest
	 * @param JsonString JSON 字符串
	 * @param OutManifest 输出的 Manifest 数据
	 * @return 是否解析成功
	 */
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Manifest")
	static bool ParseManifestFromJson(const FString& JsonString, FHotUpdateManifest& OutManifest);

	/**
	 * 将 Manifest 保存到文件
	 * @param FilePath 文件路径
	 * @param Manifest Manifest 数据
	 * @return 是否保存成功
	 */
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Manifest")
	static bool SaveManifestToFile(const FString& FilePath, const FHotUpdateManifest& Manifest);

	/**
	 * 将 Manifest 序列化为 JSON 字符串
	 * @param Manifest Manifest 数据
	 * @return JSON 字符串
	 */
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Manifest")
	static FString ManifestToJsonString(const FHotUpdateManifest& Manifest);
};