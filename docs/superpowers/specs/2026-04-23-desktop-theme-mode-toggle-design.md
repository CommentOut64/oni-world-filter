# Desktop 明暗主题切换设计

> Type: Design | Status: Approved

## 目标

在不改动 `desktop/` 业务状态、Tauri 协议和搜索流程的前提下，为桌面前端补上一套可切换的明暗主题。实现优先复用 `antd` 原生主题算法，并把现有自定义深色样式收敛为可切换的 CSS 变量。

## 当前代码事实

- `desktop/src/app/antdTheme.ts` 目前只导出固定深色 `desktopTheme`，并使用 `theme.darkAlgorithm`。
- `desktop/src/main.tsx` 在根节点把固定 `desktopTheme` 注入 `ConfigProvider`，没有主题状态。
- `desktop/src/app/app.css` 仍以 `:root` 写死深色变量，并包含大量自定义背景、边框和文字颜色。
- `desktop/src/components/layout/TopBar.tsx` 已存在但未接入主界面，是最合适的最小入口，可承载主题切换控件而不扩散到业务组件。

## 方案

### 1. antd 主题层

把 `desktop/src/app/antdTheme.ts` 从“固定深色对象”改为“按模式返回 ThemeConfig 的主题工厂”：

- `dark` 使用 `theme.darkAlgorithm`
- `light` 使用 `theme.defaultAlgorithm`
- 保留统一 token 结构，只拆分出深浅两套基础色值
- 继续使用 `cssVar.key = "oni-desktop"`

### 2. 根入口状态层

在 `desktop/src/main.tsx` 新增最小主题状态：

- 默认跟随系统 `prefers-color-scheme`
- 用 React state 驱动 `ConfigProvider theme`
- 同步 `document.documentElement.dataset.theme = "light" | "dark"`

主题状态只放在根入口，避免进入 `App.tsx` 的页面流转和 store。

### 3. 页面入口层

接入 `TopBar` 作为全局主题切换入口：

- 由 `App.tsx` 在搜索页和结果页上方统一渲染
- 使用 `antd` 的 `Segmented`，只提供“浅色 / 深色”两档
- `HostDebugWindow` 路径保持不变，不额外接线

### 4. CSS 变量层

把 `app.css` 顶部的深色 `:root` 改为：

- `html[data-theme="dark"] { ... }`
- `html[data-theme="light"] { ... }`

并只修正本次切换会直接失真的样式：

- 页面背景渐变
- panel / card 外壳背景与边框
- 统计卡背景
- 预览和结果页中少量写死的深色透明层

不做无关样式清理，不重写现有布局系统。

## 成功标准

1. `antd` 组件和自定义页面壳层都能随主题同步切换。
2. 浅色主题下不残留大面积深色面板或不可读文字。
3. 深色主题保持当前视觉风格基本不变。
4. 改动范围限制在主题工厂、根入口、顶部栏和全局样式层。
