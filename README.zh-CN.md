# oniWorldApp

> 面向 Windows x64 的《缺氧》世界生成与筛种桌面应用。

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![C++](https://img.shields.io/badge/C%2B%2B-23-00599C?logo=cplusplus&logoColor=white)
![React](https://img.shields.io/badge/React-19-61DAFB?logo=react&logoColor=white)
![Tauri](https://img.shields.io/badge/Tauri-2.x-24C8DB?logo=tauri&logoColor=white)

<img src="./public/00.png" width="100%" alt="oniWorldApp 预览">

oniWorldApp 是一个本地桌面工具，用于生成、搜索和预览《缺氧》的世界种子。项目复用 C++ 世界生成核心，并通过 Tauri 与 React 提供 Windows 桌面界面。

## 主要功能

- 按世界类型和筛选条件搜索可用种子。
- 查看搜索结果，并预览对应地图信息。
- 在搜索前给出乐观估计可匹配概率和主要瓶颈提示。
- 在本地完成计算，不依赖远程搜索服务。

## 技术栈

| 模块 | 技术 |
| --- | --- |
| 桌面界面 | React 19、TypeScript、Vite |
| 桌面宿主 | Tauri 2、Rust |
| 世界生成核心 | C++23 |
| 构建与打包 | CMake、MSVC x64、PowerShell、NSIS |

## 开发环境要求

以下要求仅适用于从源码运行、开发或构建交付包；普通用户使用已构建产物不需要准备这些开发工具。

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

在开发模式下启动桌面应用：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\dev-desktop.ps1
```

该脚本会准备桌面前端和本地世界生成核心，然后启动 Tauri 开发环境。

## 构建交付包

生成 `Setup + Portable-standard + Portable-offline`：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-desktop.ps1 -Package all -Variant both
```

构建结果会输出到 `out/release/desktop/<version>/`。当前主线产物为：

- `installer/oni-world-filter-<version>-Setup.exe`
- `portable-standard/oni-world-filter-<version>-Portable-standard.zip`
- `portable-offline/oni-world-filter-<version>-Portable-offline.zip`

说明：

- `Setup`：标准 NSIS 安装包。
- `Portable-standard`：免安装便携包，不写 `AppData`，运行期数据固定落在程序同级 `.\data\`。
- `Portable-offline`：在 `Portable-standard` 基础上额外携带 WebView2 Fixed Runtime，适合离线机器。

构建 `Portable-offline` 前，需要先设置 `ONI_WEBVIEW2_FIXED_RUNTIME_DIR` 指向本机已解压的 WebView2 Fixed Runtime 目录。

当前产物默认未启用代码签名，运行或安装时可能出现系统或安全软件提示。

## 项目结构

```text
oni_world_app-master/
├── asset/       游戏 worldgen 资源与模板
├── desktop/     React + Vite 桌面前端
├── src-tauri/   Tauri 2 Rust 宿主与打包配置
├── src/         C++ 世界生成核心与 sidecar 入口
├── scripts/     开发、构建与验收脚本
├── tests/       native tests 与 smoke 夹具
└── llmdoc/      当前态架构文档
```

## License

本项目采用 [MIT License](./LICENSE)。
