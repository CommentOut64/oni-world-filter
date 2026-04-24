# oniWorldApp

> A Windows x64 desktop app for Oxygen Not Included world generation and seed filtering.

[![中文 README](https://img.shields.io/badge/README-中文-blue.svg)](./README.zh-CN.md)
![License](https://img.shields.io/badge/license-MIT-blue.svg)
![C++](https://img.shields.io/badge/C%2B%2B-23-00599C?logo=cplusplus&logoColor=white)
![React](https://img.shields.io/badge/React-19-61DAFB?logo=react&logoColor=black)
![Tauri](https://img.shields.io/badge/Tauri-2.x-24C8DB?logo=tauri&logoColor=white)


oniWorldApp is a local desktop tool for generating, searching, and previewing Oxygen Not Included world seeds. It reuses the C++ world-generation core and provides a Windows desktop interface through Tauri and React.

## Features

- Search usable seeds by world type and filter conditions.
- Review search results and preview the corresponding map information.
- Show an optimistic estimated match probability and major bottleneck hints before searching.
- Run computation locally without relying on a remote search service.

## Tech Stack

| Area | Technology |
| --- | --- |
| Desktop UI | React 19, TypeScript, Vite |
| Desktop host | Tauri 2, Rust |
| World-generation core | C++23 |
| Build and packaging | CMake, MSVC x64, PowerShell, NSIS |

## Development Requirements

These requirements only apply when running from source, developing, or building installers. End users installing a prebuilt installer do not need these development tools.

- Windows 10/11 x64
- Visual Studio C++ x64 Build Tools
- Rust toolchain
- Node.js
- `yarn` or `corepack`
- Tauri CLI

Install Tauri CLI:

```powershell
cargo install tauri-cli --version "^2.0.0"
```

## Quick Start

Start the desktop app in development mode:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\dev-desktop.ps1
```

This script prepares the desktop frontend and local world-generation core, then starts the Tauri development environment.

## Build Installers

Generate Windows installers:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-desktop.ps1 -Variant both
```

Build outputs are written to `out/release/desktop/<version>/`. The project currently provides two installer variants:

- `standard`: smaller package, suitable for machines with WebView2 already installed or with network access to install the runtime.
- `offline`: larger package with the WebView2 offline installer included, suitable for offline environments.

Installers are currently unsigned by default, so Windows or security software may show additional prompts during installation.

## Project Structure

```text
oni_world_app-master/
├── asset/       Game worldgen resources and templates
├── desktop/     React + Vite desktop frontend
├── src-tauri/   Tauri 2 Rust host and packaging config
├── src/         C++ world-generation core and sidecar entry
├── scripts/     Development, build, and verification scripts
├── tests/       Native tests and smoke fixtures
└── llmdoc/      Current-state architecture docs
```

## License

This project is licensed under the [MIT License](./LICENSE).
