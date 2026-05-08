// Copyright czm. All Rights Reserved.

using AutomationTool;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.RegularExpressions;
using Microsoft.Extensions.Logging;

using static AutomationTool.CommandUtils;

/// <summary>
/// 最小包模式下的 Staging 处理器：
/// 1. ModifyDeploymentContextCallback — 从 UFSFiles 移除非资产文件
/// 2. PostStagingFileCopy — 用 UnrealPak 单独创建非资产 pak，然后移动 pakchunk1+ 到热更目录
/// </summary>
public class StripExtraPakChunksHandler : CustomStagingHandler
{
    /// <summary>
    /// 从 UFSFiles 中移除的非资产文件，按 ChunkId 分组（ChunkId -> (SourcePath -> PakInternalPath)）
    /// </summary>
    private Dictionary<int, Dictionary<string, string>> _pendingNonAssetFiles = new Dictionary<int, Dictionary<string, string>>();

    protected override bool TryInitialize(ProjectParams Params, DeploymentContext SC)
    {
        Params.ModifyDeploymentContextCallback += OnModifyDeploymentContext;
        return true;
    }

    /// <summary>
    /// 在 UFSFiles 收集完成后、pak 创建之前调用。
    /// 读取 MinimalPackageConfig.json 的 NonAssetChunkMapping，匹配移除非资产文件。
    /// </summary>
    private void OnModifyDeploymentContext(ProjectParams Params, DeploymentContext SC)
    {
        bool bMinimalPackage = Environment.GetCommandLineArgs()
            .Any(arg => arg.Equals("-MinimalPackage", StringComparison.OrdinalIgnoreCase));
        if (!bMinimalPackage) return;

        // 从 MinimalPackageConfig.json 读取 C++ 预计算的非资产文件映射
        string configPath = Path.Combine(SC.ProjectRoot.FullName, "Intermediate", "MinimalPackageConfig.json");
        if (!File.Exists(configPath))
        {
            Logger.LogWarning("ModifyDeploymentContext: MinimalPackageConfig.json not found at {Path}, skipping.", configPath);
            return;
        }

        // ChunkId -> (SourcePath -> PakInternalPath)
        var chunkFileMapping = new Dictionary<int, Dictionary<string, string>>();

        try
        {
            using var configDoc = JsonDocument.Parse(File.ReadAllText(configPath));
            if (configDoc.RootElement.TryGetProperty("NonAssetChunkMapping", out var nonAssetObj))
            {
                foreach (var chunkEntry in nonAssetObj.EnumerateObject())
                {
                    if (!int.TryParse(chunkEntry.Name, out int chunkId)) continue;
                    var fileDict = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
                    foreach (var fileEntry in chunkEntry.Value.EnumerateArray())
                    {
                        string sourcePath = fileEntry.GetProperty("SourcePath").GetString();
                        string pakPath = fileEntry.GetProperty("PakInternalPath").GetString();
                        if (!string.IsNullOrEmpty(sourcePath) && !string.IsNullOrEmpty(pakPath))
                        {
                            fileDict[Path.GetFullPath(sourcePath)] = pakPath;
                        }
                    }
                    if (fileDict.Count > 0)
                        chunkFileMapping[chunkId] = fileDict;
                }
            }
        }
        catch (Exception ex)
        {
            Logger.LogError(ex, "ModifyDeploymentContext: Failed to parse MinimalPackageConfig.json");
            return;
        }

        if (chunkFileMapping.Count == 0)
        {
            Logger.LogInformation("ModifyDeploymentContext: No NonAssetChunkMapping entries in config, skipping.");
            return;
        }

        int totalFiles = chunkFileMapping.Values.Sum(d => d.Count);
        Logger.LogInformation("ModifyDeploymentContext: Found {Chunks} chunks with {Count} non-asset files from MinimalPackageConfig.json", chunkFileMapping.Count, totalFiles);

        // 从 UFSFiles 中匹配并移除，按 ChunkId 分组
        var keysToRemove = new List<StagedFileReference>();
        foreach (var pair in SC.FilesToStage.UFSFiles)
        {
            string srcPath = pair.Value.FullName;
            foreach (var chunkPair in chunkFileMapping)
            {
                if (chunkPair.Value.TryGetValue(srcPath, out string dest))
                {
                    keysToRemove.Add(pair.Key);
                    if (!_pendingNonAssetFiles.TryGetValue(chunkPair.Key, out var chunkDict))
                    {
                        chunkDict = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
                        _pendingNonAssetFiles[chunkPair.Key] = chunkDict;
                    }
                    chunkDict[srcPath] = dest;
                    Logger.LogInformation("ModifyDeploymentContext: Removing non-asset from UFS: {Src} -> Chunk{Chunk} {Dest}", srcPath, chunkPair.Key, dest);
                    break;
                }
            }
        }

        foreach (var key in keysToRemove)
        {
            SC.FilesToStage.UFSFiles.Remove(key);
        }

        if (_pendingNonAssetFiles.Count > 0)
        {
            int removed = _pendingNonAssetFiles.Values.Sum(d => d.Count);
            Logger.LogInformation("ModifyDeploymentContext: Removed {Count} non-asset file(s) from UFSFiles across {Chunks} chunk(s)", removed, _pendingNonAssetFiles.Count);
        }
    }

    /// <summary>
    /// pak 创建之后调用。单独创建非资产 pak，然后移动 pakchunk1+ 到热更目录。
    /// </summary>
    public override void PostStagingFileCopy(ProjectParams Params, DeploymentContext SC)
    {
        bool bMinimalPackage = Environment.GetCommandLineArgs()
            .Any(arg => arg.Equals("-MinimalPackage", StringComparison.OrdinalIgnoreCase));
        if (!bMinimalPackage) return;

        string HotUpdateOutputDir = GetCommandLineArg("-HotUpdateOutputDir=");
        if (string.IsNullOrEmpty(HotUpdateOutputDir))
        {
            Logger.LogWarning("PostStagingFileCopy: -HotUpdateOutputDir not specified, skipping.");
            return;
        }

        string StageDir = SC.StageDirectory.FullName;

        // 1. 按 ChunkId 创建非资产 pak（放到 staging 目录，由 MoveExtraPakChunks 统一移动）
        foreach (var chunkPair in _pendingNonAssetFiles)
        {
            CreateNonAssetPak(Params, SC, StageDir, chunkPair.Key, chunkPair.Value);
        }

        // 2. 移动 pakchunk1+ 到热更目录
        if (!Directory.Exists(StageDir)) return;
        MoveExtraPakChunks(StageDir, HotUpdateOutputDir);
    }

    /// <summary>
    /// 用 UnrealPak 单独创建非资产 pak，命名格式与其他 pak 一致
    /// </summary>
    private void CreateNonAssetPak(ProjectParams Params, DeploymentContext SC, string StageDir, int chunkId, Dictionary<string, string> files)
    {
        // 命名格式：pakchunk{ChunkId}-{Platform}.pak
        string platformSuffix = SC.FinalCookPlatform;
        string pakName = string.Format("pakchunk{0}-{1}.pak", chunkId, platformSuffix);
        string pakPath = Path.Combine(StageDir, pakName);

        // 写 response file
        string responseFilesPath = CombinePaths(CmdEnv.EngineSavedFolder, "ResponseFiles");
        Directory.CreateDirectory(responseFilesPath);
        string responseFileName = CombinePaths(responseFilesPath, $"PakList_NonAssets_{chunkId}.txt");

        using (var writer = new StreamWriter(responseFileName, false, new System.Text.UTF8Encoding(true)))
        {
            foreach (var pair in files)
            {
                writer.WriteLine("\"{0}\" \"{1}\"", pair.Key, pair.Value);
            }
        }

        // 构造 UnrealPak 参数
        string arguments = string.Format("{0} -create={1}",
            MakePathSafeToUseWithCommandLine(pakPath),
            MakePathSafeToUseWithCommandLine(responseFileName));

        Logger.LogInformation("CreateNonAssetPak: Creating {Pak} with {Count} file(s)", pakPath, files.Count);

        // 运行 UnrealPak
        string UnrealPakPath = Path.Combine(CmdEnv.LocalRoot, "Engine", "Binaries", "Win64", "UnrealPak.exe");
        string fullArgs = MakePathSafeToUseWithCommandLine(Params.RawProjectPath.FullName) + " " + arguments;
        RunAndLog(CmdEnv, UnrealPakPath, fullArgs, Options: ERunOptions.Default | ERunOptions.UTF8Output);
    }

    /// <summary>
    /// 移动 pakchunk1+ 到热更目录
    /// </summary>
    private void MoveExtraPakChunks(string StageDir, string HotUpdateOutputDir)
    {
        var pakFiles = Directory.GetFiles(StageDir, "pakchunk*.pak", SearchOption.AllDirectories);
        var chunkRegex = new Regex(@"pakchunk(\d+)", RegexOptions.IgnoreCase | RegexOptions.Compiled);

        int movedCount = 0;
        Directory.CreateDirectory(HotUpdateOutputDir);

        foreach (string pakPath in pakFiles)
        {
            Match match = chunkRegex.Match(Path.GetFileName(pakPath));
            if (!match.Success) continue;

            int chunkIndex = int.Parse(match.Groups[1].Value);
            if (chunkIndex == 0) continue;

            string destPath = Path.Combine(HotUpdateOutputDir, Path.GetFileName(pakPath));
            if (File.Exists(destPath)) File.Delete(destPath);
            File.Move(pakPath, destPath);
            movedCount++;

            foreach (string ext in new[] { ".bin", ".ucas", ".utoc" })
            {
                string sidecarPath = Path.ChangeExtension(pakPath, ext);
                if (File.Exists(sidecarPath))
                {
                    string sidecarDestPath = Path.Combine(HotUpdateOutputDir, Path.GetFileName(sidecarPath));
                    if (File.Exists(sidecarDestPath)) File.Delete(sidecarDestPath);
                    File.Move(sidecarPath, sidecarDestPath);
                }
            }

            Logger.LogInformation("Moved {Name} -> {Dir}", Path.GetFileName(pakPath), HotUpdateOutputDir);
        }

        if (movedCount > 0)
            Logger.LogInformation("StripExtraPakChunks: moved {Count} pak file(s) to {Dir}", movedCount, HotUpdateOutputDir);
    }

    private static string GetCommandLineArg(string prefix)
    {
        var args = Environment.GetCommandLineArgs();
        for (int i = 0; i < args.Length; i++)
        {
            if (args[i].StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
            {
                return args[i].Substring(prefix.Length).Trim('"');
            }
        }
        return null;
    }
}
