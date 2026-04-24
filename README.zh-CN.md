# oniWorldApp

> 面向 `Windows x64` 的《缺氧》世界生成与筛种桌面应用主线。

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![C++](https://img.shields.io/badge/C%2B%2B-23-00599C?logo=cplusplus&logoColor=white)
![React](https://img.shields.io/badge/React-19-61DAFB?logo=react&logoColor=black)
![Tauri](https://img.shields.io/badge/Tauri-2.x-24C8DB?logo=tauri&logoColor=white)

当前仓库已经收敛为一条单一交付链：`desktop/ + src-tauri/ + oni-sidecar.exe`。目标是复用现有 C++ 世界生成核心，在本地提供桌面搜索、预览与安装包交付能力。

## 当前状态

- 唯一公共产品形态是 Windows 桌面应用。
- 唯一 release 命令是：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-desktop.ps1 -Variant both
```

- release 输出固定收口到：

```text
out/release/desktop/<version>/
  standard/
  offline/
  checksums.txt
  build-summary.json
```

- 默认交付为 `unsigned`，当前阶段未启用代码签名。

## 安装包变体

### `standard`

- 体积较小。
- 使用 WebView2 在线引导安装模式。
- 适合已经安装 WebView2，或允许联网补齐运行时的机器。

### `offline`

- 体积更大。
- 内置 WebView2 离线安装器。
- 适合缺少 WebView2 且不允许联网修复的机器。

## 系统要求

- Windows 10/11 x64
- Visual Studio C++ x64 Build Tools
- Rust toolchain
- Node.js
- `yarn` 或 `corepack`
- Tauri CLI

安装 Tauri CLI：

```powershell
cargo install tauri-cli --version "^2.0.0"
```

## 快速开始

### 开发模式

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\dev-desktop.ps1
```

该命令会完成桌面前端依赖检查、`x64-release` sidecar 构建与同步，然后启动 Vite 与 `cargo tauri dev`。
开发链会显式设置 `ONI_SIDECAR_PATH` 指向仓库内刚构建的 release sidecar，避免复用已安装应用写入 `LocalAppData` 的 runtime sidecar copy，也避免搜索误落到未优化的 Debug 二进制。

### 产出安装包

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-desktop.ps1 -Variant both
```

可选参数：

- `-Variant standard`
- `-Variant offline`
- `-Variant both`
- `-SigningProfile unsigned`

## 目录与架构

### 目录

```text
oni_world_app-master/
├── asset/               游戏 worldgen 资源与模板
├── desktop/             React + Vite 桌面前端
├── src-tauri/           Tauri 2 Rust 宿主与打包配置
├── src/                 C++ 世界生成核心与 sidecar 入口
├── scripts/             PowerShell 开发、构建与验收脚本
├── tests/               native tests 与 smoke 夹具
├── llmdoc/              当前态架构文档
└── out/                 本地构建与 release 输出
```

### 主链路

```text
desktop UI
  -> Tauri host
  -> oni-sidecar.exe
  -> AppRuntime / WorldGen / TemplateSpawning
  -> 搜索结果与地图预览
```

其中：

- `desktop/` 负责参数编辑、结果列表和预览界面。
- `src-tauri/` 负责 sidecar 生命周期、路径解析、事件转发和打包。
- `src/entry_sidecar.cpp` 负责协议入口。
- `src/App/`、`src/Batch/`、`src/SearchAnalysis/` 和 `src/Setting/` 承载核心运行时与搜索服务。

## 路径与数据策略

- 安装目录只放程序主体和只读资源。
- 用户持久化数据写入 `AppData`。
- 缓存、日志和 release/runtime sidecar copy 写入 `LocalAppData`。
- `dev-desktop.ps1` 会固定使用仓库内 release sidecar，不与已安装应用的 runtime sidecar copy 共用执行路径。
- 路径由 Rust host 统一解析，前端不硬编码 Windows 路径。

## 验证命令

native 主线：

```powershell
cmake --preset x64-release
cmake --build out/build/x64-release
ctest --test-dir out/build/x64-release -C Release --output-on-failure
```

Rust host：

```powershell
cargo test --manifest-path src-tauri/Cargo.toml
```

release 门禁：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\verify-desktop-release.ps1
```

## 文档入口

- `llmdoc/index.md`
- `llmdoc/overview/project.md`
- `llmdoc/architecture/world-generation.md`
- `llmdoc/reference/configuration.md`
- `llmdoc/reference/data-structures.md`

## 已知边界

- 当前自动化验证已覆盖 native tests、Rust tests、双安装包构建与静态门禁。
- 真实安装场景仍需继续补完三组手工验证：已安装 WebView2、可联网补装 WebView2、完全离线安装。
- 当前阶段未启用代码签名，因此安装包可能触发系统或安全软件的额外提示。

## License

本项目采用 [MIT License](./LICENSE)。
