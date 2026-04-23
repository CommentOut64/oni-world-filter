# Desktop Coordinate Direct Preview Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 `desktop/` 搜索页增加“仅支持标准坐标码”的顶部直达入口，提交后不触发批量搜索，直接跳到现有结果页并显示与该坐标完全对应的地图预览。

**Architecture:** 搜索页页头新增独立的坐标直达控件，但 DOM 上必须脱离批量搜索 `<form>`；新增专用 `preview_coord` 命令，让后端基于原始坐标做权威解析、canonical 校验和地图生成；前端收到返回值后构造单条 `SearchMatchSummary`，预热 `previewStore`，然后复用现有结果页与预览面板。现有批量搜索流式链路和 `load_preview(worldType, seed, mixing)` 路径保持不变。

**Tech Stack:** React 19、TypeScript、antd v6、Zustand、node:test、Tauri 2、Rust、C++ sidecar、PowerShell

---

## 先决说明

- 本计划默认遵循仓库规则：`commit` 由 `wgh` 手动执行，因此不包含自动提交步骤。
- 本计划中的“直接可执行”指：文件边界、接口草图、测试命令、验证门槛已经写死，可直接按步骤实施。
- 设计定稿见 [`docs/superpowers/specs/2026-04-23-desktop-coordinate-direct-preview-design.md`](f:/oni_world_app-master/docs/superpowers/specs/2026-04-23-desktop-coordinate-direct-preview-design.md)。

## 文件边界

- Create: `desktop/src/features/search/CoordQuickSearch.tsx`
- Create: `desktop/src/features/search/coordPreviewFlow.ts`
- Create: `desktop/src/lib/searchMatchSummary.ts`
- Create: `desktop/tests/coordQuickSearchUi.test.ts`
- Create: `desktop/tests/coordPreviewFlow.test.ts`
- Create: `desktop/tests/coordPreviewState.test.ts`
- Modify: `desktop/src/features/search/SearchPanel.tsx`
- Modify: `desktop/src/features/search/SearchActions.tsx`
- Modify: `desktop/src/lib/contracts.ts`
- Modify: `desktop/src/lib/tauri.ts`
- Modify: `desktop/src/state/searchStore.ts`
- Modify: `desktop/src/state/previewStore.ts`
- Modify: `desktop/src/state/previewStoreState.ts`
- Modify: `desktop/src/app/app.css`
- Modify: `src-tauri/src/commands.rs`
- Modify: `src-tauri/src/main.rs`
- Modify: `src-tauri/src/control_sidecar.rs`
- Modify: `src-tauri/src/sidecar.rs`
- Modify: `src/Batch/SidecarProtocol.hpp`
- Modify: `src/Batch/SidecarProtocol.cpp`
- Modify: `src/entry_sidecar.cpp`
- Modify: `llmdoc/overview/project.md`

## Chunk 1: 前端结果构造与状态注入

### Task 1: 先写纯前端状态与流程的失败测试

**Files:**
- Create: `desktop/tests/coordPreviewFlow.test.ts`
- Create: `desktop/tests/coordPreviewState.test.ts`
- Test: `desktop/tests/coordPreviewFlow.test.ts`
- Test: `desktop/tests/coordPreviewState.test.ts`

- [ ] Step 1: 在 `coordPreviewFlow.test.ts` 为成功路径写失败测试
  期望行为：
  - 只调用 `loadPreviewByCoord`
  - 成功后调用“写入单条结果”“预热 preview”“切到结果页”
  - 失败后只设置错误，不切页

- [ ] Step 2: 在 `coordPreviewState.test.ts` 为 `searchStore` / `previewStore` 新动作写失败测试
  期望行为：
  - `openDirectCoordResult()` 会清空批量搜索运行态
  - `primeResolvedPreview()` 会把 preview 放入 cache 并设为 active
  - 不覆盖现有 `draft`

- [ ] Step 3: 运行失败测试，确认当前实现还不具备这些能力

```powershell
node --test desktop/tests/coordPreviewFlow.test.ts
node --test desktop/tests/coordPreviewState.test.ts
```

### Task 2: 实现共享结果构造 helper 与 store 动作

**Files:**
- Create: `desktop/src/lib/searchMatchSummary.ts`
- Modify: `desktop/src/state/searchStore.ts`
- Modify: `desktop/src/state/previewStore.ts`
- Modify: `desktop/src/state/previewStoreState.ts`

- [ ] Step 1: 在 `desktop/src/lib/searchMatchSummary.ts` 提供共享 helper

```ts
export function buildSearchMatchSummaryFromPreview(args: {
  coord: string;
  worldType: number;
  seed: number;
  mixing: number;
  summary: PreviewPayload["summary"];
}): SearchMatchSummary
```

要求：
- `traits / start / worldSize / geysers` 直接来自 `preview.summary`
- `nearestDistance` 与现有搜索命中结果的计算口径一致
- 该 helper 同时适合后续复用到 `searchStore.appendMatch`

- [ ] Step 2: 在 `searchStore.ts` 增加专用动作

```ts
openDirectCoordResult: (match: SearchMatchSummary) => void;
```

状态约束：
- `results = [match]`
- `selectedSeed = match.seed`
- `activeWorldType = match.worldType`
- `activeMixing = match.mixing`
- `isSearching = false`
- `isCancelling = false`
- `activeJobId = null`
- `lastSubmittedRequest = null`
- `lastError = null`
- `draft` 保持不变

- [ ] Step 3: 在 `previewStoreState.ts` 增加 cache 预热 helper，并在 `previewStore.ts` 暴露动作

```ts
primeResolvedPreview: (match: SearchMatchSummary, preview: PreviewPayload) => void;
```

约束：
- 使用与现有 `loadByMatch()` 相同的 key 规则
- 预热后 `PreviewPane` 再次命中同一条记录时，只从 cache 切换，不重复请求

- [ ] Step 4: 运行测试，确认状态层转绿

```powershell
node --test desktop/tests/coordPreviewFlow.test.ts
node --test desktop/tests/coordPreviewState.test.ts
```

## Chunk 2: 顶部坐标入口与批量搜索隔离

### Task 3: 先写页头控件的失败测试

**Files:**
- Create: `desktop/tests/coordQuickSearchUi.test.ts`
- Test: `desktop/tests/coordQuickSearchUi.test.ts`

- [ ] Step 1: 为新控件写失败测试，验证它是独立 `button` 而不是 `submit`

```ts
assert.match(markup, /type="button"/);
assert.match(markup, /标准坐标码/);
assert.match(markup, /搜索/);
```

- [ ] Step 2: 为禁用态写失败测试
  期望行为：
  - 批量搜索运行中时，坐标输入框和按钮禁用
  - 坐标请求运行中时，批量搜索按钮也禁用

- [ ] Step 3: 运行测试，确认先失败

```powershell
node --test desktop/tests/coordQuickSearchUi.test.ts
```

### Task 4: 实现顶部控件与页头布局

**Files:**
- Create: `desktop/src/features/search/CoordQuickSearch.tsx`
- Modify: `desktop/src/features/search/SearchPanel.tsx`
- Modify: `desktop/src/features/search/SearchActions.tsx`
- Modify: `desktop/src/app/app.css`

- [ ] Step 1: 创建 `CoordQuickSearch.tsx` 纯展示组件

```tsx
interface CoordQuickSearchProps {
  value: string;
  loading: boolean;
  disabled: boolean;
  onChange: (value: string) => void;
  onSubmit: () => void;
}
```

约束：
- 搜索按钮必须 `htmlType="button"`
- 输入框只提供标准坐标提示，不做模糊说明

- [ ] Step 2: 在 `SearchPanel.tsx` 中把页头改成“三段式”
  具体要求：
  - 标题、坐标直达、主题切换在同一行
  - 坐标控件 DOM 上放在批量 `<form>` 之外
  - 本地状态使用 `coordInput` / `isCoordSubmitting`
  - `isCoordSubmitting` 时禁止批量 `submit`

- [ ] Step 3: 给 `SearchActions.tsx` 加一个“外部忙碌态”入参，避免坐标请求时还能发起批量搜索

```tsx
interface SearchActionsProps {
  isSearching: boolean;
  isBusy?: boolean;
  ...
}
```

- [ ] Step 4: 在 `app.css` 增加页头布局类，保持现有页面骨架不变
  必须新增的样式点：
  - 头部中间区可伸缩
  - 坐标输入框最小宽度受控
  - 小屏下允许换行，但不改变现有两页骨架

- [ ] Step 5: 运行 UI 测试，确认转绿

```powershell
node --test desktop/tests/coordQuickSearchUi.test.ts
```

## Chunk 3: 后端权威解析与 preview_coord 命令

### Task 5: 先写 Rust 侧失败测试

**Files:**
- Modify: `src-tauri/src/sidecar.rs`
- Test: `src-tauri/src/sidecar.rs`

- [ ] Step 1: 先为新请求类型写失败测试

```rust
#[test]
fn build_preview_coord_command_should_emit_coord_payload() { ... }

#[test]
fn validate_preview_coord_request_should_reject_blank_coord() { ... }
```

- [ ] Step 2: 如已具备 sidecar 二进制，再补一条 ignored smoke 测试名称占位

```rust
#[test]
#[ignore = "需要先构建 oni-sidecar.exe"]
fn sidecar_preview_coord_smoke() { ... }
```

- [ ] Step 3: 运行测试并确认先失败

```powershell
cargo test --manifest-path src-tauri/Cargo.toml preview_coord
```

### Task 6: 实现 Rust/Tauri 命令链路

**Files:**
- Modify: `src-tauri/src/commands.rs`
- Modify: `src-tauri/src/main.rs`
- Modify: `src-tauri/src/control_sidecar.rs`
- Modify: `src-tauri/src/sidecar.rs`

- [ ] Step 1: 在 `sidecar.rs` 增加请求与返回协议

```rust
#[derive(Debug, Clone, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct CoordPreviewRequestPayload {
    pub job_id: String,
    pub coord: String,
}
```

返回事件要求：
- 仍使用 `event = "preview"`
- 但新增根级 `coord` 字段
- `worldType / seed / mixing / preview` 由后端返回 canonical 值

- [ ] Step 2: 新增以下函数

```rust
pub fn load_preview_by_coord(...)
pub(crate) fn validate_preview_coord_request(...)
pub(crate) fn build_preview_coord_command(...)
```

- [ ] Step 3: 在 `commands.rs` / `main.rs` 暴露新的 Tauri command

命令名固定为：

```rust
load_preview_by_coord
```

- [ ] Step 4: 运行 Rust 单测，确认转绿

```powershell
cargo test --manifest-path src-tauri/Cargo.toml preview_coord
```

### Task 7: 实现 C++ sidecar 的 canonical 解析与生成

**Files:**
- Modify: `src/Batch/SidecarProtocol.hpp`
- Modify: `src/Batch/SidecarProtocol.cpp`
- Modify: `src/entry_sidecar.cpp`

- [ ] Step 1: 在 `SidecarProtocol` 中新增 `preview_coord` 命令类型和请求结构

```cpp
enum class SidecarCommandType {
    ...
    PreviewCoord,
};

struct SidecarPreviewCoordRequest {
    std::string jobId;
    std::string coord;
};
```

- [ ] Step 2: 在 `entry_sidecar.cpp` 增加一个局部 helper，用后端现有权威工具解析并 canonical 校验

```cpp
bool ResolveCanonicalPreviewCoord(const std::string &rawCoord,
                                  int *worldType,
                                  int *seed,
                                  int *mixing,
                                  std::string *canonicalCoord);
```

实现要求：
- 使用 `SearchAnalysis::GetWorldPrefixes()` 匹配世界前缀
- 使用 `SettingsCache::Base36ToBinary()` 解析 mixing
- 使用现有 `BuildWorldCode()` 反向重建 canonical 坐标
- 若重建结果与输入不完全相等，返回失败

- [ ] Step 3: 增加 `RunPreviewCoordCommand()`

逻辑顺序固定为：
1. 解析并 canonical 校验
2. `runtime->Generate(canonicalCoord, 0)`
3. 从 preview sink 取结果
4. 返回 `preview` 事件，并携带：
   - `coord`
   - `worldType`
   - `seed`
   - `mixing`
   - `preview`

- [ ] Step 4: 如已具备 sidecar 可执行文件，则跑 ignored smoke；否则记录为环境阻塞

```powershell
cargo test --manifest-path src-tauri/Cargo.toml sidecar_preview_coord_smoke -- --ignored
```

## Chunk 4: 前端接线、结果页落地与文档同步

### Task 8: 先写前端接线失败测试

**Files:**
- Modify: `desktop/tests/coordPreviewFlow.test.ts`
- Test: `desktop/tests/coordPreviewFlow.test.ts`

- [ ] Step 1: 补充“SearchPanel 的坐标流只依赖 `loadPreviewByCoord`，不调用批量搜索 action”的失败测试

可通过为流程函数注入依赖来验证：

```ts
await runCoordPreviewFlow({
  loadPreviewByCoord,
  openDirectCoordResult,
  primeResolvedPreview,
  setError,
  openResults,
}, "V-SNDST-C-123456-0-D3-HD");
```

- [ ] Step 2: 运行测试，确认接线前仍失败

```powershell
node --test desktop/tests/coordPreviewFlow.test.ts
```

### Task 9: 接线 SearchPanel 到新命令与新状态

**Files:**
- Create: `desktop/src/features/search/coordPreviewFlow.ts`
- Modify: `desktop/src/lib/contracts.ts`
- Modify: `desktop/src/lib/tauri.ts`
- Modify: `desktop/src/features/search/SearchPanel.tsx`

- [ ] Step 1: 在 `contracts.ts` 增加新协议

```ts
export interface CoordPreviewRequest {
  jobId: string;
  coord: string;
}

export interface CoordPreviewEvent extends PreviewEvent {
  coord: string;
}
```

- [ ] Step 2: 在 `tauri.ts` 增加调用入口

```ts
export async function loadPreviewByCoord(
  request: CoordPreviewRequest
): Promise<CoordPreviewEvent>
```

- [ ] Step 3: 在 `coordPreviewFlow.ts` 实现可测的流程函数

函数职责固定为：
- 调用 `loadPreviewByCoord`
- 用 `buildSearchMatchSummaryFromPreview()` 构造单条记录
- 调用 `openDirectCoordResult()`
- 调用 `primeResolvedPreview()`
- 成功后切页，失败后回写错误

- [ ] Step 4: 在 `SearchPanel.tsx` 只保留最薄的事件绑定，不在组件内写大段业务流程

### Task 10: 最终验证与项目文档同步

**Files:**
- Modify: `llmdoc/overview/project.md`

- [ ] Step 1: 在项目总览中补充 desktop 新能力
  建议写法：
  - 搜索页顶部支持标准坐标码直达预览
  - 该链路走后端权威解析，不进入批量搜索 sidecar 流

- [ ] Step 2: 运行前端相关测试

```powershell
node --test `
  desktop/tests/coordQuickSearchUi.test.ts `
  desktop/tests/coordPreviewFlow.test.ts `
  desktop/tests/coordPreviewState.test.ts `
  desktop/tests/searchUiChunk2.test.ts `
  desktop/tests/resultsUiChunk3.test.ts `
  desktop/tests/searchCancelState.test.ts
```

- [ ] Step 3: 运行 Rust 单测

```powershell
cargo test --manifest-path src-tauri/Cargo.toml preview_coord
```

- [ ] Step 4: 运行桌面前端构建

```powershell
corepack yarn --cwd desktop build
```

- [ ] Step 5: 如环境已具备 sidecar 二进制，执行 ignored smoke；否则如实记录阻塞

```powershell
cargo test --manifest-path src-tauri/Cargo.toml sidecar_preview_coord_smoke -- --ignored
```

- [ ] Step 6: 人工回归以下关键路径
  回归清单：
  - 顶部输入标准坐标码后直接进入结果页
  - 结果页仅显示一条记录
  - 右侧地图与输入坐标一致
  - 输入非法坐标时停留在搜索页
  - 批量搜索运行时顶部坐标入口禁用
  - 现有批量搜索、点击结果行预览、清空结果功能不回归

Plan complete and saved to `docs/superpowers/plans/2026-04-23-desktop-coordinate-direct-preview.md`. Ready to execute?
