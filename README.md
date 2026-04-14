# HotUpdatePlugin

Unreal Engine 5.7 热更新（OTA 补丁）插件，支持 Pak/IoStore 打包、增量/差异更新、版本对比和运行时补丁下载挂载。

## 目录

- [功能特性](#功能特性)
- [环境要求](#环境要求)
- [快速开始](#快速开始)
- [分包功能](#分包功能)
- [项目架构](#项目架构)
- [目录结构](#目录结构)
- [FAQ](#faq)
- [故障排除](#故障排除)
- [License](#license)

## 功能特性

| 功能 | 说明 |
|------|------|
| 运行时热更新 | 检测版本 → 下载补丁 → 挂载 Pak，全流程自动化 |
| 增量更新 | 基于 diff 的差异下载，减少下载量 |
| 并发下载 | HTTP 多线程下载，支持暂停/恢复/重试 |
| Pak 挂载 | 运行时动态挂载/卸载 Pak 文件，支持加密密钥注册 |
| 版本管理 | JSON 清单格式，支持链式更新（1.0.0 → 1.0.1 → 1.0.2） |
| 编辑器工具 | 4 合 1 Slate 面板：基础包构建、补丁打包、版本对比、Pak 查看器 |
| Blueprint 支持 | 所有运行时 API 均可从蓝图调用 |
| 分包功能 | 支持 6 种分包策略，灵活管理资源模块化下载 |
| 最小包模式 | 首包瘦身，pakchunk0 随安装包分发，pakchunk1+ 通过 CDN 热更新下载 |

## 环境要求

- Unreal Engine 5.7
- Visual Studio 2022 / Rider
- Windows 平台（当前）

## 快速开始

### 打开项目

1. 克隆仓库
2. 右键 `GameUpdate.uproject` → Generate Visual Studio project files
3. 打开 `GameUpdate.uproject`

### 配置热更新服务器

编辑 `Config/DefaultGame.ini`，填写你的服务器地址：

```ini
[/Script/HotUpdate.HotUpdateSettings]
ManifestUrl="http://your-server.com/hotpatch/manifest.json"
ResourceBaseUrl="http://your-server.com/hotpatch/"
bAllowHttpConnection=True
```

### C++ 快速示例

```cpp
#include "HotUpdateManager.h"

void StartHotUpdate()
{
    UHotUpdateManager* Manager = GetGameInstance()->GetSubsystem<UHotUpdateManager>();
    Manager->OnUpdateCheckComplete.AddDynamic(this, &MyClass::OnCheckComplete);
    Manager->CheckForUpdate();
}

void OnCheckComplete(bool bHasUpdate, const FString& Version)
{
    if (bHasUpdate)
    {
        UE_LOG(LogTemp, Log, TEXT("发现新版本: %s"), *Version);
        Manager->StartDownload();
    }
}
```

### 蓝图调用

所有运行时 API 均可在蓝图中直接调用：

1. 获取 `HotUpdateManager` Subsystem
2. 调用 `CheckForUpdate` 检查更新
3. 绑定 `OnUpdateCheckComplete` 事件处理结果
4. 调用 `StartDownload` 开始下载
5. 绑定 `OnDownloadProgress` 监控进度
6. 调用 `ApplyUpdate` 应用补丁

### 打包命令

在编辑器中通过 **HotUpdateEditor** 面板操作，或使用命令行：

#### 包类型说明

**基础包**：包含完整游戏资源的初始版本包

![基础包说明](Plugins/HotUpdatePlugin/Resources/base_package.png)

**更新包**：基于基础包的差异补丁，仅包含变更资源

![更新包说明](Plugins/HotUpdatePlugin/Resources/hot_package.png)

#### 标准打包

```bash
# 构建基础包
UnrealEditor-Cmd GameUpdate -run=HotUpdate -mode=base -version=1.0.0 -platform=Windows -output=D:/Output

# 构建增量补丁
UnrealEditor-Cmd GameUpdate -run=HotUpdate -mode=patch -version=1.0.1 -baseversion=1.0.0 -platform=Windows -output=D:/Output
```

**标准参数说明**：

| 参数 | 说明 |
|------|------|
| `-mode` | 打包模式：base（基础包）或 patch（补丁包） |
| `-version` | 版本号（如 1.0.0） |
| `-baseversion` | 基础版本号（patch 模式必需） |
| `-platform` | 目标平台：Windows、Android、IOS |
| `-output` | 输出目录路径（可选，有默认值） |
| `-shipping` | 发布版本构建（可选） |
| `-skipbuild` | 跳过编译步骤（可选） |

#### 最小包模式打包

最小包模式用于构建"瘦身"首包，将 pakchunk1+ 资源分离到热更新目录，仅 pakchunk0 打包到最终安装包。

```bash
UnrealEditor-Cmd GameUpdate -run=HotUpdate -mode=base -version=1.0.0 \
    -platform=Windows \
    -minimal \
    -whitelist="/Game/UI;/Game/Startup"
```

**最小包参数说明**：

| 参数 | 说明 |
|------|------|
| `-minimal` | 启用最小包模式 |
| `-whitelist=<paths>` | 白名单目录（分号分隔），必须打包到 Chunk 0 |

## 分包功能

插件支持将游戏资源按不同策略划分到多个 Chunk 中，便于实现增量下载、模块化管理和按需加载。

### 分包策略

| 策略 | 说明 | 适用场景 |
|------|------|----------|
| `None` | 不分包，所有资源打包成一个 Chunk | 小型项目、快速原型 |
| `Size` | 按大小分包，超过 MaxChunkSizeMB 自动分割 | 控制单个包体积 |
| `Directory` | 按目录分包，根据规则将指定目录独立打包 | 模块化项目结构 |
| `AssetType` | 按资源类型分包（Texture、Material、Mesh 等） | 资源类型隔离管理 |
| `PrimaryAsset` | UE5 标准 Primary Asset 分包（默认） | 标准游戏资源管理 |
| `Hybrid` | 混合模式：目录优先 + 其余按大小分包 | 精细化分包控制 |

### 配置方式

#### 编辑器面板配置

在 **HotUpdateEditor** 的 Base Version Panel 中配置分包参数：

- **分包策略**：选择 ChunkStrategy
- **目录分包规则**：添加 DirectoryChunkRules
- **按大小分包配置**：设置 SizeBasedConfig
- **最小包模式**：启用 MinimalPackage 并配置白名单

#### 代码配置示例

```cpp
// 基础包构建配置
FHotUpdateBasePackageConfig Config;
Config.VersionString = "1.0.0";
Config.Platform = EHotUpdatePlatform::Windows;

// 设置分包策略
Config.ChunkStrategy = EHotUpdateChunkStrategy::Directory;

// 目录分包规则
FHotUpdateDirectoryChunkRule MapsRule;
MapsRule.DirectoryPath = "/Game/Maps";
MapsRule.ChunkName = "Maps";
MapsRule.ChunkId = 1;
MapsRule.Priority = 0;  // 最小优先级，最先加载
MapsRule.MaxSizeMB = 500;  // 最大 500MB
Config.DirectoryChunkRules.Add(MapsRule);

FHotUpdateDirectoryChunkRule UIRule;
UIRule.DirectoryPath = "/Game/UI";
UIRule.ChunkName = "UI";
UIRule.ChunkId = 2;
UIRule.Priority = 1;
Config.DirectoryChunkRules.Add(UIRule);

// 按大小分包配置（用于 Size/Hybrid 策略）
Config.SizeBasedConfig.MaxChunkSizeMB = 256;
Config.SizeBasedConfig.ChunkNamePrefix = "Chunk";
Config.SizeBasedConfig.ChunkIdStart = 100;
Config.SizeBasedConfig.bSortBySize = true;  // 大资源优先打包
```

### 目录分包规则字段

| 字段 | 类型 | 说明 |
|------|------|------|
| `DirectoryPath` | FString | 要分包的目录路径（如 `/Game/Maps`） |
| `ChunkName` | FString | Chunk 名称（如 `Maps`） |
| `ChunkId` | int32 | 指定 Chunk ID（-1 自动分配） |
| `Priority` | int32 | 加载优先级（越小越先加载） |
| `MaxSizeMB` | int32 | 该 Chunk 最大大小（0 = 无限制） |
| `bRecursive` | bool | 是否递归匹配子目录 |
| `ExcludedSubDirs` | TArray\<FString\> | 排除的子目录列表 |

### 按大小分包配置字段

| 字段 | 类型 | 说明 |
|------|------|------|
| `MaxChunkSizeMB` | int32 | 最大 Chunk 大小（默认 256MB） |
| `ChunkNamePrefix` | FString | Chunk 名称前缀（默认 `Chunk`） |
| `ChunkIdStart` | int32 | Chunk ID 起始值 |
| `bSortBySize` | bool | 是否按大小排序（大资源优先） |
| `bBalanceDistribution` | bool | 是否均衡分布（各 Chunk 大小接近） |

### 最小包模式

最小包模式用于构建只包含必要资源的"瘦身"基础包，其余资源通过热更新下载。

```cpp
// 最小包配置
FHotUpdateMinimalPackageConfig MinimalConfig;
MinimalConfig.bEnableMinimalPackage = true;

// 白名单目录（必须打包到 Chunk 0）
MinimalConfig.WhitelistDirectories.Add(FDirectoryPath{"/Game/UI"});
MinimalConfig.WhitelistDirectories.Add(FDirectoryPath{"/Game/Startup"});

// 依赖处理策略
MinimalConfig.DependencyStrategy = EHotUpdateDependencyStrategy::HardOnly;  // 仅硬依赖
MinimalConfig.MaxDependencyDepth = 0;  // 无限制
```

**依赖处理策略**：

| 策略 | 说明 |
|------|------|
| `IncludeAll` | 包含所有依赖（硬依赖 + 软依赖） |
| `HardOnly` | 仅硬依赖（必须的引用） |
| `SoftOnly` | 仅软依赖（可选引用） |
| `None` | 不包含依赖 |

### 运行时 Chunk 管理

运行时通过 `UHotUpdatePakManager` 管理 Chunk 的挂载和卸载：

```cpp
// 获取 Chunk 信息
TArray<FHotUpdateChunkInfo> Chunks = HotUpdateManager->GetLoadedChunks();

// Chunk 按 Priority 顺序挂载（优先级小的先加载）
// 依赖关系自动处理：父 Chunk 先于子 Chunk 加载

// Chunk 加载状态
EHotUpdateChunkState State = HotUpdateManager->GetChunkState(ChunkId);
```

## 项目架构

### 插件模块

| 模块 | 类型 | 说明 |
|------|------|------|
| HotUpdate | Runtime | 运行时热更新核心，随游戏发布 |
| HotUpdateEditor | Editor | 编辑器打包工具，仅开发时使用 |

### 运行时核心类

| 类 | 说明 |
|----|------|
| `UHotUpdateManager` | 核心调度器，流程：CheckForUpdate → StartDownload → ApplyUpdate |
| `UHotUpdateHttpDownloader` | HTTP 并发下载，暂停/恢复/重试 |
| `UHotUpdatePakManager` | Pak 挂载/卸载/校验 |
| `UHotUpdateManifestParser` | JSON 清单解析 |
| `UHotUpdateIncrementalCalculator` | 增量差异计算 |
| `UHotUpdateVersionStorage` | 本地版本持久化 |
| `UHotUpdateSettings` | 开发者配置（服务器地址、并发数、路径等） |

### 编辑器面板

| 面板 | 说明 |
|------|------|
| Base Version Panel | 构建完整基础包 |
| Packaging Panel | 构建补丁/差异包 |
| Version Diff Panel | 版本对比 |
| Pak Viewer Panel | 查看 Pak/IoStore 内容 |

### 数据流

```
编辑器: 资产 → Chunk 分配 → IoStore 构建 → 清单生成 → 版本注册
运行时: 检查更新(HTTP) → 解析清单 → 增量计算 → 下载(并发) → 校验哈希 → 挂载Pak → 更新版本
```

## 目录结构

```
GameUpdate/
├── Config/                          # 项目配置
├── Content/                         # UE 资产
├── Sources/GameUpdate/               # 游戏模块
│   └── UI/HotUpdateWidget.h         # 运行时 UMG 热更新界面
├── Plugins/HotUpdatePlugin/
│   └── Source/
│       ├── HotUpdate/               # 运行时模块
│       │   ├── Public/              # 头文件
│       │   │   ├── Core/            # 核心类
│       │   │   ├── Download/        # 下载相关
│       │   │   ├── Manifest/        # 清单相关
│       │   │   └── Pak/             # Pak 管理
│       │   └── Private/             # 实现文件
│       └── HotUpdateEditor/         # 编辑器模块
│           ├── Public/
│           └── Private/
│               └── Widgets/         # Slate 面板
└── Build/                           # 自动化脚本
```

## FAQ

### Q: Pak 挂载失败怎么办？

A: 检查以下项：

1. Pak 文件路径是否正确（相对路径应基于项目根目录）
2. 加密密钥是否已注册（通过 `RegisterEncryptionKey`）
3. Pak 文件是否已损坏（校验 SHA1 哈希值）
4. Chunk 加载顺序是否正确（依赖关系需先加载父 Chunk）

### Q: 如何调试热更新流程？

A: 在 `Config/DefaultGame.ini` 中启用调试日志：

```ini
[Core.Log]
LogHotUpdate=Verbose
LogHotUpdateEditor=Verbose
```

同时可在蓝图或 C++ 中绑定各个回调事件，监控流程状态。

### Q: 支持哪些平台？

A: 当前支持 Windows 平台。移动端（Android/iOS）支持正在开发中。

### Q: 增量更新如何工作？

A: 增量更新通过 `UHotUpdateIncrementalCalculator` 计算版本差异：

1. 比对新旧版本的 Manifest 文件
2. 计算文件级别的 SHA1 哈希差异
3. 仅下载发生变化的文件
4. 支持链式更新（1.0.0 → 1.0.1 → 1.0.2）

### Q: 如何自定义分包策略？

A: 在编辑器面板中配置 `DirectoryChunkRules`，或通过代码设置 `FHotUpdateBasePackageConfig` 的 `ChunkStrategy` 和相关参数。

## 故障排除

### HTTP 连接失败

**症状**：`CheckForUpdate` 返回错误，无法获取 Manifest

**解决方案**：

1. 检查 `ManifestUrl` 配置是否正确
2. 检查服务器是否可访问（浏览器直接访问 URL 测试）
3. 检查防火墙/网络代理设置
4. 确保 `bAllowHttpConnection=True`（HTTP 需显式启用）

### Pak 校验失败

**症状**：下载完成后校验哈希不匹配

**解决方案**：

1. 重新下载补丁文件
2. 检查 CDN 文件完整性
3. 确认 Manifest 中的哈希值与实际文件一致
4. 检查是否有文件传输过程中的编码问题

### 版本解析错误

**症状**：Manifest 解析失败，JSON 格式错误

**解决方案**：

1. 检查 Manifest JSON 格式是否正确（使用 JSON 校验工具）
2. 确认版本字符串格式符合规范（如 `1.0.0`）
3. 检查 Manifest 版本号是否与插件兼容（当前版本 2）

### Chunk 加载顺序问题

**症状**：资源加载失败，依赖资源未找到

**解决方案**：

1. 检查 Chunk 的 `Priority` 设置（数值小的先加载）
2. 确认依赖关系配置正确（父 Chunk 先于子 Chunk）
3. 查看日志确认加载顺序

### 蓝图调用无响应

**症状**：蓝图调用 `CheckForUpdate` 后无回调

**解决方案**：

1. 确认已正确绑定事件（使用 `BindEvent` 或 `AddDynamic`）
2. 检查目标对象是否有效（未被销毁）
3. 查看日志确认异步任务是否启动

## License

[MIT](LICENSE)

Copyright czm. All Rights Reserved.