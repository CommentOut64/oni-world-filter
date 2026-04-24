# Desktop 正式名称映射 Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 `desktop` 前端建立世界类型、mixing slot、喷口/泉的正式中英文展示映射，并统一替换所有用户可见显示，同时保持后端协议、旧前端和日志不变。

**Architecture:** 在 `desktop/src/lib` 新增一层纯前端展示用的命名字典和 resolver，不修改 sidecar/Tauri/C++ 字段结构。所有下拉、结果列表、预览详情、分析提示与展示型导出都改为通过统一 formatter 输出 `中文（English）`，协议型 JSON 导出继续保留内部字段和值。

**Tech Stack:** React 19、TypeScript、Zustand、Vite、Node 22、PowerShell

---

## 文件结构

### 新增文件

- `desktop/src/lib/displayNames.ts`
  - 世界类型、喷口/泉、mixing slot 的正式中英文静态注册表
- `desktop/src/lib/displayResolvers.ts`
  - 展示名解析与统一格式化函数
- `desktop/scripts/verify-display-name-coverage.mjs`
  - 最小覆盖验证脚本，确保当前世界 code、喷口 key、mixing slot 都有展示名

### 修改文件

- `desktop/src/features/search/WorldSelector.tsx`
  - 世界类型下拉显示正式名称
- `desktop/src/features/search/MixingSelector.tsx`
  - mixing slot 标题、说明与禁用提示显示正式名称
- `desktop/src/features/search/GeyserConstraintEditor.tsx`
  - required/forbidden 选择器显示正式喷口名
- `desktop/src/features/search/DistanceRuleEditor.tsx`
  - distance 规则选择器显示正式喷口名
- `desktop/src/features/search/CountRuleEditor.tsx`
  - count 规则选择器显示正式喷口名
- `desktop/src/features/results/resultColumns.tsx`
  - 结果表喷口概览显示正式喷口名
- `desktop/src/features/preview/PreviewDetails.tsx`
  - 预览详情与喷口列表显示正式喷口名
- `desktop/src/features/search/SearchAnalysisHints.tsx`
  - `bottlenecks` 由内部 id 改为正式喷口名
- `desktop/src/features/search/SearchPanel.tsx`
  - 明确保留 `copyAsJson()` 的协议导出边界，必要时补充提示文案
- `desktop/src/lib/searchCatalog.ts`
  - 如实现需要，暴露 fallback 目录给 coverage 脚本复用
- `llmdoc/decisions/2026-04-09-tauri-desktop-refactor-plan.md`
  - 同步记录 desktop 前端新增正式名称解析层

### 只读参考文件

- `docs/superpowers/specs/2026-04-13-desktop-display-name-mapping-design.md`
- `llmdoc/reference/filter-config.md`
- `src/entry_cli.cpp`
- `src-tauri/src/sidecar.rs`
- `desktop/src/lib/contracts.ts`

## Chunk 1: 建立正式命名字典与覆盖校验

### Task 1: 建立展示命名基础设施

**Files:**
- Create: `desktop/src/lib/displayNames.ts`
- Create: `desktop/src/lib/displayResolvers.ts`
- Create: `desktop/scripts/verify-display-name-coverage.mjs`
- Modify: `desktop/src/lib/searchCatalog.ts`
- Reference: `llmdoc/reference/filter-config.md`
- Reference: `src/entry_cli.cpp`

- [ ] **Step 1: 写一个会失败的覆盖校验脚本**

目标：

- 校验 `FALLBACK_SEARCH_CATALOG.worlds` 中全部 `world.code`
- 校验 `FALLBACK_SEARCH_CATALOG.geysers` 中全部 `geyser.key`
- 校验 11 个 mixing slot 正式名是否齐全

脚本建议结构：

```js
import assert from "node:assert/strict";
import { FALLBACK_SEARCH_CATALOG } from "../src/lib/searchCatalog.ts";
import {
  WORLD_DISPLAY_NAMES,
  GEYSER_DISPLAY_NAMES,
  MIXING_SLOT_DISPLAY_NAMES,
} from "../src/lib/displayNames.ts";

for (const world of FALLBACK_SEARCH_CATALOG.worlds) {
  assert.ok(WORLD_DISPLAY_NAMES[world.code], `缺少 world code 映射: ${world.code}`);
}
for (const geyser of FALLBACK_SEARCH_CATALOG.geysers) {
  assert.ok(GEYSER_DISPLAY_NAMES[geyser.key], `缺少 geyser key 映射: ${geyser.key}`);
}
for (const path of EXPECTED_MIXING_SLOT_PATHS) {
  assert.ok(MIXING_SLOT_DISPLAY_NAMES[path], `缺少 mixing slot 映射: ${path}`);
}
```

- [ ] **Step 2: 运行脚本，确认它先失败**

Run:

```powershell
node --experimental-strip-types desktop/scripts/verify-display-name-coverage.mjs
```

Expected:

- FAIL
- 错误明确指出缺少 `displayNames.ts` 或缺少具体映射项

- [ ] **Step 3: 建立正式名称注册表**

在 `desktop/src/lib/displayNames.ts` 中定义：

- `DisplayName`
- `WORLD_DISPLAY_NAMES`
- `GEYSER_DISPLAY_NAMES`
- `MIXING_SLOT_DISPLAY_NAMES`

实施要求：

- 世界类型主键使用 `world.code`
- 喷口主键使用 `geyser.key`
- mixing slot 主键使用 `path`
- 所有最终展示值必须可生成 `中文（English）`
- 不允许把“推导英文名”当作实现方式，必须一项一项明确定义

- [ ] **Step 4: 建立统一 resolver**

在 `desktop/src/lib/displayResolvers.ts` 中实现最小闭环函数：

```ts
export function formatDisplayName(name: DisplayName): string;
export function formatWorldNameByCode(code: string): string;
export function formatWorldName(world: WorldOption): string;
export function formatGeyserNameByKey(key: string): string;
export function formatGeyserNameByType(type: number, geysers: GeyserOption[]): string;
export function formatGeyserNameFromSummary(
  summary: GeyserSummary,
  geysers: GeyserOption[]
): string;
export function formatMixingSlotName(slot: MixingSlotMeta): string;
```

回退规则：

- world 缺失映射时退回 `code`
- geyser 缺失映射时退回 `summary.id` 或 `key`，最后才是 `type#N`
- mixing slot 缺失映射时退回 `catalog.name`，再退 `path`

- [ ] **Step 5: 让 coverage 脚本通过**

Run:

```powershell
node --experimental-strip-types desktop/scripts/verify-display-name-coverage.mjs
```

Expected:

- PASS
- 输出明确表明世界、喷口、mixing slot 映射覆盖齐全

## Chunk 2: 替换搜索参数页与结果页展示

### Task 2: 替换搜索参数页的世界、mixing、喷口展示

**Files:**
- Modify: `desktop/src/features/search/WorldSelector.tsx`
- Modify: `desktop/src/features/search/MixingSelector.tsx`
- Modify: `desktop/src/features/search/GeyserConstraintEditor.tsx`
- Modify: `desktop/src/features/search/DistanceRuleEditor.tsx`
- Modify: `desktop/src/features/search/CountRuleEditor.tsx`
- Reference: `desktop/src/lib/displayResolvers.ts`

- [ ] **Step 1: 先写一个最小验收清单**

清单内容：

- `WorldSelector` 不再显示 `id - code`
- `MixingSelector` 不再显示 `Slot N`
- 四类喷口下拉不再显示 `geyser.key`

将这份清单写在实现备注或开发记录中，作为本 task 的人工验收基准。

- [ ] **Step 2: 替换世界类型下拉**

改造 `WorldSelector.tsx`：

- 选项值仍使用 `world.id`
- 选项标签改为 `formatWorldName(world)`

- [ ] **Step 3: 替换 mixing slot 展示**

改造 `MixingSelector.tsx`：

- `buildDisplaySlots()` 改为使用 resolver 构建正式显示名
- 禁止再回退 `Slot ${slot}`
- 标题、说明、禁用提示都保留现有交互，只替换名称文本

- [ ] **Step 4: 替换喷口约束编辑器**

改造：

- `GeyserConstraintEditor.tsx`
- `DistanceRuleEditor.tsx`
- `CountRuleEditor.tsx`

要求：

- `<option value={item.key}>` 保持不变
- 文本改成 `formatGeyserNameByKey(item.key)`
- “当前世界不可生成”追加在正式名后面

- [ ] **Step 5: 运行构建验证**

Run:

```powershell
corepack yarn --cwd desktop build
```

Expected:

- PASS
- 无 TypeScript 错误

### Task 3: 替换结果列表展示

**Files:**
- Modify: `desktop/src/features/results/resultColumns.tsx`
- Reference: `desktop/src/state/searchStore.ts`
- Reference: `desktop/src/lib/displayResolvers.ts`

- [ ] **Step 1: 写一个最小失败检查**

把当前错误行为列清楚：

- `formatGeyserSummary()` 直接使用 `geyser.id ?? type#N`

作为本 task 的失败基线，不额外引入测试框架。

- [ ] **Step 2: 改造喷口概览 formatter**

要求：

- 使用 `formatGeyserNameFromSummary()`
- 优先展示正式喷口名
- 只保留现有截断策略，不改变列结构

- [ ] **Step 3: 重新构建**

Run:

```powershell
corepack yarn --cwd desktop build
```

Expected:

- PASS

## Chunk 3: 替换预览详情、分析结果与导出边界说明

### Task 4: 替换预览详情与分析结果展示

**Files:**
- Modify: `desktop/src/features/preview/PreviewDetails.tsx`
- Modify: `desktop/src/features/search/SearchAnalysisHints.tsx`
- Reference: `desktop/src/lib/displayResolvers.ts`
- Reference: `desktop/src/lib/contracts.ts`

- [ ] **Step 1: 改造预览详情中的喷口名称**

要求：

- 当前喷口显示改为正式名 + 坐标
- 喷口列表显示改为正式名 + 坐标
- 不改变 hover/selected 交互行为

- [ ] **Step 2: 改造分析结果中的 bottleneck 展示**

要求：

- `analysis.bottlenecks` 改为通过 geyser resolver 格式化后再 `join(", ")`
- 只替换文本，不改变分析逻辑和字段结构

- [ ] **Step 3: 运行构建验证**

Run:

```powershell
corepack yarn --cwd desktop build
```

Expected:

- PASS

### Task 5: 明确导出边界并同步文档

**Files:**
- Modify: `desktop/src/features/search/SearchPanel.tsx`
- Modify: `llmdoc/decisions/2026-04-09-tauri-desktop-refactor-plan.md`
- Reference: `docs/superpowers/specs/2026-04-13-desktop-display-name-mapping-design.md`

- [ ] **Step 1: 保持 `copyAsJson()` 为协议型导出**

要求：

- 不修改 JSON 的字段 key
- 不修改 JSON 的内部值
- 如需要，增加一条简短提示，明确该复制项是“协议 JSON”，不是展示型导出

- [ ] **Step 2: 同步 llmdoc**

在 `llmdoc/decisions/2026-04-09-tauri-desktop-refactor-plan.md` 中补充当前 desktop 状态：

- 已新增前端展示名解析层
- 世界、喷口、mixing slot 的正式名在前端统一解析
- 后端协议与旧前端维持不变

- [ ] **Step 3: 跑最终验证**

Run:

```powershell
node --experimental-strip-types desktop/scripts/verify-display-name-coverage.mjs
corepack yarn --cwd desktop build
```

Expected:

- 覆盖校验 PASS
- desktop 构建 PASS

- [ ] **Step 4: 手工验收清单**

人工确认以下路径：

- 世界类型下拉显示 `中文（English）`
- mixing slot 标题和说明显示正式名称
- required / forbidden / distance / count 下拉显示正式喷口名
- 结果表喷口概览不再显示 `key` 或 `type#N`
- 预览详情与喷口列表不再显示内部 id 或 `type#N`
- `复制当前配置 JSON` 仍然是协议 JSON

## 执行说明

- 不要自动提交；提交由 `wgh` 手动执行
- 实现过程中保持“后端协议不变、旧前端不变、日志不变”
- 若发现某个 mixing slot 在当前 catalog 中缺少足够语义，优先在前端 overlay 中补齐，不要反向修改后端结构

