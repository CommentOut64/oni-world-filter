# Desktop Packaging Single Chain Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将仓库收敛为仅面向 `Windows x64` 的单一桌面交付链，在开发机已预装 `Visual Studio C++ Build Tools / Rust / Node.js` 的前提下，用一条 PowerShell 命令稳定产出两个可交付的 NSIS 安装包：`standard` 与 `offline`。

**Architecture:** 只保留 `desktop/ + src-tauri/ + oni-sidecar(x64/MSVC)` 这条产品主线。先深度清理仓库中旧的 `Web/WASM`、`Pixi/MinGW`、`CLI` 公共产品分支和对应依赖，再把 `CMake`、`Tauri`、PowerShell 脚本、运行时路径策略、文档与验收门禁统一到一条 release orchestrator。双安装包不走自定义 NSIS 魔改，直接基于 Tauri Windows bundling 的 `webviewInstallMode` 做配置分流。

**Tech Stack:** PowerShell、CMake 3.20+、MSVC x64、Ninja、Rust、Tauri 2、React 19、Vite、NSIS、WebView2

---

## 先决说明

- 本计划把“剪去旧的分支”解释为：**剪掉仓库内部的旧产品/旧构建分支**，不是执行 Git branch 删除。
- 触发显式 ROI 评估的代码证据已经明确存在：
  - `scripts/dev-desktop.ps1` 仍走 `mingw-release`。
  - `scripts/build-desktop.ps1` 走 `x64-release`。
  - `src-tauri/src/sidecar.rs` 仍优先搜索 `mingw-*` sidecar。
  - `CMakePresets.json` 同时维护 `x64/x86/mingw/wasm` 四套预设。
- 因此本计划选择 **B. 局部重构**，但范围严格限制在“桌面打包链闭环”内：
  - 旧产品分支清理
  - native build 收敛
  - Tauri 打包与安装器变体
  - 桌面运行时路径
  - README 与 `llmdoc/` 当前态文档
- 不扩散到世界生成算法、搜索内核行为、现有 UI 业务逻辑。
- `commit` 仍由 `wgh` 手动执行，本计划不包含自动提交步骤。
- 当前阶段**不接入代码签名**，但脚本和产物布局必须预留未来签名接入点。
- 当前阶段**不做便携包**。理由很直接：未签名情况下，额外再维护一个 launcher + 便携目录，会增加杀软误报面和交付复杂度；先把 NSIS 安装链做稳，再决定是否开第二交付形态。
- 执行本计划前，若待删除/待重写路径上存在未提交变更，必须先向 `wgh` 二次确认，不能直接覆盖或删除。

## 单一主线目标状态

### 目标产品边界

- 保留的主线目录：
  - `asset/`
  - `3rdparty/`
  - `src/` 中与核心算法、sidecar、native tests 直接相关的代码
  - `desktop/`
  - `src-tauri/`
  - `scripts/`
  - `tests/`
  - `llmdoc/`
- 退役的公共产品路径：
  - 根目录 Web/WASM 路径
  - Pixi/MinGW 路径
  - CLI 可交付路径
  - 任何以 `oniWorldApp.exe` 为中心的发布、benchmark、README 入口

### 目标构建链

- 唯一 release 命令：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-desktop.ps1 -Variant both
```

- 该命令必须完成以下动作：
  - 校验版本一致性
  - 校验 VS x64/MSVC、Rust、Node、Tauri CLI
  - 清理陈旧构建缓存
  - 编译 `x64-release` 的 `oni-sidecar.exe`
  - 同步 `src-tauri/binaries/oni-sidecar.exe`
  - 连续构建 `standard` 与 `offline` 两个 NSIS 安装包
  - 将产物收口到统一 release 目录
  - 生成校验信息
  - 默认不签名，但保留签名参数和环境变量契约

### 目标安装包矩阵

- `standard`：
  - 体积较小
  - 使用 Tauri Windows bundling 的 `downloadBootstrapper`
  - 适合机器已安装 WebView2 或安装过程中可联网下载缺失 WebView2 的用户
- `offline`：
  - 体积更大
  - 使用 Tauri Windows bundling 的 `offlineInstaller`
  - 适合离线环境或机器缺失 WebView2 且不允许联网修复的用户

### 目标运行时目录策略

- 安装目录只放程序主体与只读资源，**不写用户数据**。
- 用户数据、缓存、日志必须由 Rust host 统一解析并分流：
  - `AppData` 体系：配置、用户持久化数据
  - `LocalAppData` 体系：缓存、日志、sidecar runtime copy、临时文件
- 前端不直接硬编码 Windows 路径，只消费 host 暴露的权威路径。

### 目标依赖面

- 保留的本机前置依赖：
  - Visual Studio C++ x64 工具链
  - Rust toolchain
  - Node.js
- 从主线中移除的前置依赖：
  - `Pixi`
  - `MinGW`
  - `EMSDK`
  - `Python` 作为桌面打包必需依赖

## File Map

### Files/Dirs To Delete From Mainline

- `package.json`
- `yarn.lock`
- `tsconfig.json`
- `tsconfig.web.json`
- `webpack.config.ts`
- `dev-web.bat`
- `public/`
- `src/index.tsx`
- `src/jsUtils/`
- `src/entry_wasm.cpp`
- `src/App/WasmResultSink.cpp`
- `src/JoinWasmFilesHash.py`
- `pixi.toml`
- `pixi.lock`
- `src/entry_cli.cpp`
- `scripts/bench/run-batch-benchmark.ps1`
- `scripts/bench/run-thread-policy-benchmark.ps1`
- `filter.json`

### Existing Files To Modify

- `.gitignore`
- `CMakeLists.txt`
- `src/CMakeLists.txt`
- `CMakePresets.json`
- `scripts/build-desktop.ps1`
- `scripts/dev-desktop.ps1`
- `scripts/dev-desktop.bat`
- `scripts/smoke/run-sidecar-analysis-latency.ps1`
- `scripts/smoke/run-sidecar-search-scale.ps1`
- `scripts/smoke/run-sidecar-search-start-latency.ps1`
- `scripts/smoke/run-sidecar-search-worker-ramp.ps1`
- `src-tauri/Cargo.toml`
- `src-tauri/tauri.conf.json`
- `src-tauri/build.rs`
- `src-tauri/src/main.rs`
- `src-tauri/src/commands.rs`
- `src-tauri/src/sidecar.rs`
- `README.md`
- `README.zh-CN.md`
- `llmdoc/index.md`
- `llmdoc/overview/project.md`
- `llmdoc/guides/batch-filter.md`
- `llmdoc/reference/filter-config.md`
- `llmdoc/decisions/2026-04-09-tauri-desktop-refactor-plan.md`

### New Files To Create

- `scripts/lib/desktop-bootstrap.ps1`
  - 职责：封装 VS 环境引导、工具链校验、版本一致性校验、缓存清理、sidecar 同步、release 输出收口与未来签名 hook。
- `scripts/verify-desktop-release.ps1`
  - 职责：校验“单一打包链”约束、release 目录完整性、双安装包存在性和 legacy 引用残留。
- `src-tauri/tauri.standard.conf.json`
  - 职责：覆盖 `standard` 安装器的 WebView2 安装模式与产物命名。
- `src-tauri/tauri.offline.conf.json`
  - 职责：覆盖 `offline` 安装器的 WebView2 安装模式与产物命名。
- `src-tauri/src/app_paths.rs`
  - 职责：统一定义安装目录、资源目录、AppData、LocalAppData、日志目录、runtime sidecar copy 目录的权威解析逻辑。

## Constraints

- 最终主线仅支持 `Windows x64`。
- 最终主线只产出 `NSIS installer`，不再保留 `targets: "all"`。
- 最终主线不依赖 `Pixi / MinGW / EMSDK / Python`。
- 最终主线不得再把 `oniWorldApp.exe` 当作公共交付物。
- release 脚本默认 `unsigned`，除非未来显式开启签名参数。
- release 结果必须能从一个固定目录打包、验收、归档。

## Non-Goals

- 不在本计划内补做代码签名采购与证书接入。
- 不在本计划内新增便携包 launcher。
- 不在本计划内支持 `x86`、`ARM64`、`MSI/MSIX`。
- 不在本计划内重写搜索协议、世界生成算法或桌面 UI 业务逻辑。

## Stop Conditions

- 待删除路径上发现 `wgh` 的未提交业务改动。
- desktop 当前功能仍然隐式依赖 `src/jsUtils/`、`entry_wasm.cpp` 或 `entry_cli.cpp` 的任何运行时代码。
- Tauri 双变体打包无法通过 `webviewInstallMode + --config` 实现，必须改走自定义 NSIS 时，应暂停并重新评估，不要边做边扩散。

## Success Criteria

- `cmake --list-presets` 只剩 `x64-debug` 与 `x64-release`。
- `scripts/build-desktop.ps1 -Variant both` 能在一台预装 `VS/Rust/Node` 的 Windows x64 开发机上稳定跑通。
- release 目录中同时存在 `standard` 和 `offline` 两个 NSIS 安装包。
- README 与 `llmdoc/` 不再把 Web/WASM、Pixi/MinGW、CLI 描述为当前主线。
- 安装后程序主体不写入安装目录，用户数据与缓存落入 `AppData/LocalAppData`。
- 未来签名所需参数位和产物收口点已经固定，但默认路径仍能在 `unsigned` 模式下直接出包。

## Chunk 0: 清理前冻结与风险闸门

### Task 1: 先冻结当前状态并确认删除边界

**Files:**
- Verify only

- [ ] **Step 1: 记录工作区状态**

Run:

```powershell
git status --short
```

Expected:

- 输出当前未提交变更清单。
- 如果待删除或待重写路径出现在输出中，先停下并与 `wgh` 确认保留策略。

- [ ] **Step 2: 建立 legacy baseline**

Run:

```powershell
rg -n "mingw-|wasm-|pixi|oniWorldApp.exe|entry_cli|entry_wasm|filter.json" `
  README.md README.zh-CN.md llmdoc scripts src-tauri src CMakePresets.json
```

Expected:

- 当前仓库会出现大量 legacy 命中，作为后续清理完成前后的对照基线。

- [ ] **Step 3: 明确保留面**

人工确认以下目录在整个清理过程中不得误删：

- `asset/`
- `3rdparty/`
- `desktop/`
- `src-tauri/`
- `src/Batch/`
- `src/BatchCpu/`
- `src/SearchAnalysis/`
- `src/App/` 中除 `WasmResultSink.cpp` 外的实现
- `tests/`

## Chunk 1: 深度清理项目目录并退役旧主线

### Task 2: 先清理本地陈旧环境与构建产物

**Files:**
- Modify local workspace only

- [ ] **Step 1: 删除旧环境与构建缓存**

Run:

```powershell
@(
  "out",
  ".pixi",
  "node_modules",
  "desktop/node_modules",
  "src-tauri/target",
  "src-tauri/binaries"
) | ForEach-Object {
  if (Test-Path -LiteralPath $_) {
    Remove-Item -LiteralPath $_ -Recurse -Force
  }
}
```

Expected:

- `Pixi`、旧 Node 安装、旧 Tauri bundle、旧 sidecar copy 全部移除。
- 后续验证必须建立在“从干净目录重新出包”的前提上。

- [ ] **Step 2: 验证缓存已清空**

Run:

```powershell
@(
  "out",
  ".pixi",
  "node_modules",
  "desktop/node_modules",
  "src-tauri/target",
  "src-tauri/binaries"
) | ForEach-Object {
  "{0}: {1}" -f $_, (Test-Path -LiteralPath $_)
}
```

Expected:

- 所有路径都返回 `False`。

### Task 3: 删除仓库中的 Web/WASM 与 CLI/Pixi 公共分支

**Files:**
- Delete: `package.json`
- Delete: `yarn.lock`
- Delete: `tsconfig.json`
- Delete: `tsconfig.web.json`
- Delete: `webpack.config.ts`
- Delete: `dev-web.bat`
- Delete: `public/`
- Delete: `src/index.tsx`
- Delete: `src/jsUtils/`
- Delete: `src/entry_wasm.cpp`
- Delete: `src/App/WasmResultSink.cpp`
- Delete: `src/JoinWasmFilesHash.py`
- Delete: `pixi.toml`
- Delete: `pixi.lock`
- Delete: `src/entry_cli.cpp`
- Delete: `scripts/bench/run-batch-benchmark.ps1`
- Delete: `scripts/bench/run-thread-policy-benchmark.ps1`
- Delete: `filter.json`

- [ ] **Step 1: 执行 tracked file 删除**

Run:

```powershell
git rm `
  package.json `
  yarn.lock `
  tsconfig.json `
  tsconfig.web.json `
  webpack.config.ts `
  dev-web.bat `
  pixi.toml `
  pixi.lock `
  src/index.tsx `
  src/entry_wasm.cpp `
  src/entry_cli.cpp `
  src/App/WasmResultSink.cpp `
  src/JoinWasmFilesHash.py `
  scripts/bench/run-batch-benchmark.ps1 `
  scripts/bench/run-thread-policy-benchmark.ps1 `
  filter.json

git rm -r public src/jsUtils
```

Expected:

- root Web、Pixi、CLI 公共入口文件全部 staged for deletion。
- 若任一路径因本地变更无法删除，立即停止并回到 `Task 1 / Step 1` 处理。

- [ ] **Step 2: 验证旧入口已从目录结构中消失**

Run:

```powershell
@(
  "package.json",
  "pixi.toml",
  "src/entry_wasm.cpp",
  "src/entry_cli.cpp",
  "src/jsUtils",
  "public"
) | ForEach-Object {
  "{0}: {1}" -f $_, (Test-Path -LiteralPath $_)
}
```

Expected:

- 上述路径全部返回 `False`。

## Chunk 2: 收敛 native build 为 x64/MSVC + sidecar + tests

### Task 4: 简化 CMake 根配置与源码入口

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `src/CMakeLists.txt`
- Modify: `CMakePresets.json`
- Modify: `.gitignore`

- [ ] **Step 1: 移除 `EMSCRIPTEN`、`PYTHON_CMD` 和多入口切换**

实施要求：

- `CMakeLists.txt` 删除与 `EMSDK_PYTHON`、`find_program(PYTHON_CMD ...)` 相关逻辑。
- `src/CMakeLists.txt` 删除 `oniWorldApp` 可执行目标与 `entry_wasm.cpp` / `entry_cli.cpp` 入口切换。
- 保留 `oni-sidecar` 与 native tests。
- `CMakePresets.json` 仅保留：
  - `x64-debug`
  - `x64-release`
- 删除：
  - `x86-*`
  - `mingw-*`
  - `wasm-*`

- [ ] **Step 2: 清理 `.gitignore` 中已无意义的规则**

实施要求：

- 删除与 root `node_modules/`、`.pixi/`、`/llmdoc`、`/devdoc` 这类旧规则的误导性配置。
- 保留实际仍需忽略的活跃路径：
  - `out/`
  - `desktop/node_modules/`
  - `desktop/dist/`
  - `src-tauri/target/`
  - `src-tauri/binaries/`

- [ ] **Step 3: 验证 preset 与 configure 行为**

Run:

```powershell
cmake --list-presets
cmake --preset x64-release
```

Expected:

- 只显示 `x64-debug` 与 `x64-release`。
- `cmake --preset x64-release` 不再依赖 `Pixi`、`MinGW`、`Python`、`EMSDK`。

- [ ] **Step 4: 全量构建 native 主线并跑 CTest**

Run:

```powershell
cmake --build out/build/x64-release
ctest --test-dir out/build/x64-release -C Release --output-on-failure
```

Expected:

- 默认构建目标里不再包含 `oniWorldApp.exe`。
- `oni-sidecar.exe` 与 native tests 可以在 `x64-release` 下完整构建和运行。

## Chunk 3: 收敛桌面开发脚本与 sidecar 解析逻辑

### Task 5: 建立唯一的 PowerShell bootstrap 层

**Files:**
- Create: `scripts/lib/desktop-bootstrap.ps1`
- Modify: `scripts/build-desktop.ps1`
- Modify: `scripts/dev-desktop.ps1`
- Modify: `scripts/dev-desktop.bat`
- Modify: `src-tauri/build.rs`
- Modify: `src-tauri/src/sidecar.rs`
- Modify: `src-tauri/Cargo.toml`
- Modify: `src-tauri/tauri.conf.json`

- [ ] **Step 1: 抽取共享 bootstrap**

`desktop-bootstrap.ps1` 必须集中实现：

- `Assert-VersionConsistency`
  - `src-tauri/Cargo.toml` 的 `package.version`
  - `src-tauri/tauri.conf.json` 的 `version`
  - 不一致直接失败
- `Assert-VsToolchain`
  - 自动定位 `vswhere`
  - 加载 `VsDevCmd.bat -arch=x64`
- `Assert-NodeAndYarn`
- `Assert-RustAndCargoTauri`
- `Repair-StaleCMakeCache`
  - 重点清理曾经由 `mingw` 污染出的 `x64-release` cache
- `Build-AndSyncSidecar`
  - `x64-release` 给 dev
  - `x64-release` 给 build
- `Collect-ReleaseArtifacts`
- `Invoke-OptionalCodeSigning`
  - 默认 `unsigned`
  - 仅定义参数与环境变量契约，不默认启用

- [ ] **Step 2: 收敛脚本职责**

实施要求：

- `scripts/build-desktop.ps1` 变成**唯一 release orchestrator**。
- `scripts/dev-desktop.ps1` 只负责编译并启动 desktop dev，不再自己复制 bootstrap 逻辑。
- `scripts/dev-desktop.bat` 只做最薄转发，不再维护第二套核心逻辑。

- [ ] **Step 3: 收敛 sidecar 解析候选路径**

实施要求：

- `src-tauri/src/sidecar.rs` 删除所有 `mingw-*` 候选路径与“优先选 mingw”测试。
- 保留的候选顺序只允许：
  - `src-tauri/binaries/oni-sidecar.exe`
  - `resourceDir/oni-sidecar*.exe`
  - `out/build/x64-release/src/oni-sidecar.exe`
- 若使用 runtime copy，落点必须来自 `app_paths.rs` 提供的 LocalAppData 子目录。

- [ ] **Step 4: 让 `build.rs` 对 release bundle fail-fast**

实施要求：

- 不再用空白 placeholder 掩盖 sidecar 缺失。
- 对真正的打包场景，若 `src-tauri/binaries/oni-sidecar.exe` 缺失或为零字节，直接失败。
- 对仅 `cargo check` 的开发场景，可以保留最小宽容，但不能让正式打包悄悄带入空二进制。

- [ ] **Step 5: 验证桌面主线脚本与 Rust sidecar 测试**

Run:

```powershell
cargo test --manifest-path src-tauri/Cargo.toml sidecar::tests -- --nocapture
powershell -ExecutionPolicy Bypass -File .\scripts\dev-desktop.ps1 -SkipYarnInstall
```

Expected:

- Rust sidecar 路径测试不再出现 `mingw-*` 假设。
- `dev-desktop.ps1` 可以在没有 `Pixi/MinGW/Python` 的前提下启动桌面开发链。

## Chunk 4: 收敛 Tauri 打包链并产出双安装包

### Task 6: 基于 Tauri config overlay 产出 `standard` / `offline`

**Files:**
- Modify: `src-tauri/tauri.conf.json`
- Create: `src-tauri/tauri.standard.conf.json`
- Create: `src-tauri/tauri.offline.conf.json`
- Modify: `scripts/build-desktop.ps1`

- [ ] **Step 1: 把 base config 收敛为 NSIS-only**

实施要求：

- `src-tauri/tauri.conf.json` 的 `bundle.targets` 从 `"all"` 改为 `["nsis"]`。
- base config 只保留公共字段：
  - `productName`
  - `version`
  - `identifier`
  - `resources`
  - `icons`
  - `build.beforeDevCommand`
  - `build.beforeBuildCommand`

- [ ] **Step 2: 建立两个 overlay config**

`src-tauri/tauri.standard.conf.json` 只覆盖 `standard` 变体必需字段：

- `bundle.windows.webviewInstallMode.type = "downloadBootstrapper"`
- installer 名称或输出后缀要能区分 `standard`

`src-tauri/tauri.offline.conf.json` 只覆盖 `offline` 变体必需字段：

- `bundle.windows.webviewInstallMode.type = "offlineInstaller"`
- installer 名称或输出后缀要能区分 `offline`

- [ ] **Step 3: 让 release 脚本顺序构建两个变体**

`build-desktop.ps1` 至少支持：

```powershell
-Variant standard
-Variant offline
-Variant both
-SigningProfile unsigned
```

构建顺序固定为：

1. 校验版本
2. 安装桌面前端依赖
3. `corepack yarn --cwd desktop build`
4. `Build-AndSyncSidecar -Configuration Release`
5. `cargo tauri build --config src-tauri/tauri.standard.conf.json`
6. 收口 `standard` 产物
7. `cargo tauri build --config src-tauri/tauri.offline.conf.json`
8. 收口 `offline` 产物
9. 输出校验摘要

- [ ] **Step 4: 统一产物目录**

目标目录固定为：

```text
out/release/desktop/<version>/
  standard/
  offline/
  checksums.txt
  build-summary.json
```

要求：

- 不直接把最终交付物留在 `src-tauri/target/release/bundle/` 当手工取件目录。
- release 脚本结束后，应打印这两个安装包的绝对路径。

- [ ] **Step 5: 验证双安装包确实能产出**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-desktop.ps1 -Variant both
Get-ChildItem .\out\release\desktop -Recurse -File | Select-Object FullName
```

Expected:

- 输出中只出现 NSIS 安装包，不出现 MSI。
- `standard` 与 `offline` 两个安装包都存在。
- `offline` 包体积显著大于 `standard`。

## Chunk 5: 固化安装目录与用户数据目录策略

### Task 7: 让 Rust host 成为路径权威层

**Files:**
- Create: `src-tauri/src/app_paths.rs`
- Modify: `src-tauri/src/main.rs`
- Modify: `src-tauri/src/commands.rs`
- Modify: `src-tauri/src/sidecar.rs`

- [ ] **Step 1: 新建 `app_paths.rs`**

至少提供以下能力：

- `resolve_install_resource_dir()`
- `resolve_app_data_dir()`
- `resolve_app_local_data_dir()`
- `resolve_app_log_dir()`
- `resolve_runtime_sidecar_dir()`

要求：

- 所有路径通过 Tauri/Rust host API 统一解析。
- 不在前端、PowerShell 脚本或 sidecar 内部散落硬编码 Windows 路径。

- [ ] **Step 2: 收敛路径使用点**

实施要求：

- `sidecar.rs` 的 runtime copy、临时日志、缓存目录全部通过 `app_paths.rs` 获取。
- 如需对前端展示或诊断路径，可在 `commands.rs` 暴露只读查询命令，但仅返回权威路径字符串，不暴露平台判断逻辑。

- [ ] **Step 3: 定义安装后行为约束**

必须满足：

- 安装目录只读可升级。
- 首次启动后新增的数据、日志、缓存不写回安装目录。
- 删除用户数据不要求卸载重装主程序。

- [ ] **Step 4: 验证路径策略**

Run:

```powershell
cargo test --manifest-path src-tauri/Cargo.toml app_paths -- --nocapture
```

Manual Check:

- 安装应用并首次启动一次
- 检查安装目录未出现新增用户数据
- 检查 `AppData/LocalAppData` 下出现程序自己的数据与日志目录

Expected:

- 用户数据与缓存与安装目录彻底分离。

## Chunk 6: 重写当前态文档并加最终门禁

### Task 8: 把 README 与 `llmdoc/` 改写到 desktop-only 当前态

**Files:**
- Modify: `README.md`
- Modify: `README.zh-CN.md`
- Modify: `llmdoc/index.md`
- Modify: `llmdoc/overview/project.md`
- Modify: `llmdoc/guides/batch-filter.md`
- Modify: `llmdoc/reference/filter-config.md`
- Modify: `llmdoc/decisions/2026-04-09-tauri-desktop-refactor-plan.md`

- [ ] **Step 1: 重写 README**

必须删掉或改写：

- Web/WASM 快速开始
- Pixi/MinGW CLI 快速开始
- “桌面端只是骨架” 之类过期描述

必须新增：

- 唯一主线是 `desktop + src-tauri + oni-sidecar`
- 一键出包命令
- 双安装包说明
- WebView2 `standard/offline` 使用建议
- 当前阶段未启用代码签名

- [ ] **Step 2: 重写 `llmdoc/index.md` 与 `llmdoc/overview/project.md`**

实施要求：

- 移除 Web/WASM、CLI 作为当前主线的表述。
- 把桌面主路径、安装目录与数据目录策略写成当前事实。
- `llmdoc/index.md` 不再把 `filter.json`/CLI 作为核心入口。

- [ ] **Step 3: 处理 CLI 相关文档**

处理原则：

- 如果 `llmdoc/guides/batch-filter.md` 和 `llmdoc/reference/filter-config.md` 只服务于已退役 CLI 公共路径，则删除或改写为“内部协议/历史资料”，不能继续作为当前用户文档入口。
- `llmdoc/decisions/2026-04-09-tauri-desktop-refactor-plan.md` 若仍描述 `entry_cli.cpp` / `entry_wasm.cpp` 为目标文件，必须整体重写或下架引用，不能在原文后追加补丁说明。

### Task 9: 建立 release 验证脚本与最终验收矩阵

**Files:**
- Create: `scripts/verify-desktop-release.ps1`

- [ ] **Step 1: 编写静态门禁**

`verify-desktop-release.ps1` 至少校验：

- legacy tracked files 已不存在：
  - `package.json`
  - `pixi.toml`
  - `src/entry_wasm.cpp`
  - `src/entry_cli.cpp`
- `CMakePresets.json` 只剩 `x64-debug` / `x64-release`
- `src-tauri/tauri.standard.conf.json` 与 `src-tauri/tauri.offline.conf.json` 存在
- release 目录中同时存在两个 NSIS 安装包

- [ ] **Step 2: 编写 grep 门禁**

Run inside script:

```powershell
rg -n "mingw-|wasm-|pixi|oniWorldApp.exe|entry_cli|entry_wasm|filter.json" `
  README.md README.zh-CN.md llmdoc scripts src-tauri src CMakePresets.json `
  --glob "!docs/superpowers/**" `
  --glob "!src-tauri/target/**"
```

Expected:

- 只允许命中明确保留的历史目录或测试说明；默认应趋近于 `0`。

- [ ] **Step 3: 执行最终 build + verify**

Run:

```powershell
corepack yarn --cwd desktop install --frozen-lockfile
corepack yarn --cwd desktop build
cmake --preset x64-release
cmake --build out/build/x64-release
ctest --test-dir out/build/x64-release -C Release --output-on-failure
cargo test --manifest-path src-tauri/Cargo.toml
powershell -ExecutionPolicy Bypass -File .\scripts\build-desktop.ps1 -Variant both
powershell -ExecutionPolicy Bypass -File .\scripts\verify-desktop-release.ps1
```

Expected:

- 从零开始构建时不需要 `Pixi`、`MinGW`、`Python`、`EMSDK`。
- 所有测试与 release 门禁通过。

- [ ] **Step 4: 按真实交付场景做安装验证**

最少验证 3 组环境：

1. Windows 11，已安装 WebView2
2. Windows 10，未安装 WebView2，但允许联网
3. Windows 10 或 11，未安装 WebView2，且断网

验收要求：

- `standard` 在场景 1 正常安装和启动。
- `standard` 在场景 2 能通过 bootstrapper 修复缺失 WebView2 后正常启动。
- `offline` 在场景 3 能不依赖网络直接安装并启动。

## Acceptance Checklist

- [ ] 仓库只保留 desktop 单一交付主线
- [ ] 根目录 Web/WASM、Pixi、CLI 公共入口与依赖已删除
- [ ] `cmake --list-presets` 只剩 `x64-debug` / `x64-release`
- [ ] `build-desktop.ps1` 成为唯一 release orchestrator
- [ ] `standard` / `offline` 两个 NSIS 安装包可以一键产出
- [ ] release 构建不再要求 `Pixi / MinGW / EMSDK / Python`
- [ ] 安装目录与用户数据目录已分离
- [ ] README 与 `llmdoc/` 已同步到 desktop-only 当前态
- [ ] 默认 `unsigned` 也可直接出包，但未来签名 hook 已预留

## Notes For Execution

- 这是一次“先砍旧链、再固化新链”的收敛工程，不能边保留旧入口边宣称“单一打包链”已经成立。
- 旧目录清理必须先做，否则后续任何脚本、自测、README、sidecar 路径选择都还会继续被旧链污染。
- 若执行过程中发现某个 desktop 功能仍依赖已退役的 Web/CLI 文件，不要偷偷保留旧文件；应把依赖迁回桌面主线，或暂停并重新定界。

Plan complete and saved to `docs/superpowers/plans/2026-04-23-desktop-packaging-single-chain.md`. Ready to execute?
