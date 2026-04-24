# Desktop Search Stability-First Performance Optimization Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在不牺牲 desktop + sidecar 交互搜索稳定性、准确性、取消语义和 CLI 现状的前提下，系统性降低搜索前分析卡顿，并移除搜索热路径中的重初始化瓶颈，让 CPU 真正有机会被有效吃满。

**Architecture:** 本方案按“先低风险、后高风险、每阶段都做等价性验证”的顺序推进。第一阶段只消除 desktop 冷路径的重复 sidecar 启动与重复 settings 初始化；第二阶段只在 desktop + sidecar 搜索热路径内引入“每 worker 初始化一次、每 seed 只重置最小可变态”的优化，并保留 legacy fallback；第三阶段仅在前两阶段的结果等价和伸缩收益都被证明之后，才重新调整 desktop 默认 CPU 策略。任何阶段一旦出现结果漂移、取消行为回退或扩展性不达标，都必须停止后续步骤并回退。

**Tech Stack:** Rust/Tauri 2、C++23、Batch/BatchCpu、SearchAnalysis、PowerShell smoke scripts、native tests、Node test、React 19 + TypeScript

---

## File Map

### New Files To Create

- `src-tauri/src/control_sidecar.rs`
  - 职责：管理 desktop 专用常驻 control sidecar，会话级复用 `get_search_catalog` / `analyze_search_request` / `preview`，并处理子进程异常退出后的重启与串行化请求。
- `scripts/smoke/run-sidecar-analysis-latency.ps1`
  - 职责：分别测量 fresh sidecar 与 warm control sidecar 上的 `search_catalog` / `analyze_search_request` 延迟，验证冷路径优化收益。
- `scripts/smoke/run-sidecar-search-scale.ps1`
  - 职责：对“零命中”和“高命中”代表性请求按 worker 数采样，输出 wall time / cpu time / seeds/s 曲线，判断搜索热路径是否恢复扩展性。
- `tests/native/test_search_runtime_equivalence.cpp`
  - 职责：对同一批代表性搜索请求比较 legacy 搜索路径与优化后路径的命中 seed 集合、终态统计、错误与取消语义，作为热路径改造的结果等价门禁。
- `tests/native/test_app_runtime_worker_reset.cpp`
  - 职责：验证 `AppRuntime` 的“每 worker 初始化一次 + 每 seed 重置最小状态”路径与 legacy `Initialize()` 每 seed 重置路径在 summary/traits/geysers 上完全一致。

### Existing Files To Modify

- `src-tauri/src/commands.rs`
  - 把 `get_search_catalog` / `analyze_search_request` / `load_preview` 切到常驻 control sidecar，会保持命令签名不变。
- `src-tauri/src/main.rs`
  - 注册新的 `control_sidecar` 模块与应用状态。
- `src-tauri/src/sidecar.rs`
  - 保留 streaming search 逻辑；下沉可复用的 sidecar line protocol / read-write helpers；为 control sidecar 和 search sidecar 复用公共逻辑。
- `src-tauri/src/state.rs`
  - 给 `AppState` 增加 control sidecar 管理器；保留现有 `JobRegistry` 不变。
- `src/App/AppRuntime.hpp`
  - 增加搜索专用 worker 级初始化与 seed 级重置接口；保留现有 `Initialize()` / `Generate()` 供 legacy 路径与 preview 使用。
- `src/App/AppRuntime.cpp`
  - 实现 worker 级基线快照、seed 级最小状态恢复、legacy/optimized 双路径共存。
- `src/Setting/SettingsCache.hpp`
  - 声明“搜索可变态快照”结构及捕获/恢复接口，只覆盖真正会被 `Generate()` 修改的字段。
- `src/Setting/SettingsCache.cpp`
  - 实现可变态快照的捕获与恢复；不改变 settings 解析逻辑，不改变世界生成算法。
- `src/entry_sidecar.cpp`
  - desktop + sidecar 搜索路径接入优化后的 `AppRuntime` reset 流程；保留 legacy fallback；必要时做 process 内缓存与模式选择。
- `src/WorldGen.hpp`
  - 仅在必须时调整构造/调用边界，使优化后的 `AppRuntime` 能在不改变算法行为的前提下复用 worker 级状态。
- `src/WorldGen.cpp`
  - 只允许做为支持新边界所需的最小改动；禁止修改世界生成算法本体。
- `src/CMakeLists.txt`
  - 注册新增 native tests。
- `tests/native/test_settings_cache.cpp`
  - 补充 settings 可变态快照的捕获/恢复测试。
- `tests/native/test_batch_search_smoke.cpp`
  - 补充优化路径下的 started/completed/cancelled 烟雾测试。
- `tests/native/test_search_analysis.cpp`
  - 补充 cold/warm 分析路径一致性和输出形状断言。
- `tests/native/test_desktop_search_policy.cpp`
  - 仅在最终 CPU 策略调优阶段修改；前两阶段不允许先动这里。
- `desktop/scripts/searchAnalysisDisplay.test.ts`
  - 继续验证前端对 `analysis` 输出的展示逻辑不漂移。
- `desktop/tests/searchCancelState.test.ts`
  - 继续验证取消态冻结与终态行为不漂移。
- `desktop/src/lib/tauri.ts`
  - 原则上不改协议；只有当 control sidecar 需要额外 prewarm 命令且无法在后端透明实现时才修改。
- `llmdoc/overview/project.md`
  - 同步 desktop 搜索当前的 control sidecar 复用、search runtime reset 与 fallback 边界。
- `llmdoc/decisions/2026-04-09-tauri-desktop-refactor-plan.md`
  - 同步 desktop 与 sidecar 当前交互模型，避免文档继续把 catalog/analyze 描述为“一次命令一次 sidecar”的固定模型。
- `llmdoc/decisions/2026-04-19-search-initial-worker-bounded-recovery-plan.md`
  - 仅在最终 CPU 策略阶段真的调整默认策略时更新；前两阶段不提前改写。

## Constraints

- 只优化 `desktop + sidecar` 交互搜索路径；CLI 现状必须保持不变。
- 不修改世界生成算法判定逻辑；`Diagram`、`GenerateOverworld()`、`MatchFilter()` 的行为必须保持一致。
- 不允许把性能收益建立在“少算、跳算、近似算、缓存 seed 结果”之上。
- 不引入新的前端配置项；fallback 与模式切换只能是内部开关。
- 不自动提交；每个 chunk 结束后由 `wgh` 手动决定是否提交。
- 没有通过当前 chunk 的全部测试前，禁止进入下一个 chunk。
- 如果发现需要修改 `WorldGen` 算法本体、模板选择规则或 matcher 规则，必须停下重新评估，不能在本计划内静默扩张。

## ROI Gate

这次先选 **B. 局部重构**，但严格分阶段控制范围。

- 触发证据 1：fresh `analyze_search_request` 约 `268~308ms`，而同一进程内 warm `analyze` 约 `10.1ms`，说明 desktop 冷路径存在明显的重复 sidecar 启动与重复 settings 初始化成本。
- 触发证据 2：零命中代表性搜索里，`1 worker = 7.16 seeds/s`、`2 = 11.15`、`4 = 11.13`、`8 = 10.04`，说明热路径不是简单的“默认 worker 太少”，而是搜索内核本身存在更硬的瓶颈。
- 重构范围限制：
  - Chunk 2 只动 Rust host 的 control sidecar 生命周期；
  - Chunk 3 只动 `AppRuntime / SettingsCache / entry_sidecar` 的搜索热路径重置边界；
  - `WorldGen` 只允许做必要的接口性配合；
  - CPU 策略放到最后，且只有在扩展性恢复后才允许触碰。

## Current Baseline Evidence

以下数据已在当前仓库上实际测得，后续每个阶段都必须对照它复测：

- fresh `search -> started`: `5~8ms`
- fresh `analyze_search_request`: `268.5ms`、`308.1ms`
- 同一 sidecar 复用：
  - 首次 `search_catalog`: `255.9ms`
  - warm `search_analysis`: `10.1ms`
  - warm `search -> started`: `2.0ms`
- 零命中搜索扩展性（worldType=13，mixing=625，seed=100000..100100）：
  - `1 worker`: `7.16 seeds/s`
  - `2 workers`: `11.15 seeds/s`
  - `4 workers`: `11.13 seeds/s`
  - `8 workers`: `10.04 seeds/s`

## Result-Drift Gates

### Gate A: 冷路径输出等价

`catalog` / `analysis` / `preview` 改为常驻 control sidecar 后，以下内容必须与原 fresh sidecar 路径完全一致：

- `SearchCatalogPayload`
- `SearchAnalysisPayload`
- `preview` 事件结构与关键字段
- 错误消息语义

### Gate B: 热路径结果等价

优化后的 desktop 搜索路径与 legacy 路径对同一请求必须满足：

- 命中 seed 集合完全一致
- `processedSeeds` / `totalSeeds` / `totalMatches` 完全一致
- `summary.start`、`summary.worldSize`、`summary.traits`、`summary.geysers` 完全一致
- `failed` / `cancelled` 终态行为一致

### Gate C: 取消/状态行为不回退

必须持续通过现有 desktop 行为验证：

- 取消后不再继续增长命中数
- 取消中不再接受晚到 `progress/match`
- 终态 `current seeds/s` 清零
- `isCancelling`、`activeJobId`、`activeWorkers` 行为不漂移

### Gate D: 性能收益必须真实

只有在下面两条同时满足时，才允许进入最终 CPU 策略调优：

- warm 分析延迟稳定落到 `< 20ms`
- 零命中代表性搜索的扩展性恢复到：
  - `2 workers >= 1.4x single-worker seeds/s`
  - `4 workers >= 1.1x two-worker seeds/s`

若达不到，先停在热路径继续查瓶颈，禁止靠放大默认 worker 掩盖问题。

### Gate D Current Verdict (2026-04-21)

当前实际 representative scale 数据：

- `1 worker ≈ 1.82 seeds/s`
- `2 workers ≈ 2.79 seeds/s`
- `4 workers ≈ 2.71 seeds/s`

判定结果：

- `2 workers >= 1.4x single-worker`：通过
- `4 workers >= 1.1x two-worker`：未通过

因此 Gate D 未通过。按本计划约束，desktop 默认 CPU 策略调优在当前阶段终止，不进入 `Chunk 4` 实施。

## Internal Fallback Strategy

- Rust host 侧增加隐藏开关：
  - `ONI_DESKTOP_CONTROL_SIDECAR=0|1`
  - 默认先按 `0` 引入代码并让测试覆盖，切换到 `1` 前必须通过全部 Gate A/C。
- search 热路径侧增加隐藏开关：
  - `ONI_DESKTOP_SEARCH_RUNTIME=legacy|optimized`
  - 在 Chunk 3 完成前默认 `legacy`
  - 只有在 native 等价测试和 smoke 都通过后，才把 desktop 默认切到 `optimized`
- 任一 Gate 失败时，必须先把默认值切回 legacy，再继续排查；禁止带着失败继续推进后续 chunk。

## Characterization Matrix

以下代表性请求必须在 legacy 与 optimized 两条路径上反复比较：

- `Request A`
  - worldType=`13`
  - mixing=`625`
  - seed range=`100000..100100`
  - constraints=`required=[] forbidden=[] distance=[] count=[]`
  - 目的：覆盖高命中、高事件量路径
- `Request B`
  - worldType=`13`
  - mixing=`625`
  - seed range=`100000..100100`
  - constraints=`required=['cryo_tank']`
  - 目的：覆盖零命中、纯热路径吞吐
- `Request C`
  - worldType=`13`
  - mixing=`625`
  - seed range=`100000..100500`
  - constraints=`required=['steam'] forbidden=['methane']`
  - 目的：覆盖 required/forbidden 组合
- `Request D`
  - worldType=`13`
  - mixing=`625`
  - seed range=`100000..100500`
  - constraints=`count=[steam:1..2] distance=[steam:0..120]`
  - 目的：覆盖 count/distance 路径
- `Seed Fixture`
  - seeds=`100000, 100030, 100123, 100500, 101337`
  - 目的：覆盖已知回归 seed、常规 seed 和不同 summary 组合

---

## Chunk 1: Baseline Harness And No-Drift Guards

### Task 1: 建立 cold/warm/scale 基线与结果等价测试骨架

**Files:**
- Create: `scripts/smoke/run-sidecar-analysis-latency.ps1`
- Create: `scripts/smoke/run-sidecar-search-scale.ps1`
- Create: `tests/native/test_search_runtime_equivalence.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/native/test_main.cpp`（仅当新测试需要注册时）

- [ ] **Step 1: 先写 characterization harness，而不是先改实现**

要求：

- `run-sidecar-analysis-latency.ps1` 必须同时输出：
  - fresh `search_catalog` latency
  - fresh `search_analysis` latency
  - same-process warm `search_analysis` latency
  - same-process warm `search -> started` latency
- `run-sidecar-search-scale.ps1` 必须同时输出：
  - worker 数
  - wall time
  - cpu time
  - average seeds/s
  - total matches
- `test_search_runtime_equivalence.cpp` 先只建立请求矩阵和比较工具，暂时可以比较 legacy 对 legacy，确保测试骨架可运行。

- [ ] **Step 2: 跑基线脚本和测试，确认它们在当前代码上可用**

Run:

```powershell
cmake --build out/build/mingw-debug --target test_search_runtime_equivalence oni-sidecar
out\build\mingw-debug\src\test_search_runtime_equivalence.exe
powershell -ExecutionPolicy Bypass -File scripts/smoke/run-sidecar-analysis-latency.ps1 -SidecarPath out/build/mingw-debug/src/oni-sidecar.exe
powershell -ExecutionPolicy Bypass -File scripts/smoke/run-sidecar-search-scale.ps1 -SidecarPath out/build/mingw-debug/src/oni-sidecar.exe
```

Expected:

- `test_search_runtime_equivalence.exe` PASS
- 两个 smoke script 都能稳定输出基线数据
- 当前基线应接近本文档“Current Baseline Evidence”中的已知量级

- [ ] **Step 3: 把这些脚本和测试列入后续每个 chunk 的必跑项**

要求：

- 文档后续每个 chunk 的 Verification Checklist 都必须包含它们
- 后续任何性能结论必须引用这些统一脚本，不允许临时手敲零散命令代替

## Chunk 2: Persistent Control Sidecar For Catalog / Analyze / Preview

### Task 2: 引入 desktop 专用常驻 control sidecar，但不动 streaming search

**Files:**
- Create: `src-tauri/src/control_sidecar.rs`
- Modify: `src-tauri/src/state.rs`
- Modify: `src-tauri/src/main.rs`
- Modify: `src-tauri/src/sidecar.rs`
- Modify: `src-tauri/src/commands.rs`

- [ ] **Step 1: 先为 control sidecar 生命周期写 Rust 单测**

至少覆盖：

- 连续两次 `catalog/analyze` 复用同一个 child，而不是重新 spawn
- child 意外退出后，下一次请求会自动重建
- `preview` 走 control sidecar 不会污染 streaming search job registry
- 设置 `ONI_DESKTOP_CONTROL_SIDECAR=0` 时，仍回落到旧的一次请求一次 sidecar

- [ ] **Step 2: 运行 Rust 单测，确认红灯**

Run:

```powershell
cargo test --manifest-path src-tauri/Cargo.toml control_sidecar sidecar::tests state::tests -- --nocapture
```

Expected:

- 至少一个新增 control sidecar 生命周期断言失败，证明当前实现仍是 ephemeral request model

- [ ] **Step 3: 实现 `control_sidecar.rs` 的最小会话模型**

实现要求：

- 单独维护一个常驻 child，不复用搜索 streaming child
- 请求串行化，不引入并发复用协议
- 只复用：
  - `get_search_catalog`
  - `analyze_search_request`
  - `load_preview`
- 若 child EOF / 非零退出：
  - 清理句柄
  - 下一次请求重建
- stderr 必须完整保留并带回错误，不允许吞掉诊断

建议暴露最小接口：

```rust
pub struct ControlSidecarManager { ... }

impl ControlSidecarManager {
    pub fn request(&self, request: &Value) -> Result<Vec<Value>, HostError>;
    pub fn reset(&self);
}
```

- [ ] **Step 4: 将 Tauri commands 切到 control sidecar**

要求：

- `commands.rs` 的函数签名不改
- `desktop/src/lib/tauri.ts` 的调用不改
- `start_search_streaming()` 继续使用独立 streaming search child，不进入 control sidecar
- 默认先保留 `ONI_DESKTOP_CONTROL_SIDECAR=0`，代码接好但不开默认

- [ ] **Step 5: 跑 Rust/desktop 验证，确认无漂移**

Run:

```powershell
cargo test --manifest-path src-tauri/Cargo.toml control_sidecar sidecar::tests state::tests -- --nocapture
node --test --experimental-strip-types desktop/scripts/searchAnalysisDisplay.test.ts
node --test --experimental-strip-types desktop/tests/searchCancelState.test.ts
corepack yarn --cwd desktop build
```

Expected:

- Rust tests 全绿
- analysis 展示脚本测试继续通过
- cancel state 测试继续通过
- desktop build 通过

### Task 3: 打开 control sidecar 默认值并验证冷路径收益

**Files:**
- Modify: `src-tauri/src/commands.rs`
- Modify: `src-tauri/src/control_sidecar.rs`
- Modify: `desktop/src/state/searchStore.ts`（仅当需要显式 prewarm 且后端无法透明完成时）

- [ ] **Step 1: 先加一个“开关打开后必须提速”的 smoke 门禁**

门禁要求：

- warm `search_analysis < 20ms`
- warm `search -> started < 10ms`
- `search_catalog` / `analysis` 输出内容与 legacy 路径完全一致

- [ ] **Step 2: 把 `ONI_DESKTOP_CONTROL_SIDECAR` 默认值切到 `1`**

要求：

- 仍保留关闭开关用于紧急回退
- 若存在 bootstrap 时机，优先通过已有 `get_search_catalog` 预热 control sidecar，不新增前端协议

- [ ] **Step 3: 跑 latency + desktop 验证，确认 Gate A/C 通过**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/smoke/run-sidecar-analysis-latency.ps1 -SidecarPath out/build/mingw-debug/src/oni-sidecar.exe
node --test --experimental-strip-types desktop/scripts/searchAnalysisDisplay.test.ts
node --test --experimental-strip-types desktop/tests/searchCancelState.test.ts
corepack yarn --cwd desktop build
```

Expected:

- warm `search_analysis` 落到 `< 20ms`
- warm `search -> started` 维持毫秒级
- 前端行为测试无回退

## Chunk 3: Remove Per-Seed Heavy Reinitialize In Search Hot Path

### Task 4: 先把 `SettingsCache` 的“可变态”从整份 settings 拆出来

**Files:**
- Modify: `src/Setting/SettingsCache.hpp`
- Modify: `src/Setting/SettingsCache.cpp`
- Modify: `tests/native/test_settings_cache.cpp`

- [ ] **Step 1: 为 settings 可变态快照写失败测试**

至少覆盖：

- 捕获快照后，修改以下字段，再恢复，结果必须完全回到基线：
  - `seed`
  - `cluster`
  - `mixConfigs`
  - `worlds`
  - `m_dlcState`
- 恢复后再次调用 `CoordinateChanged()` 和 `DoSubworldMixing()`，输出必须与首次调用一致

- [ ] **Step 2: 运行定向 native test，确认红灯**

Run:

```powershell
cmake --build out/build/mingw-debug --target test_settings_cache
out\build\mingw-debug\src\test_settings_cache.exe
```

Expected:

- 至少一个“恢复后结果不一致”断言失败

- [ ] **Step 3: 实现最小可变态快照接口**

建议结构：

```cpp
struct SearchMutableStateSnapshot {
    int seed = 0;
    ClusterLayout *cluster = nullptr;
    std::vector<MixingConfig> mixConfigs;
    std::map<std::string, World> worlds;
    int dlcState = 0;
};
```

要求：

- 只覆盖搜索热路径真正会变的字段
- 不复制 `defaults` / `traits` / `subworlds` / `templates` / `clusters` 等大块只读数据
- 不改变 `LoadSettingsCache()` 与 blob 解析逻辑

- [ ] **Step 4: 跑 native test，确认恢复逻辑转绿**

Run:

```powershell
cmake --build out/build/mingw-debug --target test_settings_cache
out\build\mingw-debug\src\test_settings_cache.exe
```

Expected:

- `test_settings_cache.exe` PASS

### Task 5: 在 `AppRuntime` 中并存 legacy 与 optimized 两条搜索执行路径

**Files:**
- Modify: `src/App/AppRuntime.hpp`
- Modify: `src/App/AppRuntime.cpp`
- Create: `tests/native/test_app_runtime_worker_reset.cpp`
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: 先写 `AppRuntime` 等价性失败测试**

至少覆盖：

- legacy `Initialize(0) + Generate(code, 0)` 与 optimized worker-reset 路径，对 `Seed Fixture` 的 `summary.start/worldSize/traits/geysers` 完全一致
- 连续处理多个 seed 时，optimized 路径不会串掉上一 seed 的 `mixConfigs/worlds`
- preview 仍然走 legacy 路径，不被新的 search-only API 污染

- [ ] **Step 2: 运行 native test，确认红灯**

Run:

```powershell
cmake --build out/build/mingw-debug --target test_app_runtime_worker_reset
out\build\mingw-debug\src\test_app_runtime_worker_reset.exe
```

Expected:

- 新增 optimized path 相关断言失败，因为当前还不存在 worker-reset 路径

- [ ] **Step 3: 在 `AppRuntime` 增加搜索专用 API，但保留旧接口**

建议最小接口：

```cpp
void PrepareSearchWorker();
bool ResetSearchSeed(const std::string &code);
bool GeneratePrepared(int traitsFlag);
```

要求：

- `Initialize()` / `Generate()` 对 preview 与 legacy 路径保持兼容
- `PrepareSearchWorker()` 只在 worker 初始化时调用一次
- `ResetSearchSeed()` 必须先恢复 `SettingsCache` 的可变态快照，再应用当前 seed 的 coordinate code
- `GeneratePrepared()` 只负责在已经准备好的 seed 上生成 summary/preview
- 不允许 silent fallback；optimized 路径出错必须明确失败并走测试暴露

- [ ] **Step 4: 跑 `AppRuntime` 等价测试，确认绿灯**

Run:

```powershell
cmake --build out/build/mingw-debug --target test_app_runtime_worker_reset test_settings_cache
out\build\mingw-debug\src\test_app_runtime_worker_reset.exe
out\build\mingw-debug\src\test_settings_cache.exe
```

Expected:

- 两个测试程序都 PASS

### Task 6: 仅对 desktop + sidecar 搜索接入 optimized path，并保留 legacy fallback

**Files:**
- Modify: `src/entry_sidecar.cpp`
- Modify: `tests/native/test_batch_search_smoke.cpp`
- Modify: `tests/native/test_search_analysis.cpp`
- Modify: `tests/native/test_search_runtime_equivalence.cpp`

- [ ] **Step 1: 先为 sidecar 搜索双路径写失败测试**

至少覆盖：

- `ONI_DESKTOP_SEARCH_RUNTIME=legacy` 时，走旧的 per-seed `Initialize()` 路径
- `ONI_DESKTOP_SEARCH_RUNTIME=optimized` 时，走新的 worker-reset 路径
- 两条路径对 `Characterization Matrix` 的命中集合、终态 stats、错误消息、取消终态完全一致

- [ ] **Step 2: 运行 native test，确认红灯**

Run:

```powershell
cmake --build out/build/mingw-debug --target test_search_runtime_equivalence test_batch_search_smoke test_search_analysis oni-sidecar
out\build\mingw-debug\src\test_search_runtime_equivalence.exe
out\build\mingw-debug\src\test_batch_search_smoke.exe
out\build\mingw-debug\src\test_search_analysis.exe
```

Expected:

- 至少一个 optimized-vs-legacy 等价断言失败

- [ ] **Step 3: 修改 `entry_sidecar.cpp` 的搜索 worker 初始化与 evaluateSeed**

实现要求：

- `initializeWorker`
  - legacy：保留 `runtime->Initialize(0)`
  - optimized：改成 `runtime->PrepareSearchWorker()`
- `evaluateSeed`
  - legacy：保留现有 per-seed `Initialize(0)` 路径
  - optimized：改成 `ResetSearchSeed(code)` + `GeneratePrepared(0)`
- 只对 desktop + sidecar 搜索启用 optimized 模式；CLI 与 preview 不变
- 默认值在这一步前仍保持 `legacy`

- [ ] **Step 4: 跑 native 等价验证，确认 Gate B 通过**

Run:

```powershell
cmake --build out/build/mingw-debug --target oni-sidecar test_search_runtime_equivalence test_batch_search_smoke test_search_analysis test_app_runtime_worker_reset test_settings_cache
out\build\mingw-debug\src\test_search_runtime_equivalence.exe
out\build\mingw-debug\src\test_batch_search_smoke.exe
out\build\mingw-debug\src\test_search_analysis.exe
out\build\mingw-debug\src\test_app_runtime_worker_reset.exe
out\build\mingw-debug\src\test_settings_cache.exe
```

Expected:

- 所有 native 测试都 PASS

- [ ] **Step 5: 打开 optimized 默认值并跑端到端 smoke**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/smoke/run-sidecar-search-scale.ps1 -SidecarPath out/build/mingw-debug/src/oni-sidecar.exe
node --test --experimental-strip-types desktop/tests/searchCancelState.test.ts
node --test --experimental-strip-types desktop/scripts/searchAnalysisDisplay.test.ts
corepack yarn --cwd desktop build
cargo test --manifest-path src-tauri/Cargo.toml sidecar::tests state::tests -- --nocapture
```

Expected:

- desktop 行为测试与 Rust cancel tests 全绿
- `run-sidecar-search-scale.ps1` 显示：
  - `2 workers >= 1.4x single-worker seeds/s`
  - `4 workers >= 1.1x two-worker seeds/s`

- [ ] **Step 6: 若 Gate D 未通过，则停止后续 CPU 调优**

要求：

- 不允许因为“吞吐还没理想”就直接去扩大默认 worker
- 必须先记录新的 scale 数据，再决定是否需要单独的 deeper-state-split 方案

## Chunk 4: Retune Desktop CPU Policy Only After Hot Path Scales Again

### Current Status

- 本 Chunk 前置条件是 Gate D 通过。
- 截至 2026-04-21，Gate D 未通过，因此本 Chunk 不执行。
- 结论不是“跳过 Gate D 继续做”，而是“停止默认 CPU 策略调优”。
- 若后续继续推进，只能另起高风险专项，目标应转为 `WorldGen::GenerateOverworld()` 核心算法 / 数据结构优化，而不是继续盲调 desktop 默认 worker 策略。

### Task 7: 仅在热路径恢复扩展性后，重新调整 desktop 默认 CPU 策略

**Files:**
- Modify: `src/Batch/DesktopSearchPolicy.cpp`
- Modify: `src/Batch/DesktopSearchPolicy.hpp`
- Modify: `tests/native/test_desktop_search_policy.cpp`
- Modify: `scripts/smoke/run-sidecar-search-worker-ramp.ps1`
- Modify: `llmdoc/decisions/2026-04-19-search-initial-worker-bounded-recovery-plan.md`

- [x] **Step 0: Gate D 未通过，本 Task 终止**

记录：

- 不执行新的策略失败测试
- 不修改 `DesktopSearchPolicy`
- 不扩大 desktop 默认 worker
- 当前主线优化闭环停在“低风险热路径优化 + 瓶颈定位已完成”

- [ ] **Step 1: 先用新的 scale 数据决定是否真的需要改策略**

判定条件：

- 如果热路径优化后，现有 `balanced/turbo` 已能把 representative workload 跑到满意区间，则这一步可以不做
- 只有当 scale script 证明还有稳定可用的 headroom 时，才允许写策略失败测试

- [ ] **Step 2: 若需要改策略，先写失败测试固定新规则**

至少覆盖：

- `balanced` 默认 ceiling 与 startup headroom 不会回退到比当前更保守
- `turbo` 不会因为新策略把 startup 调得过高而再次引入明显降速
- worker ramp smoke 仍能从 startup 回升到 runtime ceiling

- [ ] **Step 3: 运行策略测试，确认红灯**

Run:

```powershell
cmake --build out/build/mingw-debug --target test_desktop_search_policy
out\build\mingw-debug\src\test_desktop_search_policy.exe
```

Expected:

- 新增断言失败，证明策略尚未按最新 scale 数据调整

- [ ] **Step 4: 做最小策略调整，不动 CLI**

要求：

- 只修改 desktop + sidecar 的默认执行计划
- 不改 `BuildCandidates()` 拓扑候选生成器
- 不改 CLI warmup 路径

- [ ] **Step 5: 跑 worker ramp / scale / cancel / build 全量验证**

Run:

```powershell
cmake --build out/build/mingw-debug --target test_desktop_search_policy oni-sidecar
out\build\mingw-debug\src\test_desktop_search_policy.exe
powershell -ExecutionPolicy Bypass -File scripts/smoke/run-sidecar-search-worker-ramp.ps1 -SidecarPath out/build/mingw-debug/src/oni-sidecar.exe
powershell -ExecutionPolicy Bypass -File scripts/smoke/run-sidecar-search-scale.ps1 -SidecarPath out/build/mingw-debug/src/oni-sidecar.exe
node --test --experimental-strip-types desktop/tests/searchCancelState.test.ts
corepack yarn --cwd desktop build
```

Expected:

- 所有验证通过
- 不出现 started 延迟回升到明显可感知级别
- 不出现取消回退或 seeds/s 状态回退

## Chunk 5: Documentation Sync And Final Verification

### Task 8: 同步 llmdoc，并把最终验证清单跑完

**Files:**
- Modify: `llmdoc/overview/project.md`
- Modify: `llmdoc/decisions/2026-04-09-tauri-desktop-refactor-plan.md`
- Modify: `llmdoc/decisions/2026-04-19-search-initial-worker-bounded-recovery-plan.md`（若 Chunk 4 有实际改动）

- [ ] **Step 1: 重写过时文档段落**

要求：

- 说明 desktop 当前有：
  - 常驻 control sidecar
  - streaming search sidecar
  - desktop 搜索热路径的 worker-reset optimized runtime
  - hidden fallback 开关
- 文档必须明确 CLI 仍保持旧路径
- 不允许用“Update 2026”补丁式追加，必须直接重写成当前事实

- [ ] **Step 2: 跑最终全量验证**

Run:

```powershell
cmake --build out/build/mingw-debug --target oni-sidecar test_settings_cache test_search_analysis test_batch_search_smoke test_desktop_search_policy test_app_runtime_worker_reset
out\build\mingw-debug\src\test_settings_cache.exe
out\build\mingw-debug\src\test_search_analysis.exe
out\build\mingw-debug\src\test_batch_search_smoke.exe
out\build\mingw-debug\src\test_desktop_search_policy.exe
out\build\mingw-debug\src\test_app_runtime_worker_reset.exe
cargo test --manifest-path src-tauri/Cargo.toml sidecar::tests control_sidecar state::tests -- --nocapture
node --test --experimental-strip-types desktop/scripts/searchAnalysisDisplay.test.ts
node --test --experimental-strip-types desktop/tests/searchCancelState.test.ts
corepack yarn --cwd desktop build
powershell -ExecutionPolicy Bypass -File scripts/smoke/run-sidecar-analysis-latency.ps1 -SidecarPath out/build/mingw-debug/src/oni-sidecar.exe
powershell -ExecutionPolicy Bypass -File scripts/smoke/run-sidecar-search-scale.ps1 -SidecarPath out/build/mingw-debug/src/oni-sidecar.exe
powershell -ExecutionPolicy Bypass -File scripts/smoke/run-sidecar-search-worker-ramp.ps1 -SidecarPath out/build/mingw-debug/src/oni-sidecar.exe
```

Expected:

- 所有 native / Rust / Node / smoke 全绿
- warm 分析延迟维持在当前目标区间
- 搜索扩展性结论与 Gate D 当前判定一致；若仍未通过，不再继续进入默认 CPU 策略调优
- 没有任何结果漂移、取消回退、状态展示回退

## Verification Checklist

- [ ] `cmake --build out/build/mingw-debug --target oni-sidecar test_settings_cache test_search_analysis test_batch_search_smoke test_desktop_search_policy test_app_runtime_worker_reset`
- [ ] `out\build\mingw-debug\src\test_settings_cache.exe`
- [ ] `out\build\mingw-debug\src\test_search_analysis.exe`
- [ ] `out\build\mingw-debug\src\test_batch_search_smoke.exe`
- [ ] `out\build\mingw-debug\src\test_desktop_search_policy.exe`
- [ ] `out\build\mingw-debug\src\test_app_runtime_worker_reset.exe`
- [ ] `cargo test --manifest-path src-tauri/Cargo.toml control_sidecar sidecar::tests state::tests -- --nocapture`
- [ ] `node --test --experimental-strip-types desktop/scripts/searchAnalysisDisplay.test.ts`
- [ ] `node --test --experimental-strip-types desktop/tests/searchCancelState.test.ts`
- [ ] `corepack yarn --cwd desktop build`
- [ ] `powershell -ExecutionPolicy Bypass -File scripts/smoke/run-sidecar-analysis-latency.ps1 -SidecarPath out/build/mingw-debug/src/oni-sidecar.exe`
- [ ] `powershell -ExecutionPolicy Bypass -File scripts/smoke/run-sidecar-search-scale.ps1 -SidecarPath out/build/mingw-debug/src/oni-sidecar.exe`
- [ ] `powershell -ExecutionPolicy Bypass -File scripts/smoke/run-sidecar-search-worker-ramp.ps1 -SidecarPath out/build/mingw-debug/src/oni-sidecar.exe`

## Out Of Scope

- 不修改 CLI 的 warmup、analysis、search runtime 行为
- 不修改 `WorldGen` 算法本体的数学/拓扑逻辑
- 不做 seed 结果缓存、近似搜索、概率性跳过
- 不在前端暴露新的性能配置项
- 不提前承诺“必须用更多 worker”；CPU 策略只在热路径扩展性恢复后再评估
- 不自动提交；提交由 `wgh` 手动执行
