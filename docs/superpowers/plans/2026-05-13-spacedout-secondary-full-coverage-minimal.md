# Spaced Out Secondary Full Coverage Minimal Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在不扩展为“多星球预览列表”的前提下，把桌面端副星预览从“只覆盖部分眼冒金星 worldType”补到“覆盖整个眼冒金星模式中存在 warp 副星的主星/副星二元预览”。

**Architecture:** 不改批量搜索结果结构，不改前端 `primary|secondary` 二元 UI，不让搜索阶段预生成更多地图。只在 native/sidecar 预览链路里，把“副星候选选择”从当前隐式生成结果改为“基于 cluster/world 配置显式锁定 StartWorld + 唯一 warpworld placement”，并把实际生成工作限制为最多两个目标世界。

**Tech Stack:** C++23、Tauri sidecar、jsoncpp、ctest、node:test、TypeScript

---

## File Map

### Existing Files To Modify

- `src/App/AppRuntime.hpp`
  - 现状：只有按当前内建筛选逻辑生成预览的入口。
  - 目标：增加“只生成指定 placement 集合”的最小入口，避免整组 cluster 额外生成。
- `src/App/AppRuntime.cpp`
  - 现状：`GenerateCurrentState()` 只靠 `StartWorld + startingBaseTemplate.contains("::bases/warpworld")` 的隐式筛选决定产出。
  - 目标：抽出“收集可预览 placement / 按 placement 定向生成”的公共逻辑。
- `src/entry_sidecar.cpp`
  - 现状：`GeneratePreviewSession()` 只接 `primary|secondary`，但 secondary 的来源依赖 runtime 隐式产出。
  - 目标：在这里显式解析当前 cluster 的主星 placement 和副星 placement，并只请求生成这两个 placement。
- `tests/native/test_preview_world_session.cpp`
  - 现状：只覆盖 `M-*` 这组 moonlet cluster。
  - 目标：补 `27-31` 这组 classic-style SO cluster 的 characterization 测试。
- `devdoc/v1.0.0/眼冒金星副星独立预览实施方案.md`
  - 现状：当前文档没有明确写出“副星选择来自 cluster warp placement”。
  - 目标：同步到最终规则。

### Optional Files To Modify

- `tests/native/test_sidecar_protocol.cpp`
  - 如果需要补协议级 `hasSecondaryPreview` 回归样例，可在这里加最小断言。

---

## Constraints

- 不改批量搜索 `match` 事件协议。
- 不把前端 `PreviewTarget` 从 `primary|secondary` 扩成任意世界列表。
- 不把 `SearchMatchSummary` 扩成携带副星列表。
- 不在结果页缓存所有 `worldPlacements` 预览。
- 只支持“主星 + 一个副星”；其他 asteroid 明确保留不做。
- 默认不增加预览热路径的生成世界数量上限：每次 preview 最多生成 2 个世界。

## Design Decision

当前项目卡住的根因不是前端按钮，也不是“眼冒金星只能看 M-*”，而是 native 预览链路把“secondary 来源”建立在 runtime 的隐式结果上，导致 `SNDST-C-/FRST-C-/SWMP-C-/PRE-C-/CER-C-` 这组 cluster 虽然在本地 `data.zip` 的 cluster JSON 里都存在 warp target placement，但没有稳定落进 sidecar 的 `secondaryPreview`。

因此本次不继续给现有隐式筛选打补丁，而采用 **B. 局部重构**：

1. 先基于当前 `SettingsCache.cluster->worldPlacements` 和 world 定义显式算出“主星 placement / 副星 placement”。
2. 再把 preview 生成限制到这两个 placement。
3. 保持前端、缓存键、协议形态都不变。

这样做的收益是：

- 覆盖范围从“当前碰巧有 secondary 的 worldType”拓展到“整个眼冒金星里所有存在 warp 副星的 cluster”。
- 性能上不会因为完整 cluster 里有更多小行星就多生成无关世界。
- 变更面仍然控制在 native/sidecar 预览闭环。

## Secondary Selection Rule

以当前项目本地 `data.zip` 资产为准，secondary 选择规则固定为：

1. `primaryPlacementIndex`
   - 优先取 `locationType == StartWorld`
   - 若不存在，再回退 `cluster.startWorldIndex`
2. `secondaryPlacementIndex`
   - 在 `worldPlacements` 中，排除 `primaryPlacementIndex`
   - 只认引用到的 `World` 满足 `startingBaseTemplate` 包含 `::bases/warpworld`
   - 必须唯一命中；若找到 0 个或多于 1 个，都按“当前 cluster 无可预览副星”处理
3. 只有 `secondaryPlacementIndex` 唯一确定时，`hasSecondaryPreview = true`
4. 只生成 `primaryPlacementIndex` 和 `secondaryPlacementIndex` 对应世界

这条规则完全不依赖坐标前缀，也不依赖前端世界分类名称。

## Chunk 1: Lock Down Current Asset Facts

### Task 1: 为 classic-style SO cluster 写失败测试

**Files:**
- Modify: `tests/native/test_preview_world_session.cpp`
- Reference: `src-tauri/binaries/data.zip`

- [ ] **Step 1: 给 `SNDST-C-/PRE-C-/CER-C-/FRST-C-/SWMP-C-` 各补一条失败断言**

断言目标：

1. 本地 `data.zip` 对应 cluster 都存在 warpworld placement。
2. `GeneratePreviewSessionForTest(worldType, seed, mixing, ...)` 最终应带 `secondaryPreview`。

- [ ] **Step 2: 运行测试，确认当前失败**

Run:

```powershell
ctest --test-dir out/build/x64-release -C Release --output-on-failure -R test_preview_world_session
```

Expected:

- `27-31` 至少有一条 FAIL，且失败点落在 `secondaryPreview` 缺失。

## Chunk 2: Extract Explicit Primary/Secondary Placement Resolution

### Task 2: 在 sidecar 层显式解析主星和副星 placement

**Files:**
- Modify: `src/entry_sidecar.cpp`

- [ ] **Step 1: 新增 placement 解析 helper 的失败测试思路**

helper 目标：

```cpp
struct PreviewPlacementSelection {
    int primaryPlacementIndex;
    std::optional<int> secondaryPlacementIndex;
};
```

- [ ] **Step 2: 实现最小 helper**

职责：

1. 从 `SettingsCache.cluster->worldPlacements` 找到 `StartWorld`
2. 回退 `startWorldIndex`
3. 找唯一的 `startingBaseTemplate.contains("::bases/warpworld")` 非主星 placement

- [ ] **Step 3: 遇到无副星 cluster 时保持当前行为**

要求：

1. `secondaryPlacementIndex = nullopt`
2. `hasSecondaryPreview = false`
3. 不报错，不影响主星预览

## Chunk 3: Restrict Preview Generation To Two Placements

### Task 3: 把 runtime preview 从“隐式筛选”改成“定向生成 placement”

**Files:**
- Modify: `src/App/AppRuntime.hpp`
- Modify: `src/App/AppRuntime.cpp`
- Modify: `src/entry_sidecar.cpp`

- [ ] **Step 1: 为按 placement 生成写最小入口**

建议形态：

```cpp
bool GenerateCurrentStateForPlacementIndexes(
    int traitsFlag,
    const std::vector<int>& placementIndexes
);
```

- [ ] **Step 2: 复用现有单世界生成逻辑**

要求：

1. 不复制 `BuildSummary / BuildPreview / trait apply / WorldGen.GenerateOverworld` 的核心代码
2. 只把“遍历哪些 world”这一层参数化

- [ ] **Step 3: 生成数量上限固定为 2**

sidecar 传入：

1. 主星 placement
2. 若存在则再加一个副星 placement

这样保持最小性能成本。

## Chunk 4: Rewire Preview Session To Use Explicit Selection

### Task 4: 让 `GeneratePreviewSession()` 不再依赖隐式 secondary

**Files:**
- Modify: `src/entry_sidecar.cpp`
- Optional Modify: `tests/native/test_sidecar_protocol.cpp`

- [ ] **Step 1: 在 `GeneratePreviewSession()` 中先解析 placement selection**

顺序：

1. `BuildWorldCode`
2. `SettingsCache.CoordinateChanged`
3. `BuildWorldList`
4. `ResolvePreviewPlacementSelection`
5. `GenerateCurrentStateForPlacementIndexes`

- [ ] **Step 2: 保持输出协议不变**

仍然只输出：

1. `primaryPreview`
2. `secondaryPreview`
3. `hasSecondaryPreview`

- [ ] **Step 3: 对不存在 secondary 的 worldType 保持兼容**

要求：

1. 主星仍正常返回
2. `secondary` 请求仍报“current seed 无副星”
3. 不让主星预览回归

## Chunk 5: Verification And Docs

### Task 5: 回归验证与文档同步

**Files:**
- Modify: `devdoc/v1.0.0/眼冒金星副星独立预览实施方案.md`

- [ ] **Step 1: 运行 native 回归**

Run:

```powershell
ctest --test-dir out/build/x64-release -C Release --output-on-failure -R test_preview_world_session
ctest --test-dir out/build/x64-release -C Release --output-on-failure -R test_geyser_parameter_calculator
ctest --test-dir out/build/x64-release -C Release --output-on-failure -R test_sidecar_protocol
```

Expected:

- 新增 `27-31` 样例都能拿到 `secondaryPreview`
- 既有 `M-*` 样例不回归

- [ ] **Step 2: 运行前端回归**

Run:

```powershell
node --test --experimental-strip-types desktop/tests/previewWorldSwitchState.test.ts
node --test --experimental-strip-types desktop/tests/previewWorldSwitchUi.test.ts
corepack yarn --cwd desktop build
```

Expected:

- 当前 `primary|secondary` UI 无需协议改动即可继续工作

- [ ] **Step 3: 更新文档**

写明：

1. 当前项目只支持主星 + 一个 warp 副星
2. secondary 来源于 cluster 中第一个 warpworld placement
3. 不再依赖 world code 前缀判断
