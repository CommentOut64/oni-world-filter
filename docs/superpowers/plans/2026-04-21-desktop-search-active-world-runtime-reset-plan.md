# Desktop Search Active-World Runtime Reset Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在不改变 desktop + sidecar 搜索结果、取消语义和 CLI 行为的前提下，把搜索热路径的每-seed 状态恢复从“整张 worlds 深拷贝”收缩到“只恢复当前 cluster active worlds 的运行时字段”，以降低内存拷贝和容器重建成本。

**Architecture:** 保留现有 `optimized` 搜索执行顺序与世界生成算法不变，只改 `SettingsCache` / `AppRuntime` 的 seed 级状态恢复边界。worker 初始化阶段预计算固定请求上下文，seed 级只重置 `seed`、`mixConfigs`、`dlcState`、active world 的 `locationType` 与运行时容器字段，随后继续调用现有 `DoSubworldMixing()`、`GetRandomTraits()`、`ApplayTraits()`、`GenerateOverworld()` 和 `MatchFilter()`。

**Tech Stack:** C++23、Rust/Tauri sidecar host、native tests、PowerShell smoke scripts

---

## Background

- 当前第一轮优化已经移除了每 seed 的 `Initialize(0)`，但 [SettingsCache.cpp](F:/oni_world_app-master/src/Setting/SettingsCache.cpp) 里 `CaptureSearchMutableState()` / `RestoreSearchMutableState()` 仍会复制整张 `worlds` 表并再次对全表调用 `ClearMixingsAndTraits()`。
- [entry_sidecar.cpp](F:/oni_world_app-master/src/entry_sidecar.cpp) 的 desktop 搜索请求在单次运行中 `worldType` / `mixing` 固定，因此 `cluster`、`dlcState`、`mixConfigs` 基线和 active world 集合也固定。
- 这意味着当前热路径仍在为本次 seed 根本不会访问的 worlds 支付拷贝和容器重建成本，容易在 4 worker 时转化为内存带宽与分配器压力，而不是算法本身的必要工作。

## Scope

- 只优化 `desktop + sidecar` 的 `optimized` 搜索热路径。
- CLI 保持现状。
- `legacy` fallback 保持可用。
- 不修改 `WorldGen` 算法本体、`Batch::MatchFilter()`、搜索协议、前端调用协议。

## Non-Goals

- 不做 seed 结果缓存。
- 不做近似搜索或跳算。
- 不提前调整 desktop CPU 默认策略。
- 不把 preview 切到新路径。

## File Map

### Existing Files To Modify

- `src/Setting/SettingsCache.hpp`
  - 新增 active-world 运行时快照/恢复结构，只覆盖搜索热路径真正会变的字段。
- `src/Setting/SettingsCache.cpp`
  - 实现 active-world 级别的捕获、恢复与 cluster 固定上下文初始化。
- `src/App/AppRuntime.hpp`
  - 增加搜索请求固定上下文准备接口，保留旧接口。
- `src/App/AppRuntime.cpp`
  - worker 初始化时绑定固定请求上下文；每 seed 只恢复 seed 级运行时状态。
- `src/entry_sidecar.cpp`
  - desktop 搜索 optimized 分支改为传入固定 `worldType/mixing` 上下文，而不是每次通过完整 world code 重新建立全部 mutable state。
- `tests/native/test_settings_cache.cpp`
  - 增加 active-world-only restore 的失败测试和回归测试。
- `tests/native/test_app_runtime_worker_reset.cpp`
  - 增加固定请求上下文 + 多 seed 连续执行的一致性测试。
- `tests/native/test_search_runtime_equivalence.cpp`
  - 补充至少一组 optimized vs legacy 多 seed 结果等价断言，避免只测 parser。

### Existing Files To Re-Verify

- `tests/native/test_batch_search_smoke.cpp`
- `tests/native/test_search_analysis.cpp`
- `desktop/tests/searchCancelState.test.ts`
- `desktop/scripts/searchAnalysisDisplay.test.ts`
- `scripts/smoke/run-sidecar-search-scale.ps1`

## Safety Rules

- 任何优化都必须满足：对于同一 `worldType/mixing/seed`，`summary.start/worldSize/traits/geysers` 与 legacy 完全一致。
- 不允许跳过 `DoSubworldMixing()`、`GetRandomTraits()`、`ApplayTraits()`、`GenerateOverworld()` 中任何一步。
- `mixConfigs.minCount/maxCount/setting` 的恢复必须与当前逻辑一致。
- `unknownCellsAllowedSubworlds2` 中被 mixing 改写的 filter 必须在下一个 seed 前恢复。
- 只允许恢复“当前 cluster 会访问到的 active worlds”；若 cluster 变化，则必须重新准备固定上下文。

## Design Sketch

### 固定请求上下文

在 worker 初始化阶段，根据 `worldType + mixing` 只做一次：

- 解析 cluster prefix
- 应用 mixing code
- 计算 `cluster`
- 计算 `dlcState`
- 捕获 `mixConfigs` 基线
- 收集 active world 名称与 `locationType`
- 捕获这些 active world 的运行时基线

### seed 级恢复

每个 seed 只做：

- 恢复 `seed`
- 恢复 `dlcState`
- 恢复 `mixConfigs`
- 对 active worlds 恢复：
  - `locationType`
  - `startingPositionHorizontal2`
  - `startingPositionVertical2`
  - `subworldFiles2`
  - `unknownCellsAllowedSubworlds2`
  - `worldTemplateRules2`
  - `globalFeatures2`
  - `mixingSubworlds`
- 然后继续执行现有 `GenerateCurrentState()`

### 为什么不会损失准确率

- 所有依赖 seed 的算法步骤仍然原样运行。
- 仅缩小恢复范围，不改变恢复内容。
- active world 的运行时字段仍然每 seed 回到同一基线，再重新执行 mixing/traits/worldgen，因此结果应与 legacy 一致。

## Chunk 1: Characterization Tests First

### Task 1: 为 active-world-only restore 写失败测试

**Files:**
- Modify: `tests/native/test_settings_cache.cpp`
- Modify: `tests/native/test_app_runtime_worker_reset.cpp`
- Modify: `tests/native/test_search_runtime_equivalence.cpp`

- [ ] **Step 1: 在 `test_settings_cache.cpp` 添加 active world 基线测试**

覆盖点：

- 选定一个 `worldType + mixing` 对应的 cluster
- 记录 active world 名称集合
- 人工污染：
  - active world 的 `startingPositionHorizontal2`
  - `startingPositionVertical2`
  - `subworldFiles2`
  - `unknownCellsAllowedSubworlds2`
  - `worldTemplateRules2`
  - `globalFeatures2`
  - `mixingSubworlds`
- 执行新的 active-world restore
- 断言 active world 恢复为基线
- 断言非 active worlds 未被重建或深拷贝污染

- [ ] **Step 2: 在 `test_app_runtime_worker_reset.cpp` 添加固定上下文路径失败测试**

覆盖点：

- 同一 worker 上连续跑 `Seed Fixture`
- legacy `Generate(code, 0)` vs 新固定上下文 `ResetPreparedSearchSeed(seed)` 路径 summary 完全一致
- 中间插入不同 seed 顺序，确保不会串 seed

- [ ] **Step 3: 在 `test_search_runtime_equivalence.cpp` 增加至少一组 desktop 搜索等价断言**

覆盖点：

- `ONI_DESKTOP_SEARCH_RUNTIME=legacy`
- `ONI_DESKTOP_SEARCH_RUNTIME=optimized`
- 至少比较一组 `Request A` 和一组 `Request B`
- 断言命中集合、processed、matches、终态事件一致

- [ ] **Step 4: 运行定向测试，确认红灯**

Run:

```powershell
cmake --build out/build/mingw-debug --target test_settings_cache test_app_runtime_worker_reset test_search_runtime_equivalence
out\build\mingw-debug\src\test_settings_cache.exe
out\build\mingw-debug\src\test_app_runtime_worker_reset.exe
out\build\mingw-debug\src\test_search_runtime_equivalence.exe
```

Expected:

- 至少一个新增断言失败，证明当前实现还停留在整表 `worlds` 恢复。

## Chunk 2: Minimal Active-World Runtime Restore

### Task 2: 在 `SettingsCache` 中拆出 active world 运行时快照

**Files:**
- Modify: `src/Setting/SettingsCache.hpp`
- Modify: `src/Setting/SettingsCache.cpp`
- Modify: `tests/native/test_settings_cache.cpp`

- [ ] **Step 1: 定义最小运行时快照结构**

建议：

```cpp
struct ActiveWorldRuntimeSnapshot {
    std::string name;
    LocationType locationType{};
    MinMax startingPositionHorizontal2;
    MinMax startingPositionVertical2;
    std::vector<const Feature *> globalFeatures2;
    std::vector<const WeightedSubworldName *> subworldFiles2;
    std::vector<const AllowedCellsFilter *> unknownCellsAllowedSubworlds2;
    std::vector<const TemplateSpawnRules *> worldTemplateRules2;
    std::vector<WeightedSubworldName> mixingSubworlds;
};
```

- [ ] **Step 2: 实现 active world 捕获/恢复**

要求：

- 只捕获当前 cluster active worlds
- 恢复时只重置 active worlds 的运行时字段
- 不再复制整张 `worlds`
- 仍保证被 mixing 改写过的 filter 会被 `Restore()` 回基线

- [ ] **Step 3: 跑 `test_settings_cache.exe`，确认转绿**

## Chunk 3: 固定请求上下文接入 `AppRuntime`

### Task 3: 把 optimized 路径改成“prepare request once + reset seed only”

**Files:**
- Modify: `src/App/AppRuntime.hpp`
- Modify: `src/App/AppRuntime.cpp`
- Modify: `src/entry_sidecar.cpp`
- Modify: `tests/native/test_app_runtime_worker_reset.cpp`
- Modify: `tests/native/test_search_runtime_equivalence.cpp`

- [ ] **Step 1: 为 `AppRuntime` 增加固定请求准备接口**

建议：

```cpp
bool PrepareSearchRequest(int worldType, int mixing);
bool ResetPreparedSearchSeed(int seed);
```

要求：

- `PrepareSearchWorker()` / `Generate()` / `GeneratePrepared()` 旧接口保留，避免过大扩散
- 新接口只用于 desktop + sidecar optimized 搜索

- [ ] **Step 2: `entry_sidecar.cpp` optimized 分支改为固定请求模式**

要求：

- worker 初始化时先准备 `worldType/mixing` 固定上下文
- evaluate seed 时直接传 seed
- legacy 分支不改
- preview 不改

- [ ] **Step 3: 运行定向 native tests，确认绿灯**

Run:

```powershell
cmake --build out/build/mingw-debug --target oni-sidecar test_settings_cache test_app_runtime_worker_reset test_search_runtime_equivalence test_batch_search_smoke test_search_analysis
out\build\mingw-debug\src\test_settings_cache.exe
out\build\mingw-debug\src\test_app_runtime_worker_reset.exe
out\build\mingw-debug\src\test_search_runtime_equivalence.exe
out\build\mingw-debug\src\test_batch_search_smoke.exe
out\build\mingw-debug\src\test_search_analysis.exe
```

Expected:

- 全部 PASS

## Chunk 4: Throughput Verification Before Any Further Tuning

### Current Status (2026-04-21)

- 本 Chunk 的唯一目的，是验证 active-world runtime reset 完成后，搜索热路径是否已经重新具备足够扩展性。
- 当前实际 representative scale 数据：
  - `1 worker ≈ 1.82 seeds/s`
  - `2 workers ≈ 2.79 seeds/s`
  - `4 workers ≈ 2.71 seeds/s`
- 判定结果：
  - `2 workers >= 1.4x single-worker`：通过
  - `4 workers >= 1.1x two-worker`：未通过
- 结论：
  - 吞吐验证已经完成
  - 不进入任何后续 desktop CPU policy 调优
  - 当前剩余瓶颈按 profiling 已收敛到 `GenerateOverworld()` 本体，后续若继续只能另起高风险算法专项

### Task 4: 只验证吞吐，不先碰 CPU 策略

**Files:**
- Reuse: `scripts/smoke/run-sidecar-search-scale.ps1`
- Reuse: `desktop/tests/searchCancelState.test.ts`
- Reuse: `desktop/scripts/searchAnalysisDisplay.test.ts`

- [ ] **Step 1: 跑 representative scale smoke**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/smoke/run-sidecar-search-scale.ps1 -SidecarPath out/build/mingw-debug/src/oni-sidecar.exe -SeedEnd 100100
```

记录：

- `1 / 2 / 4 workers` 的 `WallMs`
- `CpuMs`
- `AvgSeedsPerSecond`
- `TotalMatches`

- [ ] **Step 2: 跑 desktop 行为回归**

Run:

```powershell
node --test --experimental-strip-types desktop/tests/searchCancelState.test.ts
node --test --experimental-strip-types desktop/scripts/searchAnalysisDisplay.test.ts
corepack yarn --cwd desktop build
```

- [ ] **Step 3: 按 Gate D 重新判定**

判定条件：

- `2 workers >= 1.4x single-worker`
- `4 workers >= 1.1x two-worker`

如果未满足：

- 停止，不进入 CPU policy 调优
- 记录新的 `WallMs/CpuMs`，继续分析是否剩余瓶颈已转到 `GenerateOverworld()` 本体

## Verification Checklist

- [ ] `cmake --build out/build/mingw-debug --target oni-sidecar test_settings_cache test_app_runtime_worker_reset test_batch_search_smoke test_search_analysis`
- [ ] `out\build\mingw-debug\src\test_settings_cache.exe`
- [ ] `out\build\mingw-debug\src\test_app_runtime_worker_reset.exe`
- [ ] `out\build\mingw-debug\src\test_batch_search_smoke.exe`
- [ ] `out\build\mingw-debug\src\test_search_analysis.exe`
- [ ] `node --test --experimental-strip-types desktop/tests/searchCancelState.test.ts`
- [ ] `node --test --experimental-strip-types desktop/scripts/searchAnalysisDisplay.test.ts`
- [ ] `corepack yarn --cwd desktop build`
- [ ] `powershell -ExecutionPolicy Bypass -File scripts/smoke/run-sidecar-search-scale.ps1 -SidecarPath out/build/mingw-debug/src/oni-sidecar.exe -SeedEnd 100100`

## Out Of Scope

- 不修改 CLI runtime
- 不修改 preview runtime
- 不修改 `WorldGen` 算法行为
- 不直接调高默认 worker
- 不自动提交
