# Desktop Search Runtime Worker Ramp Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在保留 desktop + sidecar 交互搜索“无 warmup 立即启动”的前提下，让运行中的活跃 worker 能从较低首档自动回升到更高 CPU ceiling，从而在非 warmup 模式下更充分吃满 CPU。

**Architecture:** 保留 `BuildCandidates()` 作为唯一的 CPU 拓扑候选生成器，也保留现有 `AdaptiveConcurrencyController + BoundedRecoveryController` 作为执行内核，不为 desktop 另起一套全新的在线调度器。核心改动是把“总线程上限”与“启动时活跃 worker 数”从同一个 `workerCount` 里拆开：desktop/sidecar 无 warmup 路径先选一个较高的 ceiling policy，再以更低的 `initialActiveWorkers` 立即开跑，随后复用现有 recovery 逻辑逐步回升到 ceiling；CLI 保持现状不变。

**Tech Stack:** C++23、Batch/BatchCpu、Tauri sidecar、PowerShell smoke script、native tests

---

## File Map

### New Files To Create

- `scripts/smoke/run-sidecar-search-worker-ramp.ps1`
  - 职责：直接驱动 sidecar 搜索请求，采集 `progress` 事件里的 `activeWorkers` 变化，验证 desktop 无 warmup 路径能从较低首档回升到更高 ceiling。

### Existing Files To Modify

- `src/Batch/SearchRequest.hpp`
  - 为搜索执行内核新增“启动时活跃 worker 数”字段，且保持旧调用方默认兼容。
- `src/Batch/BatchSearchService.cpp`
  - 把 `adaptiveWorkers` 初值改为 `initialActiveWorkers`，并继续用 `workerCount` 作为总线程 ceiling 与 recovery 上限。
- `src/Batch/DesktopSearchPolicy.hpp`
  - 从“只返回单个静态 policy”扩展为返回 desktop 交互搜索专用的执行计划，包含 ceiling policy、初始活跃 worker 数、是否启用 recovery 及 desktop 专用 recovery 参数。
- `src/Batch/DesktopSearchPolicy.cpp`
  - 实现 `balanced/turbo/custom/conservative` 的 desktop 执行计划选择规则，不改 `BuildCandidates()`。
- `src/entry_sidecar.cpp`
  - 仅对 desktop + sidecar 交互搜索接线：无 warmup 路径构造 desktop 执行计划，把 `initialActiveWorkers`、recovery 开关和 desktop 恢复参数写入 `SearchRequest`。
- `tests/native/test_desktop_search_policy.cpp`
  - 从“验证静态择档”扩展为“验证 ceiling + initialActiveWorkers + recovery enable”的执行计划测试。
- `tests/native/test_batch_search_smoke.cpp`
  - 增加“初始活跃 worker 低于 ceiling 时，运行中能回升到 ceiling”的烟雾测试。
- `tests/native/test_adaptive_concurrency.cpp`
  - 增加 `initialActiveWorkers < workerCount` 场景的边界断言，确保 recovery ceiling 仍受 `workerCount` 限制而不是受初始首档限制。
- `scripts/smoke/run-sidecar-search-start-latency.ps1`
  - 复用或补充输出，确认加入 runtime ramp 后 `started` 延迟仍保持毫秒级。
- `llmdoc/overview/project.md`
  - 同步说明 desktop + sidecar 当前已是“无 warmup + 首档立即启动 + 运行中 bounded recovery 回升 ceiling”，CLI 仍保留原有 warmup。
- `llmdoc/decisions/2026-04-19-search-initial-worker-bounded-recovery-plan.md`
  - 重写当前事实，避免文档继续把 recovery 的上限描述为“当前单一 workerCount 模型下的回升”。

## Constraints

- 只修改 `desktop + sidecar` 的交互搜索路径，CLI 的 warmup 与运行参数保持不变。
- 不修改 `BuildCandidates()` 的职责、候选生成顺序和候选命名规则。
- 不新增 desktop 前端 UI 配置项；若需要更快恢复参数，优先在 sidecar 内部注入 desktop 默认值，而不是扩展前端表单。
- 不改 sidecar/desktop 现有协议结构，除非实现过程中证明 `initialActiveWorkers` 必须暴露到协议层；默认目标是不改协议。
- 不新增另一套复杂的在线 throughput 探测器；先复用现有 `BoundedRecoveryController` 完成最小闭环。
- 不自动提交；提交由 `wgh` 手动执行。

## ROI Gate

这次选择 **B. 局部重构**。触发证据是：当前 `workerCount` 同时承担了“创建多少线程”“当前活跃 worker 起始值”“运行中 recovery 上限”三个职责，导致 desktop 无 warmup 路径虽然能即时启动，但运行中永远没有额外 headroom 可以向上恢复。重构范围限制在 `SearchRequest` 执行参数建模、desktop 执行计划选择与 sidecar 接线，不扩散到 CLI、`BuildCandidates()` 和前端协议。

## Current Behavior Summary

- `BatchSearchService::Run()` 里，`adaptiveWorkers` 直接初始化为 `workerCount`，见 `src/Batch/BatchSearchService.cpp`。
- `currentActiveWorkers()` 最终又会把活跃 worker clamp 到 `workerCount`，因此运行期 ceiling 仍由 `workerCount` 决定。
- `BoundedRecoveryController` 的上调上限是构造时传入的 `initialWorkers`，当前调用处同样传的是 `workerCount`。
- desktop + sidecar 当前请求构造时只开启了 `adaptive down`，没有开启 recovery。
- 这导致 desktop 无 warmup 虽然启动快，但如果首档为了安全而偏保守，运行中没有任何生产路径能自动往更高档位爬升。

## Target Design

### 1. 搜索执行参数拆分

- `SearchRequest.workerCount`
  - 继续表示“总线程数 / runtime ceiling / placement policy 长度”。
- `SearchRequest.initialActiveWorkers`
  - 新增字段；默认 `0` 表示“沿用 workerCount，保持旧行为”。
  - desktop 无 warmup runtime ramp 路径下，显式设为低于 `workerCount` 的起始值。

### 2. `BatchSearchService` 行为

- 计算：
  - `configuredWorkerCount = clamp(request.workerCount, 1, +inf)`
  - `startingActiveWorkers = clamp(request.initialActiveWorkers > 0 ? request.initialActiveWorkers : configuredWorkerCount, 1, configuredWorkerCount)`
- `adaptiveWorkers` 初始化为 `startingActiveWorkers`
- worker 线程总创建数仍为 `configuredWorkerCount`
- `currentActiveWorkers()` 仍以 `configuredWorkerCount` 为 ceiling
- `BoundedRecoveryController` 仍用 `configuredWorkerCount` 作为 recovery 上限
- 这样旧调用方完全不变，而 desktop 新路径天然获得“从较低首档回升到较高 ceiling”的能力

### 3. Desktop 执行计划模型

在 `DesktopSearchPolicy` 中新增执行计划结构，例如：

```cpp
struct DesktopSearchExecutionPlan {
    BatchCpu::ThreadPolicy runtimePolicy;
    uint32_t initialActiveWorkers = 1;
    bool enableRecovery = false;
    BatchCpu::RecoveryConfig recoveryConfig{};
};
```

设计要求：

- `runtimePolicy`
  - 表示最终 ceiling policy，也就是 sidecar 实际创建线程和应用放置策略时使用的 policy
- `initialActiveWorkers`
  - 只决定启动时先放开多少 worker
  - 必须 `<= runtimePolicy.workerCount`
- `enableRecovery`
  - 仅在 `initialActiveWorkers < runtimePolicy.workerCount` 时开启
- `recoveryConfig`
  - 仅用于 desktop + sidecar 无 warmup 路径，不影响 CLI

### 4. Desktop 默认策略规则

#### `balanced`

- 异构 CPU：
  - 若存在 `balanced-p-core-plus-smt`，则：
    - `runtimePolicy = balanced-p-core-plus-smt`
    - 若存在 `balanced-p-core-plus-smt-partial` 且 `balanced-p-core.workerCount >= 4`
      - `initialActiveWorkers = balanced-p-core-plus-smt-partial.workerCount`
    - 否则
      - `initialActiveWorkers = balanced-p-core.workerCount`
  - 若不存在 `balanced-p-core-plus-smt`
    - 回退到现有静态首档逻辑，`initialActiveWorkers = runtimePolicy.workerCount`
- 同构 CPU：
  - 若存在 `balanced-physical-plus-smt`，则：
    - `runtimePolicy = balanced-physical-plus-smt`
    - 若存在 `balanced-physical-plus-smt-partial` 且 `balanced-physical.workerCount >= 4`
      - `initialActiveWorkers = balanced-physical-plus-smt-partial.workerCount`
    - 否则
      - `initialActiveWorkers = balanced-physical.workerCount`
  - 若不存在 `balanced-physical-plus-smt`
    - 回退到现有静态首档逻辑，`initialActiveWorkers = runtimePolicy.workerCount`
- 继续不默认使用：
  - `balanced-p-core-plus-low-core`

解释：

- 这样仍保持 desktop 默认不把低性能核心作为 balanced 首选。
- 但 ceiling 不再停在 partial SMT，而是抬到 full SMT；非 warmup 启动时先用 partial/physical 档立即开跑，再由 recovery 回升到 full SMT。

#### `turbo`

- `runtimePolicy`
  - 优先 `turbo-all-candidates`
  - 其次 `turbo-all-logical`
  - 再次 fallback 到 worker 数最多的候选
- `initialActiveWorkers`
  - 若 `runtimePolicy.workerCount <= 4`，直接等于 ceiling
  - 否则取 `ceil(runtimePolicy.workerCount * 0.75)`
- 这样 turbo 在大核数机器上仍会“先高起步”，但保留约 25% headroom 给 recovery

#### `custom` / `conservative` / 单候选

- 继续沿用当前单档行为：
  - `initialActiveWorkers = runtimePolicy.workerCount`
  - `enableRecovery = false`

### 5. Desktop 专用 recovery 参数

为避免 desktop 默认 `sampleWindowMs=2000` 导致上调过慢，本方案优先在 sidecar 无 warmup 路径下内部注入更快的 desktop recovery 参数：

- `sampleWindow`
  - 若当前请求值大于 `500ms`，则内部收敛到 `500ms`
- `RecoveryConfig.stableWindows = 1`
- `RecoveryConfig.retentionRatio = 0.95`
- `RecoveryConfig.cooldown = 500ms`

说明：

- 这套参数只应用于 desktop 无 warmup runtime ramp 路径。
- 先不扩展前端配置项，避免把内部执行策略暴露成新的 UI 负担。
- 若后续验证表明噪声过大，再单独调整 recovery 参数；本次不同时引入新协议字段。

## Success Criteria

- desktop + sidecar 交互搜索继续保持无 warmup 毫秒级启动；`started` 延迟目标仍 `< 100ms`。
- `balanced` 模式下，运行中 `activeWorkers` 能从 partial/physical 首档回升到 full SMT ceiling。
- `turbo` 模式下，运行中 `activeWorkers` 能从约 75% ceiling 回升到 full ceiling。
- CLI 行为、CLI warmup 路径和 CLI 参数不发生变化。
- 现有 `adaptive down` 与 `bounded recovery` 测试继续通过。

## Residual Risk To Document

- 现有 `BoundedRecoveryController` 仍然是“逐窗 +1”模型，不是更激进的在线爬坡器；在极大核数机器上，回升到 ceiling 仍可能需要多个采样窗口。
- 本方案接受该限制，因为它能在最小改动下先打通“非 warmup 也能向上恢复”的闭环；若后续证明爬升仍偏慢，再单独迭代 recovery 算法本身。

## Chunk 1: Split Startup Workers From Runtime Ceiling

### Task 1: 在搜索执行内核里拆分 `initialActiveWorkers` 与 `workerCount`

**Files:**
- Modify: `src/Batch/SearchRequest.hpp`
- Modify: `src/Batch/BatchSearchService.cpp`
- Test: `tests/native/test_batch_search_smoke.cpp`
- Test: `tests/native/test_adaptive_concurrency.cpp`

- [ ] **Step 1: 先为 `SearchRequest` 默认兼容行为写失败测试**

覆盖至少两个场景：

- 未设置 `initialActiveWorkers` 时，行为与现在完全一致
- 设置 `initialActiveWorkers < workerCount` 时，进度事件里的 `activeWorkers` 初始值应等于 `initialActiveWorkers`

- [ ] **Step 2: 运行定向测试，确认红灯**

Run:

```powershell
cmake --build out/build/mingw-debug --target test_batch_search_smoke test_adaptive_concurrency
out\build\mingw-debug\src\test_batch_search_smoke.exe
out\build\mingw-debug\src\test_adaptive_concurrency.exe
```

Expected:

- 至少一个新增断言失败，表明当前实现仍把起始活跃 worker 绑死在 `workerCount`

- [ ] **Step 3: 在 `SearchRequest` 增加 `initialActiveWorkers` 字段**

要求：

- 默认值设为 `0`
- 注释明确：`0` 表示“沿用 workerCount，保持旧行为”
- 不改现有字段命名和现有调用方签名

- [ ] **Step 4: 修改 `BatchSearchService::Run()` 初始化逻辑**

实现要求：

- `workerCount` 继续表示总线程数与 runtime ceiling
- 新增 `startingActiveWorkers` 计算
- `adaptiveWorkers` 用 `startingActiveWorkers` 初始化
- `currentActiveWorkers()` 继续 clamp 到 `workerCount`
- `BoundedRecoveryController` 继续以 `workerCount` 作为 recovery ceiling

- [ ] **Step 5: 补充并运行单测，确认绿灯**

最少新增断言：

- `initialActiveWorkers=0` 时，`started/progress/finalActiveWorkers` 与旧行为一致
- `initialActiveWorkers=4, workerCount=8, enableRecovery=false` 时，活跃 worker 不会超过 4
- `initialActiveWorkers=4, workerCount=8, enableRecovery=true` 时，活跃 worker 最终可回升到 8

Run:

```powershell
cmake --build out/build/mingw-debug --target test_batch_search_smoke test_adaptive_concurrency
out\build\mingw-debug\src\test_batch_search_smoke.exe
out\build\mingw-debug\src\test_adaptive_concurrency.exe
```

Expected:

- 两个测试程序均输出 `[PASS]`

## Chunk 2: Desktop Execution Plan And Sidecar Wiring

### Task 2: 从单一静态 policy 升级为 desktop 执行计划

**Files:**
- Modify: `src/Batch/DesktopSearchPolicy.hpp`
- Modify: `src/Batch/DesktopSearchPolicy.cpp`
- Modify: `tests/native/test_desktop_search_policy.cpp`
- Modify: `src/CMakeLists.txt`（仅当测试 target 需新增依赖时）

- [ ] **Step 1: 先写失败测试，固定 desktop 执行计划规则**

至少覆盖：

- 异构 `balanced`
  - 有 `balanced-p-core-plus-smt`、`balanced-p-core-plus-smt-partial`、`balanced-p-core`
  - 断言 `runtimePolicy` 选 full SMT
  - 断言 `initialActiveWorkers` 选 partial SMT 或 p-core
- 同构 `balanced`
  - 同样断言 full SMT 为 ceiling，partial/physical 为 startup
- `turbo`
  - 断言 `runtimePolicy` 选 turbo 候选
  - 断言 `initialActiveWorkers` 为 ceiling 的 75% 规则
- `custom/conservative/单候选`
  - 断言 `initialActiveWorkers == runtimePolicy.workerCount`
  - 断言 `enableRecovery == false`

- [ ] **Step 2: 运行新增测试，确认红灯**

Run:

```powershell
cmake --build out/build/mingw-debug --target test_desktop_search_policy
out\build\mingw-debug\src\test_desktop_search_policy.exe
```

Expected:

- 新增执行计划断言失败，证明当前实现只会返回单个静态 policy

- [ ] **Step 3: 在 `DesktopSearchPolicy` 中引入执行计划结构与选择逻辑**

实现要求：

- 保留现有静态择档能力，不改 `BuildCandidates()`
- 新增 desktop 执行计划入口，建议形如：

```cpp
DesktopSearchExecutionPlan BuildDesktopSearchExecutionPlan(
    const ThreadPolicyRequest &request,
    const CpuTopology &topology,
    const std::vector<BatchCpu::ThreadPolicy> &candidates);
```

- 输入只读，不改写 `candidates`
- `balanced` / `turbo` / `custom` / `conservative` 规则严格按本计划执行
- 对空候选保持安全 fallback

- [ ] **Step 4: 让测试重新转绿**

Run:

```powershell
cmake --build out/build/mingw-debug --target test_desktop_search_policy
out\build\mingw-debug\src\test_desktop_search_policy.exe
```

Expected:

- 输出 `[PASS] test_desktop_search_policy`

### Task 3: 仅对 sidecar 无 warmup 路径接入 runtime worker ramp

**Files:**
- Modify: `src/entry_sidecar.cpp`
- Reuse: `src/Batch/DesktopSearchPolicy.hpp`

- [ ] **Step 1: 先写或整理 sidecar 层最小验证点**

至少固定两个目标：

- `balanced + enableWarmup=false` 时，请求构造出的 `SearchRequest` 具备：
  - `workerCount = ceiling policy workerCount`
  - `initialActiveWorkers < workerCount`
  - `enableRecovery = true`
- `turbo + enableWarmup=false` 时，`workerCount` 不会回落到保守首档

- [ ] **Step 2: 修改 `entry_sidecar.cpp` 的无 warmup 分支**

实现要求：

- 保留 CLI 不变
- 保留 warmup 分支不变
- 仅在 `desktop + sidecar + !enableWarmup + 非 custom + 非 conservative + 多候选` 时：
  - 调用 desktop 执行计划
  - 用 `runtimePolicy` 作为实际 `policy`
  - 把 `initialActiveWorkers` 写入 `SearchRequest`
  - 开启 desktop recovery
  - 注入 desktop 专用 recovery 参数
  - 对 `sampleWindow` 做 desktop 内部收敛

- [ ] **Step 3: 确认旧路径不受影响**

要求：

- `legacyThreadsOnly` 继续保持旧行为
- `custom` 继续只跑用户配置的单候选
- `conservative` 保持旧行为
- `enableWarmup=true` 的 sidecar 路径完全不变

- [ ] **Step 4: 构建 sidecar 与相关 native test**

Run:

```powershell
cmake --build out/build/mingw-debug --target oni-sidecar test_desktop_search_policy test_batch_search_smoke test_adaptive_concurrency
out\build\mingw-debug\src\test_desktop_search_policy.exe
out\build\mingw-debug\src\test_batch_search_smoke.exe
out\build\mingw-debug\src\test_adaptive_concurrency.exe
```

Expected:

- `oni-sidecar` 构建通过
- 三个测试程序全部 PASS

## Chunk 3: Smoke Verification And Documentation Sync

### Task 4: 用 sidecar smoke 证明“启动仍快 + 运行中会向上恢复”

**Files:**
- Create: `scripts/smoke/run-sidecar-search-worker-ramp.ps1`
- Modify: `scripts/smoke/run-sidecar-search-start-latency.ps1`

- [ ] **Step 1: 新增 worker ramp smoke**

脚本要求：

- 直接启动 `oni-sidecar.exe`
- 发送 `enableWarmup=false` 的搜索请求
- 采集 `started` / `progress` / `completed` 事件
- 输出：
  - 首个 `started` 事件耗时
  - 首次观测到的 `activeWorkers`
  - 最大观测到的 `activeWorkers`
  - 是否出现“从较低首档回升到 ceiling”的过程

- [ ] **Step 2: 跑启动时延 smoke**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/smoke/run-sidecar-search-start-latency.ps1 -SidecarPath out/build/mingw-debug/src/oni-sidecar.exe
```

Expected:

- `balanced` / `turbo` 的 `started` 延迟仍为毫秒级，不因 runtime ramp 回到秒级

- [ ] **Step 3: 跑 worker ramp smoke**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/smoke/run-sidecar-search-worker-ramp.ps1 -SidecarPath out/build/mingw-debug/src/oni-sidecar.exe
```

Expected:

- 至少一个 desktop 无 warmup 场景出现：
  - `initial activeWorkers < max observed activeWorkers`
  - `max observed activeWorkers == workerCount ceiling`

### Task 5: 同步系统文档

**Files:**
- Modify: `llmdoc/overview/project.md`
- Modify: `llmdoc/decisions/2026-04-19-search-initial-worker-bounded-recovery-plan.md`

- [ ] **Step 1: 重写文档中过时的运行期并发描述**

要求：

- 说明 desktop + sidecar 当前是：
  - 无 warmup 即时启动
  - 有独立的 startup active workers
  - recovery 上限由 runtime ceiling 决定
- 明确 CLI 仍沿用原路径

- [ ] **Step 2: 自查文档与代码是否一致**

检查点：

- 文档不再声称 recovery 上限等于“启动时 workerCount”
- 文档不再把 desktop 交互搜索描述成“只有 adaptive down，没有 recovery”
- 文档不再把 sidecar 与 CLI 的运行期并发行为混为一谈

## Verification Checklist

- [ ] `cmake --build out/build/mingw-debug --target oni-sidecar test_desktop_search_policy test_batch_search_smoke test_adaptive_concurrency`
- [ ] `out\build\mingw-debug\src\test_desktop_search_policy.exe`
- [ ] `out\build\mingw-debug\src\test_batch_search_smoke.exe`
- [ ] `out\build\mingw-debug\src\test_adaptive_concurrency.exe`
- [ ] `powershell -ExecutionPolicy Bypass -File scripts/smoke/run-sidecar-search-start-latency.ps1 -SidecarPath out/build/mingw-debug/src/oni-sidecar.exe`
- [ ] `powershell -ExecutionPolicy Bypass -File scripts/smoke/run-sidecar-search-worker-ramp.ps1 -SidecarPath out/build/mingw-debug/src/oni-sidecar.exe`

## Out Of Scope

- 不把 recovery 参数暴露成 desktop 新 UI
- 不修改 CLI 的 warmup、sample window 或 recovery 行为
- 不重写 `BoundedRecoveryController` 为更激进的指数/二分/带回退的在线爬坡器
- 不引入机器级持久化 benchmark cache

