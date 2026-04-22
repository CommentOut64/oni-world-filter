# Search World Parameter UI Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把桌面端搜索参数页的世界参数从“后端字段直出”改成用户可理解、可操作的世界分类与 mixing 分组表单，同时保持提交给后端的仍然只有 `worldType` 和 `mixing`。

**Architecture:** 本次只做前端组织层重构，不改 sidecar 协议。`worldType` 继续使用现有 catalog 的 world id，但在前端派生“世界分类 -> 具体世界”两级选择；`mixing` 继续存储为 base-5 编码整数，但在前端改为“DLC 包复选框 + 槽位复选框 + 可能/确保”交互，并保留只读编码值作为高级调试信息。

**Tech Stack:** React 19、TypeScript、react-hook-form、Zustand、Vite、现有 Tauri search catalog/analyze API

---

## File Map

### Existing Files To Modify

- `desktop/src/features/search/WorldSelector.tsx`
  - 现状：单个 `worldType` 下拉框。
  - 目标：改为“世界分类 + 具体世界列表”两级 UI。
- `desktop/src/features/search/MixingSelector.tsx`
  - 现状：直接展示 `mixing` 数字和 11 个槽位等级下拉。
  - 目标：改为 DLC 包卡片、包复选框、槽位复选框、可能/确保选择器、高级信息折叠区。
- `desktop/src/features/search/SearchPanel.tsx`
  - 现状：只负责组合现有 `WorldSelector` 和 `MixingSelector`。
  - 目标：接入新的说明文案、必要 props、世界参数区域结构调整。
- `desktop/src/app/app.css`
  - 现状：已有基础 field、grid、mixing-slot-grid 样式。
  - 目标：补世界分类列表、世界卡片、mixing 分组卡片、禁用态、折叠区样式。

### New Files To Create

- `desktop/src/features/search/worldParameterUi.ts`
  - 职责：集中放世界分类派生、mixing 分组派生、UI 状态与 level 编码转换，避免把规则散落在组件里。

### Optional Files To Modify Only If Needed

- `desktop/src/features/search/SearchAnalysisHints.tsx`
  - 仅在需要把“当前世界禁用的 mixing 槽位说明”补进分析区时修改。
- `desktop/src/lib/displayResolvers.ts`
  - 仅在世界项需要新的格式化辅助函数时修改；能不动则不动。

## Constraints

- 不改 `src/`、`src-tauri/`、sidecar 协议和 catalog 结构。
- 不把 `traitCatalog` 误导成可搜索条件。
- 不把 `Mixing 编码值` 继续暴露为主输入。
- 不新增前端测试框架；当前仓库无 `vitest/jest`，本次验证以 `desktop` 构建和手工关键路径为准。
- 不自动提交；提交由用户手动执行。

## UI Rules To Preserve

- `worldType` 最终仍然是现有 catalog world id。
- `mixing` 最终仍然是现有 base-5 编码整数。
- `0 = 禁用`
- `1 = 可能`
- `2 = 确保`
- `3/4` 不在主 UI 单独暴露，统一按“可能启用”回写 `1`。
- 当前世界禁用的 slot 必须显示但禁用，不能隐藏。

## World Classification Rules

- `原版星体`：`world.code` 包含 `-A-`
- `经典星群`：`world.code` 以 `V-` 开头且包含 `-C-`
- `卫星星群`：其余包含 `-C-` 的 world code

## Mixing Grouping Rules

- 以 DLC 包 slot 为一级分组：
  - `DLC2_ID`
  - `DLC3_ID`
  - `DLC4_ID`
- 同 DLC 目录下的 `worldMixing/` 与 `subworldMixing/` 归入对应包卡片
- `DLC3_ID` 当前无子槽位，仍保留单独卡片

## Chunk 1: Build World Parameter UI Helpers

### Task 1: Create focused helper module

**Files:**
- Create: `desktop/src/features/search/worldParameterUi.ts`

- [ ] **Step 1: Create world category types and constants**

定义最小类型：

```ts
export type WorldCategory = "baseAsteroid" | "classicCluster" | "moonletCluster";

export interface WorldCategoryOption {
  id: WorldCategory;
  label: string;
  description: string;
}
```

- [ ] **Step 2: Implement world classification helpers**

写纯函数：

```ts
export function classifyWorld(code: string): WorldCategory
export function groupWorldsByCategory(worlds: readonly WorldOption[]): Record<WorldCategory, WorldOption[]>
export function getCategoryForWorld(worlds: readonly WorldOption[], worldType: number): WorldCategory
```

规则固定为本计划 `World Classification Rules`，不要写成基于索引区间的魔法数字。

- [ ] **Step 3: Implement mixing group helpers**

写纯函数：

```ts
export interface MixingPackageGroup {
  packageSlot: MixingSlotMeta;
  children: MixingSlotMeta[];
}

export function groupMixingSlots(slots: readonly MixingSlotMeta[]): MixingPackageGroup[]
```

按 path 前缀把 slot 归到 DLC2/DLC3/DLC4 三组，不要在组件里手写过滤逻辑。

- [ ] **Step 4: Implement UI state <-> level mapping helpers**

写纯函数：

```ts
export type MixingUiMode = "off" | "normal" | "guaranteed";

export function levelToUiMode(level: number): MixingUiMode
export function uiModeToLevel(mode: MixingUiMode): number
export function isSlotEnabled(level: number): boolean
```

要求：

- `0 -> off`
- `1/3/4 -> normal`
- `2 -> guaranteed`

- [ ] **Step 5: Add package-level derivation helpers**

写纯函数：

```ts
export function getPackageMode(levels: readonly number[], group: MixingPackageGroup): MixingUiMode
export function applyPackageMode(levels: readonly number[], group: MixingPackageGroup, mode: MixingUiMode): number[]
export function applyChildMode(levels: readonly number[], slot: number, mode: MixingUiMode): number[]
```

规则：

- 包关闭时，包自身和子槽位全部回写 `0`
- 包开启可能时，包自身默认 `1`，已勾选的子槽位保持现状
- 包开启确保时，包自身写 `2`

- [ ] **Step 6: Sanity-check helper file with TypeScript build**

Run: `yarn --cwd desktop build`

Expected:

- TypeScript 通过
- 若构建失败，先修 helper 类型问题，不进入组件改造

## Chunk 2: Replace World Selector With Two-Level UI

### Task 2: Rebuild `WorldSelector` around categories

**Files:**
- Modify: `desktop/src/features/search/WorldSelector.tsx`
- Modify: `desktop/src/features/search/SearchPanel.tsx`
- Create: `desktop/src/features/search/worldParameterUi.ts`

- [ ] **Step 1: Read current selector responsibilities**

确认当前文件只负责 `worldType` 表单字段，没有额外副作用。不要把 mixing 或分析逻辑塞进 `WorldSelector`。

- [ ] **Step 2: Replace single select with category state derived from current worldType**

组件内部引入：

```ts
const selectedCategory = getCategoryForWorld(worlds, watch("worldType"));
```

再提供分类切换控件。分类切换只切换当前候选列表，不直接把 `worldType` 重置成硬编码值。

- [ ] **Step 3: Implement concrete world list**

具体世界列表要求：

- 每项显示中文名、英文名、前缀码
- 只能单选
- 当前选中项有明确选中态
- 空分类时显示 hint，而不是空白区域

- [ ] **Step 4: Preserve form integration**

点击具体世界项时仍然调用 `setValue("worldType", world.id, ...)`。

不要改 `searchSchema.ts` 和 `SearchDraft` 数据结构。

- [ ] **Step 5: Update section copy in `SearchPanel.tsx`**

把“世界类型与 mixing 配置”改成更明确的说明，例如：

- 分类决定世界家族
- 具体世界决定实际 worldType
- mixing 仅展示当前世界相关的可混搭内容

- [ ] **Step 6: Run build**

Run: `yarn --cwd desktop build`

Expected:

- 通过构建
- 没有 `WorldSelector` 相关类型错误

## Chunk 3: Replace Mixing Selector With Package Cards

### Task 3: Rebuild `MixingSelector` around package cards and checkbox-first UX

**Files:**
- Modify: `desktop/src/features/search/MixingSelector.tsx`
- Create: `desktop/src/features/search/worldParameterUi.ts`

- [ ] **Step 1: Keep existing encoding helpers**

继续复用：

- `decodeMixingToLevels`
- `encodeMixingFromLevels`
- `MIXING_SLOT_COUNT`

不要复制一套新的 base-5 编解码逻辑。

- [ ] **Step 2: Remove main-surface numeric editing**

把 `Mixing 编码值` 从主输入区移出，只保留到折叠区只读展示：

- 主区域不允许手输数字
- 折叠区可显示当前整数值，供调试和复制 JSON 使用

- [ ] **Step 3: Render package cards**

每张卡片包含：

- 包名称
- 包说明
- 包级复选框
- 包级 `可能/确保` 选择器，仅在勾选后显示
- 子槽位列表

`DLC3_ID` 无子项时也按同一结构渲染，只是列表为空。

- [ ] **Step 4: Render child slots as checkbox-first rows**

每个子槽位行包含：

- 复选框
- 名称
- 简短说明
- `可能/确保` 选择器，仅在已勾选且未禁用时显示

禁用态要求：

- 复选框禁用
- 文案追加“当前世界不可用”
- 若分析结果强制该 slot 为 0，UI 必须与表单值一致

- [ ] **Step 5: Normalize old levels into new UI**

如果当前 level 是 `3/4`：

- UI 显示为已勾选
- 模式显示为 `可能`
- 用户一旦改动，统一回写到 `1`

- [ ] **Step 6: Keep disabled slot auto-clear compatibility**

`SearchPanel.tsx` 里已有 disabled slot 自动清零逻辑。新 `MixingSelector` 不要重复实现一次自动清零，只负责展示和调用 `setValue("mixing", ...)`。

- [ ] **Step 7: Run build**

Run: `yarn --cwd desktop build`

Expected:

- 构建通过
- 没有 `MixingSelector` 类型错误

## Chunk 4: Polish Copy, Layout, And Advanced Info

### Task 4: Update world parameter section copy and layout

**Files:**
- Modify: `desktop/src/features/search/SearchPanel.tsx`
- Modify: `desktop/src/app/app.css`
- Optional Modify: `desktop/src/features/search/SearchAnalysisHints.tsx`

- [ ] **Step 1: Split world parameter section visually**

布局目标：

- 左上：世界分类
- 左下：具体世界
- 右侧或下方：世界混搭包卡片

在窄屏下自动回落到单列。

- [ ] **Step 2: Add advanced info disclosure**

新增折叠区，用于只读展示：

- 当前 `mixing` 编码值
- 当前世界禁用 slot 数量
- 可选：禁用 slot 名称列表

不要把调试信息放回主表单首屏。

- [ ] **Step 3: Add CSS for new components**

新增样式类建议：

- `.world-category-grid`
- `.world-category-card`
- `.world-option-list`
- `.world-option-card`
- `.mixing-package-list`
- `.mixing-package-card`
- `.mixing-child-row`
- `.mixing-disabled-note`
- `.advanced-debug-panel`

要求：

- 沿用现有深色面板风格
- 选中态、禁用态、确保态有明显区分
- 不改变搜索页整体双栏布局

- [ ] **Step 4: Optional analysis hint copy adjustment**

仅在确实有必要时，把分析区文案补成更贴合新 UI 的说法，例如：

- “当前世界禁用了以下混搭槽位”
- “以下喷口在当前世界结构上不可能出现”

若现有提示已足够，则不改。

- [ ] **Step 5: Run build**

Run: `yarn --cwd desktop build`

Expected:

- 构建通过
- 无 CSS/TSX 引入错误

## Chunk 5: Manual Verification Gate

### Task 5: Verify critical user paths

**Files:**
- No code changes required

- [ ] **Step 1: Start desktop dev server**

Run: `yarn --cwd desktop dev`

Expected:

- Vite 在 `127.0.0.1:1420` 启动成功

- [ ] **Step 2: Verify world classification flow**

手工检查：

- 默认世界能正确落到某个分类
- 切换分类后，只更新候选世界列表
- 点击具体世界后，表单真实 worldType 跟着变化

- [ ] **Step 3: Verify mixing package flow**

手工检查：

- 三个 DLC 包能正确分组
- 包复选框勾选后出现 `可能/确保`
- 子槽位勾选后出现 `可能/确保`
- 取消勾选后对应 level 回到 `0`

- [ ] **Step 4: Verify disabled slot behavior**

手工检查：

- 切换到会禁用部分 mixing 的世界
- 被禁用的 slot 显示且默认不可开启
- 若旧值里仍保留禁用 slot，用户仍可手动关闭，不会被自动清零
- 搜索提交前会由 `analyze_search_request` 明确阻止，并给出前端正式名称提示

- [ ] **Step 5: Verify backward-compat normalization**

通过加载已有 draft 或手动构造值检查：

- 若某 slot 初始值为 `3/4`
- UI 仍表现为“已启用 + 可能”
- 用户保存后不再写回 `3/4`

- [ ] **Step 6: Verify request integrity**

手工检查：

- 复制 JSON 后仍只有 `worldType` 和 `mixing`
- 没有新增协议字段
- 搜索提交前 `analyze_search_request` 仍正常工作

- [ ] **Step 7: Final build**

Run: `yarn --cwd desktop build`

Expected:

- 最终构建通过，作为交付前门禁

## Acceptance Criteria

- 搜索参数页首屏不再暴露 `worldType`/`mixing` 的裸实现细节。
- 用户能先按世界分类理解，再选择具体世界。
- 用户能按 DLC 包理解 mixing，而不是面对 11 个匿名 slot。
- mixing 主交互为复选框；勾选后可选 `可能/确保`。
- 当前世界禁用的 slot 会显示并禁用，不会静默隐藏。
- 当前世界禁用的 slot 不再被前端静默改写；用户可见、可主动关闭、提交前有明确阻止提示。
- 提交给后端的协议仍然只有现有 `worldType` 和 `mixing`。
- `desktop` 构建通过，关键手工路径验证通过。

## Notes For Implementation

- 若 `WorldSelector.tsx` 体积明显增长，优先把渲染辅助拆到 `worldParameterUi.ts`，不要在组件里堆大量条件分支。
- 若 `MixingSelector.tsx` 出现“包状态”和“子状态”相互覆盖的问题，先以“所有写入都回到 levels 数组再统一 encode”为唯一真相源，不要维护并行本地状态。
- 若后续发现前端分类规则必须从后端返回，再单开后端协议计划；本次不扩散到 sidecar。
- 本计划不包含自动提交步骤；由用户手动提交。
