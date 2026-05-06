# Desktop Portable Packaging Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在保留单一 installer 的同时，新增 `Portable-standard` 与 `Portable-offline` 两个真正不写 `AppData` 的便携包。

**Architecture:** 继续复用现有 `build-desktop.ps1` 作为唯一 orchestrator。Rust host 通过 `portable.flag` 切换到便携模式，把 app data、日志、runtime sidecar 与 WebView2 user data 全部重定向到 `exe` 同级的 `data/`，脚本再分别收口 installer 与两种 portable 产物。

**Tech Stack:** PowerShell、Rust、Tauri 2、WebView2、Node.js

---

## Chunk 1: 先锁定运行时 portable 约束

### Task 1: 为 portable 路径策略补测试

**Files:**
- Modify: `src-tauri/src/app_paths.rs`
- Modify: `src-tauri/src/main.rs`
- Test: `src-tauri/src/app_paths.rs`

- [ ] **Step 1: 写出 portable 路径的失败测试**
- [ ] **Step 2: 运行 Rust 测试，确认新测试先失败**
- [ ] **Step 3: 最小实现 portable 路径解析与 WebView2 data directory 重定向**
- [ ] **Step 4: 重新运行相关 Rust 测试，确认转绿**

## Chunk 2: 收口新的发布矩阵

### Task 2: 改造构建脚本与产物命名

**Files:**
- Modify: `scripts/build-desktop.ps1`
- Modify: `scripts/lib/desktop-bootstrap.ps1`
- Modify: `src-tauri/tauri.conf.json`
- Delete or stop using: `src-tauri/tauri.offline.conf.json`

- [ ] **Step 1: 为新产物矩阵写脚本测试或静态验收断言**
- [ ] **Step 2: 运行脚本相关测试，确认先失败**
- [ ] **Step 3: 最小实现 installer + portable-standard + portable-offline 收口**
- [ ] **Step 4: 重新运行相关测试，确认转绿**

## Chunk 3: 更新验收门禁与当前态文档

### Task 3: 调整 release 验证与文档

**Files:**
- Modify: `scripts/verify-desktop-release.ps1`
- Modify: `README.md`
- Modify: `README.zh-CN.md`
- Modify: `llmdoc/index.md`
- Modify: `llmdoc/overview/project.md`

- [ ] **Step 1: 更新 release 验证门禁到 3 产物矩阵**
- [ ] **Step 2: 同步 README 与 llmdoc 当前态**
- [ ] **Step 3: 跑相关测试与脚本验证**

