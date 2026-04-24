# Desktop Theme Mode Toggle Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 `desktop/` 增加最小可工作的浅色/深色主题切换，并优先复用 antd 原生主题算法。

**Architecture:** 主题状态仅存在于 `desktop/src/main.tsx` 根入口；`desktop/src/app/antdTheme.ts` 提供按模式生成的 `ThemeConfig`；`desktop/src/app/app.css` 通过 `html[data-theme]` 切换自定义 CSS 变量；`TopBar` 作为唯一全局切换入口接入 `App.tsx`。不改 store、Tauri、sidecar 和业务提交流程。

**Tech Stack:** React 19、TypeScript、antd v6、node:test、Vite、PowerShell

---

## 文件边界

- Modify: `desktop/src/app/antdTheme.ts`
- Modify: `desktop/src/main.tsx`
- Modify: `desktop/src/app/App.tsx`
- Modify: `desktop/src/components/layout/TopBar.tsx`
- Modify: `desktop/src/app/app.css`
- Modify: `desktop/tests/antdTheme.test.ts`
- Modify: `llmdoc/overview/project.md`

## Chunk 1: 主题工厂与入口状态

### Task 1: 先写主题工厂测试

**Files:**
- Modify: `desktop/tests/antdTheme.test.ts`
- Test: `desktop/tests/antdTheme.test.ts`

- [ ] Step 1: 为 `dark` / `light` 两种模式写出期望 token 测试
- [ ] Step 2: 为根入口需要的主题模式同步函数写测试
- [ ] Step 3: 运行 `node --test desktop/tests/antdTheme.test.ts`，确认先失败

### Task 2: 实现主题工厂与根入口状态

**Files:**
- Modify: `desktop/src/app/antdTheme.ts`
- Modify: `desktop/src/main.tsx`

- [ ] Step 1: 将固定 `desktopTheme` 改为 `createDesktopTheme(mode)`
- [ ] Step 2: 新增 `DesktopThemeMode`、系统默认模式解析和 `documentElement` 同步函数
- [ ] Step 3: 在 `main.tsx` 用状态驱动 `ConfigProvider`
- [ ] Step 4: 运行 `node --test desktop/tests/antdTheme.test.ts`，确认通过

## Chunk 2: 顶部切换入口

### Task 3: 接入最小主题切换 UI

**Files:**
- Modify: `desktop/src/components/layout/TopBar.tsx`
- Modify: `desktop/src/app/App.tsx`

- [ ] Step 1: 为 `TopBar` 增加 `mode` 和 `onModeChange`
- [ ] Step 2: 用 `Segmented` 渲染“浅色/深色”切换
- [ ] Step 3: 在 `App.tsx` 顶部统一接入，不改变页面骨架流转
- [ ] Step 4: 视情况补一条 SSR 标记测试或构建验证

## Chunk 3: CSS 变量双主题

### Task 4: 将全局壳层样式切为 `data-theme`

**Files:**
- Modify: `desktop/src/app/app.css`

- [ ] Step 1: 将 `:root` 深色变量拆为 `html[data-theme=\"dark\"]` / `html[data-theme=\"light\"]`
- [ ] Step 2: 修正最关键的硬编码深色面板和背景
- [ ] Step 3: 保持现有响应式和页面骨架不变

## Chunk 4: 文档与验证

### Task 5: 同步项目文档并做最终验证

**Files:**
- Modify: `llmdoc/overview/project.md`

- [ ] Step 1: 记录 desktop 已支持明暗主题切换
- [ ] Step 2: 运行 `node --test desktop/tests/antdTheme.test.ts`
- [ ] Step 3: 运行 `corepack yarn --cwd desktop build`
- [ ] Step 4: 如实记录是否存在未覆盖的手工视觉验证项
