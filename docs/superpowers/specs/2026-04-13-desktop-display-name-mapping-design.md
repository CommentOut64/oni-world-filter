# Desktop 正式名称映射设计

## 1. 背景

当前 `desktop` 前端里，世界类型、喷口/泉、mixing slot 的展示来源不统一：

- 世界类型下拉仍显示 `id - code`，例如 `13 - V-SNDST-C-`
- mixing slot 在 catalog 缺失或名称不规范时会退回 `Slot N`
- 喷口约束编辑器、结果列表、预览详情、分析提示中仍大量显示 `geyser.key`、`summary.id` 或 `type#N`
- `复制当前配置 JSON` 属于协议型导出，当前输出使用内部字段名和内部值

这会导致桌面端展示风格不一致，也无法稳定表达“正式中文/英文名称”。

本设计的目标是：在 **不修改后端字段命名、不影响旧前端、不改变日志输出** 的前提下，为 `desktop` 建立一套前端专用的正式命名层，并统一替换所有用户可见名称。

## 2. 目标与非目标

### 2.1 目标

- 为世界类型、喷口/泉、mixing slot 建立完整的正式中英文映射
- `desktop` 所有用户可见展示统一输出为 `中文（English）`
- 对“前端展示”和“人类可读导出”统一走同一套名称解析逻辑
- 保持当前后端协议、请求字段和值不变

### 2.2 非目标

- 不修改 `sidecar` / Tauri / C++ 返回的数据结构字段
- 不修改旧浏览器前端的显示逻辑
- 不修改 native/sidecar 日志文案
- 不把 `复制当前配置 JSON` 这类协议型导出改成中文字段或中文值

## 3. 约束

根据当前需求，必须满足以下边界：

- 前端显示格式统一为 `中文（English）`
- 只改 `desktop` 新前端与导出展示
- 旧前端和日志维持原样
- 后端继续沿用当前 `worldType` / `code` / `key` / `id` / `path` / `mixing` 等字段命名

## 4. 正式名称来源

### 4.1 世界类型

正式名称来源按以下顺序校对并固化到前端：

1. `llmdoc/reference/filter-config.md` 的 `worldType` 对照表
2. `src/entry_cli.cpp` 中 `PrintWorldList()` 使用的世界列表
3. 旧前端现有世界显示名称

前端命名主键使用当前稳定字段 `world.code`，例如：

- `SNDST-A-`
- `V-SNDST-C-`
- `M-RAD-C-`

原因：

- `worldType` 是数值索引，只适合作为运行参数，不适合作为命名字典主键
- `world.code` 已稳定参与坐标码生成与目录展示，适合作为前端映射索引

### 4.2 喷口/泉

正式中文名来源以 `src/entry_cli.cpp` 中 `g_geyserNames[]` 为主锚点。

喷口主键继续沿用现有 `geyser.key` / `summary.id`，例如：

- `steam`
- `hot_water`
- `oil_reservoir`

英文正式名由前端静态字典补齐，并与中文名一一对应。  
最终展示形式统一为：

- `清水泉（Cool Steam Vent）` 这种形式并不适用于所有项，因此必须按每个 `key` 单独定义正式中英文，而不是用规则推导

### 4.3 Mixing Slot

mixing slot 的正式名称来源分两层：

1. `catalog.mixingSlots` 的 `slot / path / type / name / description`
2. 前端 overlay 对不规范、缺失或技术化命名进行纠偏

已知参考来源：

- `llmdoc/reference/filter-config.md` 的 11 个 mixing 位说明
- 当前 catalog 中的 `mixingSlots`

前端命名主键优先使用 `mixingSlot.path`，必要时辅以 `slot` 编号校验。

原因：

- `slot` 数字本身没有业务语义
- `path` 更接近当前参数表与 catalog 的真实来源
- `slot + path` 组合可防止未来 catalog 调整时出现误映射

## 5. 前端命名层设计

### 5.1 新增模块

建议新增以下前端模块：

- `desktop/src/lib/displayNames.ts`
  - 存放世界、喷口、mixing slot 的正式中英文静态注册表
- `desktop/src/lib/displayResolvers.ts`
  - 提供统一的解析与格式化函数

### 5.2 数据结构

建议使用如下展示元数据结构：

```ts
export interface DisplayName {
  zh: string;
  en: string;
}

export interface WorldDisplayName extends DisplayName {}
export interface GeyserDisplayName extends DisplayName {}
export interface MixingSlotDisplayName extends DisplayName {
  shortZh?: string;
  shortEn?: string;
}
```

### 5.3 注册表结构

```ts
export const WORLD_DISPLAY_NAMES: Record<string, WorldDisplayName>;
export const GEYSER_DISPLAY_NAMES: Record<string, GeyserDisplayName>;
export const MIXING_SLOT_DISPLAY_NAMES: Record<string, MixingSlotDisplayName>;
```

主键约定：

- 世界：`world.code`
- 喷口：`geyser.key`
- mixing slot：`mixingSlot.path`

### 5.4 统一 formatter

建议统一提供以下函数：

```ts
export function formatDisplayName(name: DisplayName): string;
export function formatWorldName(world: WorldOption): string;
export function formatWorldNameByCode(code: string): string;
export function formatGeyserNameByKey(key: string): string;
export function formatGeyserNameByType(type: number, geysers: GeyserOption[]): string;
export function formatGeyserNameFromSummary(
  summary: GeyserSummary,
  geysers: GeyserOption[]
): string;
export function formatMixingSlotName(slot: MixingSlotMeta): string;
```

统一输出规则：

```ts
`${zh}（${en}）`
```

### 5.5 回退规则

为了避免前端再次出现空白或 `Slot N` 之类退化展示，回退策略必须统一：

- 世界名称缺失时：显示 `world.code`
- 喷口名称缺失时：优先 `summary.id`，再退回 `geyser.key`，最后才退回 `type#N`
- mixing slot 名称缺失时：优先 `catalog.name`，若仍缺失则显示 `slot.path`，禁止直接显示 `Slot N`

说明：

- 回退值只用于兜底，不代表正式展示达标
- 实现时需要配套一份覆盖性校验，确保当前 catalog 不会走到退化分支

## 6. 替换范围

### 6.1 搜索参数页

需要替换的组件：

- `desktop/src/features/search/WorldSelector.tsx`
- `desktop/src/features/search/MixingSelector.tsx`
- `desktop/src/features/search/GeyserConstraintEditor.tsx`
- `desktop/src/features/search/DistanceRuleEditor.tsx`
- `desktop/src/features/search/CountRuleEditor.tsx`

具体要求：

- 世界类型下拉显示 `中文（English）`
- mixing slot 的标题与说明显示正式名
- 四类喷口选择器选项显示正式喷口名
- “当前世界不可生成”之类状态提示仍保留，但附加在正式名称之后

### 6.2 结果列表

需要替换的组件：

- `desktop/src/features/results/resultColumns.tsx`

当前问题：

- `formatGeyserSummary()` 直接显示 `summary.id` 或 `type#N`
- trait 仍显示 `trait#N`

本期要求：

- 喷口概览列改为正式喷口名
- trait 是否纳入本期取决于映射范围；本需求当前明确覆盖“世界类型、slot、泉名称”，trait 不作为本期必做项

### 6.3 地图预览详情

需要替换的组件：

- `desktop/src/features/preview/PreviewDetails.tsx`

当前问题：

- 当前喷口显示 `summary.id ?? type#N`
- 喷口列表显示内部 id 或 `type#N`

本期要求：

- 预览详情中的当前喷口与喷口列表统一显示正式喷口名
- 位置坐标继续保留

### 6.4 分析提示与分析结果

需要替换的组件：

- `desktop/src/features/search/SearchAnalysisHints.tsx`
- 以及分析结果中所有直接展示 `geyserId` 的辅助显示函数

当前问题：

- `analysis.bottlenecks` 当前直接 `join(", ")`
- world profile / source summary / possible/impossible geyser types 后续扩展展示时仍会暴露内部 key

本期要求：

- `bottlenecks` 显示正式喷口名
- 任何 world profile / source summary / geyser group 相关的用户可见文本，都必须先走名称解析层

### 6.5 导出

当前已知导出入口：

- `desktop/src/features/search/SearchPanel.tsx` 的 `copyAsJson()`
- `desktop/src/features/preview/PreviewCanvas.tsx` 的 PNG 导出文件名

本期导出规则分两类：

#### 协议型导出

例如：

- `复制当前配置 JSON`

要求：

- 保持字段 key 不变
- 保持内部值不变
- 不把 `geyser` 字段值从 `hot_water` 改成中文

原因：

- 该 JSON 仍然承担“可回放请求 / 可供后端消费”的职责

#### 展示型导出

例如后续的人类可读摘要、文本导出、复制说明：

- 应改为正式名称
- 如需同时保留内部 key，应作为附属信息而非主展示名

#### PNG 导出

当前文件名 `oni-preview-${seed}.png` 不涉及正式名称映射，本期不强制修改。

## 7. 与后端的接口边界

以下内容明确不变：

- `desktop/src/lib/contracts.ts` 的现有协议字段
- `src-tauri/src/sidecar.rs` 的 `WorldOption` / `GeyserOption` / `MixingSlotMeta`
- `src/Batch/SidecarProtocol.cpp` 里的目录与事件序列化结构
- `src/entry_cli.cpp` 的列表输出与日志输出

前端只是新增一层展示转换，不改变任何请求或响应字段的含义。

## 8. 实施步骤建议

### 8.1 建立正式命名字典

- 新增 `displayNames.ts`
- 收录全部 `world.code`
- 收录全部 `geyser.key`
- 收录全部当前支持的 mixing slot

### 8.2 建立统一解析器

- 新增 `displayResolvers.ts`
- 统一处理 `world.code -> 正式名称`
- 统一处理 `geyser.key / summary.id / type -> 正式名称`
- 统一处理 `MixingSlotMeta -> 正式名称`

### 8.3 替换 UI 展示

- 搜索参数页
- 结果列表
- 地图预览详情
- 分析提示

### 8.4 替换展示型导出

- 保持协议 JSON 不变
- 若存在可读摘要导出，统一改走正式名称格式

### 8.5 增加覆盖性校验

至少增加一份最小校验，验证：

- 当前全部 `world.code` 都能映射到正式名
- fallback catalog 中全部 `geyser.key` 都能映射到正式名
- 当前 mixing slot 列表不会退回 `Slot N`

## 9. 需要修改的主要文件

预计会涉及：

- 新增：`desktop/src/lib/displayNames.ts`
- 新增：`desktop/src/lib/displayResolvers.ts`
- 修改：`desktop/src/features/search/WorldSelector.tsx`
- 修改：`desktop/src/features/search/MixingSelector.tsx`
- 修改：`desktop/src/features/search/GeyserConstraintEditor.tsx`
- 修改：`desktop/src/features/search/DistanceRuleEditor.tsx`
- 修改：`desktop/src/features/search/CountRuleEditor.tsx`
- 修改：`desktop/src/features/results/resultColumns.tsx`
- 修改：`desktop/src/features/preview/PreviewDetails.tsx`
- 修改：`desktop/src/features/search/SearchAnalysisHints.tsx`
- 可能修改：`desktop/src/features/search/SearchPanel.tsx`
- 可能修改：`desktop/src/lib/searchCatalog.ts`

如分析结果落点进一步拆分，也可能增加少量辅助文件，但不应扩散到后端。

## 10. 验证策略

### 10.1 静态覆盖验证

建议添加最小脚本或测试，验证：

- 全部世界 code 可解析
- 全部 geyser key 可解析
- mixing slot 可解析

### 10.2 构建验证

执行：

```powershell
corepack yarn --cwd desktop build
```

### 10.3 手工关键路径验证

- 世界类型下拉显示 `中文（English）`
- mixing slot 标题和说明显示正式名称
- required / forbidden / distance / count 四类喷口下拉显示正式名称
- 结果表喷口概览不再显示 `key` 或 `type#N`
- 预览详情不再显示 `summary.id` 或 `type#N`
- `复制当前配置 JSON` 仍保持原协议字段和值

## 11. 风险与处理

### 11.1 风险：名称来源不完整

处理：

- 先以当前 world / geyser / mixing 目录为闭环
- 用覆盖性校验确保没有遗漏

### 11.2 风险：把协议型导出误改成人类展示

处理：

- 明确区分“协议型导出”和“展示型导出”
- `copyAsJson()` 保持原样，只允许在按钮文案或附加说明层做展示增强

### 11.3 风险：不同组件各自拼名称导致再次分裂

处理：

- 禁止组件内手写 `item.key` / `world.code` / `type#N` 作为最终显示
- 所有用户可见名称都必须经由统一 resolver

## 12. 结论

本设计采用“**前端单一命名字典 + 统一格式化入口**”方案：

- 后端协议不变
- 旧前端与日志不变
- `desktop` 和展示型导出统一切到正式中英文名
- 协议型 JSON 导出继续保留内部字段和值

这是当前需求边界下最小、最稳、可验证的实现方案。
