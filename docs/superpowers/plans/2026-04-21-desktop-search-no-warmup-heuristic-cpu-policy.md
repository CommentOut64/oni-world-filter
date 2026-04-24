# Desktop Search No-Warmup Heuristic CPU Policy Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 去掉 desktop + sidecar 交互搜索的同步 warmup 标定，改用静态 CPU 启发式首档，让搜索几乎立即启动，同时把默认 `balanced` 的初始并发提升到更接近高吞吐区间的位置。

**Architecture:** 保留 `BuildCandidates()` 作为唯一的 CPU 拓扑候选生成器，不改 CLI 的 warmup 路径，也不改 `BatchSearchService` 的执行内核。新增一个 desktop/sidecar 专用的静态择档器，负责在 `balanced/turbo` 模式下从候选策略中选出交互搜索默认首档；desktop 侧把 `enableWarmup` 默认改为 `false`，并在会话恢复时做强制归一化，避免旧持久化状态把 warmup 带回来。运行期继续依赖现有 `adaptive down + bounded recovery` 做回退与稳定性兜底。

**Tech Stack:** C++23、Batch/BatchCpu、Tauri sidecar、React 19 + TypeScript、PowerShell smoke script、native tests

---

## File Map

### New Files To Create

- `src/Batch/DesktopSearchPolicy.hpp`
  - 职责：声明 desktop + sidecar 交互搜索专用的静态 CPU 择档入口与最小配置常量。
- `src/Batch/DesktopSearchPolicy.cpp`
  - 职责：在不修改 `BuildCandidates()` 输出顺序的前提下，按候选名称与 worker 数规则选择 desktop 默认首档。
- `tests/native/test_desktop_search_policy.cpp`
  - 职责：覆盖异构 / 同构 / turbo / fallback 场景，确保 desktop 静态择档规则可单测、不会回退到过保守首档。
- `scripts/smoke/run-sidecar-search-start-latency.ps1`
  - 职责：直接驱动 sidecar 搜索请求，测量首个 `started` 事件延迟，证明 desktop 无 warmup 路径不再受 warmup 预算放大。

### Existing Files To Modify

- `src/CMakeLists.txt`
  - 注册新的 desktop CPU 策略源文件与 native test target。
- `src/entry_sidecar.cpp`
  - 保留 CLI 不变，仅修改 sidecar 搜索选档逻辑：desktop 无 warmup 时走静态择档器，不再回落到 `candidates.front()`。
- `desktop/src/state/searchStore.ts`
  - 把 desktop 默认 CPU 配置改为 `enableWarmup: false`。
- `desktop/src/features/search/searchSchema.ts`
  - 把表单提交生成的 `cpu.enableWarmup` 改为 `false`，确保 `balanced/turbo/custom` 在 desktop 交互搜索里都不触发同步 warmup。
- `desktop/src/state/searchStorePersistence.ts`
  - 在会话恢复时强制把旧持久化 draft/request 的 `cpu.enableWarmup` 归一化为 `false`。
- `desktop/tests/searchSchema.test.ts`
  - 增加 `toSearchDraft()` 对 `enableWarmup: false` 的断言。
- `desktop/tests/searchStorePersistence.test.ts`
  - 增加“旧快照里 warmup=true，恢复后被强制改成 false”的断言。
- `desktop/scripts/searchAnalysisDisplay.test.ts`
  - 同步测试夹具里的 CPU 配置，避免文义与 desktop 当前真实默认值漂移。
- `llmdoc/overview/project.md`
  - 同步 desktop/sidecar 当前搜索 CPU 策略：交互搜索无 warmup，CLI 仍保留 warmup。
- `llmdoc/decisions/2026-04-09-tauri-desktop-refactor-plan.md`
  - 同步桌面搜索默认 CPU 策略事实，不再声称 sidecar 交互搜索默认先做 warmup。
- `llmdoc/decisions/2026-04-19-search-initial-worker-bounded-recovery-plan.md`
  - 重写与“sidecar/CLI 共享 warmup”相关的现状描述，避免文档继续声称 desktop 交互搜索也必须先 warmup。

## Constraints

- 只修改 `desktop + sidecar` 的交互搜索路径，CLI 的 warmup 与线程选型逻辑保持不变。
- 不调整 `BuildCandidates()` 的职责和候选生成顺序，避免影响 CLI 与已有 native benchmark。
- 不在 desktop 交互搜索里新增第二套 runtime adaptive up；运行期仍只依赖现有 `adaptive down + bounded recovery`。
- 不新增协议字段，不修改前后端 `SearchCpuConfig` 结构。
- 不自动提交；提交由 `wgh` 手动执行。

## ROI Gate

这次选择 **B. 局部重构**。触发证据是：sidecar 当前把“是否 warmup”和“无 warmup 时如何选默认策略”混在同一个 `SelectPolicy()` 分支里，而 desktop 要求只改交互搜索、CLI 不动，如果继续在 `entry_sidecar.cpp` 里硬编码字符串判断，会让测试和后续调参都变脆。重构范围限制在“新增 desktop 专用静态择档器 + sidecar 接线”，不扩散到 CLI 和 `BuildCandidates()`。

## Current Root Cause Summary

- desktop 搜索按钮提交后，`SearchPanel.tsx` 会先执行 `analyze_search_request`，但这一步实测仅约 `0.58s ~ 0.82s`，不是主阻塞。
- 主阻塞发生在 sidecar `RunSearchCommand()` 调用 `SelectPolicy(cfg)` 期间；`started` 事件必须等 warmup 完整结束后才会发出。
- 直接 sidecar 对照实测：
  - `enableWarmup = true` 时，首个 `started` 事件约 `15.9s / 24.4s / 28.0s`
  - `enableWarmup = false` 时，首个 `started` 事件约 `4.7ms / 5.1ms / 4.9ms`
- 当前 desktop 默认 `balanced` 仍把 `enableWarmup` 设为 `true`，而 sidecar 无 warmup 分支只会取 `candidates.front()`，这对 `turbo` 和“更激进但不过载”的 desktop 默认首档都不够好。

## Target Policy Rules

### Desktop `balanced`

- 异构 CPU：
  - 若存在 `balanced-p-core-plus-smt-partial`，且 `balanced-p-core` 的 worker 数 `>= 4`，默认选择 `balanced-p-core-plus-smt-partial`
  - 否则退回 `balanced-p-core`
- 同构 CPU：
  - 若存在 `balanced-physical-plus-smt-partial`，且 `balanced-physical` 的 worker 数 `>= 4`，默认选择 `balanced-physical-plus-smt-partial`
  - 否则退回 `balanced-physical`
- 不默认选择：
  - `balanced-p-core-plus-low-core`
  - `balanced-p-core-plus-smt`
  - `balanced-physical-plus-smt`

### Desktop `turbo`

- 优先选择显式 turbo 候选：
  - `turbo-all-candidates`
  - `turbo-all-logical`
- 若不存在显式 turbo 候选，则退回 worker 数最多的候选策略。

### Desktop `custom`

- 继续沿用现有单候选路径，不新增额外策略分支。

## Success Criteria

- desktop 交互搜索不再因为 warmup 在分析完成后额外阻塞多秒；sidecar 直连 smoke 下，首个 `started` 事件目标值 `< 100ms`，且不再随 `warmupTotalMs` 增长。
- `balanced` 默认首档不再是最保守首档，而是符合上面规则的“中高档启发式候选”。
- `turbo` 在 desktop 无 warmup 路径下仍能选到 turbo 档，而不是错误回落到 `candidates.front()`。
- `adaptive down` 与 `bounded recovery` 现有测试继续通过。
- CLI 行为与文档保持不变；只有 desktop/sidecar 交互搜索改变。

## Residual Risk To Document

- 静态启发式不能像 warmup 一样为每台机器实测最优点，因此少数机型上默认首档可能仍偏高或偏低。
- 本方案接受该风险，因为 desktop 首要问题是启动阻塞；默认首档不足时，后续可继续微调静态规则或引入异步机器级缓存，但不恢复同步 warmup。

## Chunk 1: Desktop Static Policy Selector

### Task 1: 抽出 desktop + sidecar 交互搜索专用静态择档器

**Files:**
- Create: `src/Batch/DesktopSearchPolicy.hpp`
- Create: `src/Batch/DesktopSearchPolicy.cpp`
- Create: `tests/native/test_desktop_search_policy.cpp`
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: 写失败测试，固定 desktop 静态择档规则**

测试至少覆盖：

- 异构 CPU：
  - `balanced-p-core` 有 4+ worker，且存在 `balanced-p-core-plus-smt-partial` 时，默认应选 partial SMT
  - `balanced-p-core` worker 小于 4 时，默认应回退到 `balanced-p-core`
- 同构 CPU：
  - `balanced-physical` 有 4+ worker，且存在 `balanced-physical-plus-smt-partial` 时，默认应选 partial SMT
  - `balanced-physical` worker 小于 4 时，默认应回退到 `balanced-physical`
- `turbo` 模式优先选 `turbo-all-candidates` 或 `turbo-all-logical`
- 候选命名缺失时，fallback 到 worker 数最多的候选，而不是崩溃或返回空策略

- [ ] **Step 2: 运行新增 native 测试，确认红灯**

Run:

```powershell
cmake --build out/build/mingw-debug --target test_desktop_search_policy
```

Expected:

- 编译失败，提示 `DesktopSearchPolicy` 相关符号不存在

- [ ] **Step 3: 实现 `DesktopSearchPolicy`**

实现要求：

- 入口函数建议形如：
  - `BatchCpu::ThreadPolicy SelectDesktopSearchPolicy(const Batch::ThreadPolicyRequest&, const Batch::CpuTopology&, const std::vector<BatchCpu::ThreadPolicy>& candidates);`
- 只读输入，不改写 `candidates`
- 选择逻辑优先依赖候选名称与 worker 数，不重新生成候选
- `balanced` 规则严格按本计划的 worker 阈值与候选优先级实现
- `turbo` 优先显式 turbo 候选；无显式 turbo 时回退到 worker 数最多的候选
- 对空候选保持安全 fallback，返回默认构造策略

- [ ] **Step 4: 把新文件接入 CMake**

要求：

- `oniWorldApp` / `oni-sidecar` 可链接新的 `DesktopSearchPolicy.cpp`
- 注册 `test_desktop_search_policy` target
- 只把测试所需最小源文件加进新 target，避免不必要链接漂移

- [ ] **Step 5: 重新构建并运行测试，确认绿灯**

Run:

```powershell
cmake --build out/build/mingw-debug --target test_desktop_search_policy
out\build\mingw-debug\src\test_desktop_search_policy.exe
```

Expected:

- 编译通过
- 输出 `[PASS] test_desktop_search_policy`

## Chunk 2: Sidecar No-Warmup Path

### Task 2: 让 sidecar 交互搜索在无 warmup 时走静态择档器

**Files:**
- Modify: `src/entry_sidecar.cpp`
- Reuse: `src/Batch/DesktopSearchPolicy.hpp`

- [ ] **Step 1: 先写失败断言或最小 smoke，固定 sidecar 接线目标**

建议先写/整理一个直接 sidecar smoke，用固定 JSON 请求覆盖：

- `balanced + enableWarmup=false`
- `turbo + enableWarmup=false`

至少需要确认：

- 首个 `started` 事件能在非常短时间内返回
- `turbo` 模式不会错误地回落到最保守首档

- [ ] **Step 2: 修改 `SelectPolicy(const FilterConfig&)` 分支结构**

实现要求：

- 保留现有：
  - `DetectCpuTopology()`
  - `BuildThreadPolicyRequestFromFilter()`
  - `BuildPlannerInput()`
  - `BuildThreadPolicyCandidates()`
- 新增分支：
  - 当 `cfg.hasCpuSection && !cfg.cpu.enableWarmup` 时
  - 不再直接 `return candidates.front()`
  - 改为调用 `SelectDesktopSearchPolicy(...)`
- 继续保留：
  - `legacyThreadsOnly`
  - `custom`
  - `conservative`
  - 单候选场景
  - enable warmup 时的现有分段 warmup 路径

- [ ] **Step 3: 保证 `turbo/custom` 语义不被误伤**

要求：

- `turbo` 在 sidecar 无 warmup 路径下必须仍能选到 turbo 候选
- `custom` 仍保持按用户 workers/allowSmt/allowLowPerf 得出的单候选
- 不修改 CLI 的任何入口或共享 warmup 规划器

- [ ] **Step 4: 构建 sidecar 并跑现有 CPU/搜索测试**

Run:

```powershell
cmake --build out/build/mingw-debug --target oni-sidecar test_cpu_topology test_adaptive_concurrency test_batch_search_smoke
out\build\mingw-debug\src\test_cpu_topology.exe
out\build\mingw-debug\src\test_adaptive_concurrency.exe
out\build\mingw-debug\src\test_batch_search_smoke.exe
```

Expected:

- `oni-sidecar` 构建通过
- 现有 CPU 拓扑与运行期并发回退测试继续 PASS

## Chunk 3: Desktop Defaults And Session Sanitization

### Task 3: 把 desktop 默认 CPU 配置切到无 warmup，并清洗旧会话

**Files:**
- Modify: `desktop/src/state/searchStore.ts`
- Modify: `desktop/src/features/search/searchSchema.ts`
- Modify: `desktop/src/state/searchStorePersistence.ts`
- Modify: `desktop/tests/searchSchema.test.ts`
- Modify: `desktop/tests/searchStorePersistence.test.ts`
- Modify: `desktop/scripts/searchAnalysisDisplay.test.ts`

- [ ] **Step 1: 为 `toSearchDraft()` 写失败测试**

新增断言至少覆盖：

- `cpuMode = balanced` 时，生成的 draft `cpu.enableWarmup === false`
- `cpuMode = turbo` 时，生成的 draft `cpu.enableWarmup === false`
- `cpuMode = custom` 时，生成的 draft `cpu.enableWarmup === false`

- [ ] **Step 2: 为会话恢复归一化写失败测试**

新增断言至少覆盖：

- 持久化快照里 `draft.cpu.enableWarmup = true` 时，恢复后被强制改为 `false`
- `lastSubmittedRequest.cpu.enableWarmup = true` 时，恢复后同样被强制改为 `false`
- 其他 CPU 字段保持不变

- [ ] **Step 3: 修改 desktop 默认值与表单序列化**

实现要求：

- `DEFAULT_CPU_CONFIG.enableWarmup = false`
- `toSearchDraft()` 里构造的 `cpu.enableWarmup = false`
- 不修改 `enableAdaptiveDown`
- 不引入新的前端表单开关；desktop UI 仍保持当前 `balanced / turbo / custom`

- [ ] **Step 4: 在 `searchStorePersistence.ts` 增加恢复期清洗**

建议最小实现：

- 增加一个小的 `normalizeDesktopSearchCpuConfig()` helper
- 对 `draft.cpu` 与 `lastSubmittedRequest.cpu` 都做同样清洗
- 只强制 `enableWarmup=false`，不顺手改动其他字段

- [ ] **Step 5: 同步测试夹具**

要求：

- `searchStorePersistence.test.ts` 的 `SAMPLE_DRAFT`
- `searchAnalysisDisplay.test.ts` 的 `draft`

都改成与 desktop 当前真实默认值一致，避免夹具继续暗示 warmup 默认为 `true`

- [ ] **Step 6: 运行前端测试与构建**

Run:

```powershell
node --test --experimental-strip-types desktop/tests/searchSchema.test.ts
node --test --experimental-strip-types desktop/tests/searchStorePersistence.test.ts
node --test --experimental-strip-types desktop/scripts/searchAnalysisDisplay.test.ts
yarn --cwd desktop build
```

Expected:

- 三组测试全部通过
- `desktop` 构建通过

## Chunk 4: Smoke Verification And Docs Sync

### Task 4: 增加启动时延 smoke，并同步 llmdoc

**Files:**
- Create: `scripts/smoke/run-sidecar-search-start-latency.ps1`
- Modify: `llmdoc/overview/project.md`
- Modify: `llmdoc/decisions/2026-04-09-tauri-desktop-refactor-plan.md`
- Modify: `llmdoc/decisions/2026-04-19-search-initial-worker-bounded-recovery-plan.md`

- [ ] **Step 1: 写 sidecar 启动时延 smoke 脚本**

脚本至少支持：

- 向 `oni-sidecar.exe` 发送单个 `search` JSON 请求
- 测量“发出请求 -> 收到首个 `started` 事件”的毫秒数
- 覆盖：
  - `balanced + enableWarmup=false`
  - `turbo + enableWarmup=false`
- 输出结构化结果，方便人工比较是否仍存在多秒级阻塞

- [ ] **Step 2: 跑 smoke 验证“无 warmup 即时启动”**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/smoke/run-sidecar-search-start-latency.ps1
```

Expected:

- `balanced` 与 `turbo` 两个请求都能快速收到 `started`
- 不再出现十几秒到几十秒级的 warmup 等待

- [ ] **Step 3: 同步 llmdoc 当前事实**

文档必须反映：

- desktop + sidecar 交互搜索不再默认同步 warmup
- CLI 仍保留 warmup 选型与 bounded recovery 设计
- desktop `balanced` 改为静态启发式首档，不再等同于最保守首档
- 运行期仍保留 `adaptive down + bounded recovery`

- [ ] **Step 4: 做一次完整验证收口**

Run:

```powershell
cmake --build out/build/mingw-debug --target oni-sidecar test_desktop_search_policy test_cpu_topology test_adaptive_concurrency test_batch_search_smoke
out\build\mingw-debug\src\test_desktop_search_policy.exe
out\build\mingw-debug\src\test_cpu_topology.exe
out\build\mingw-debug\src\test_adaptive_concurrency.exe
out\build\mingw-debug\src\test_batch_search_smoke.exe
node --test --experimental-strip-types desktop/tests/searchSchema.test.ts
node --test --experimental-strip-types desktop/tests/searchStorePersistence.test.ts
node --test --experimental-strip-types desktop/scripts/searchAnalysisDisplay.test.ts
yarn --cwd desktop build
powershell -ExecutionPolicy Bypass -File scripts/smoke/run-sidecar-search-start-latency.ps1
```

Expected:

- native tests 全部 PASS
- desktop 测试与构建通过
- smoke 证明启动延迟不再受 warmup 预算支配

## Done Criteria

- desktop 交互搜索默认不再触发同步 warmup
- sidecar 无 warmup 路径具备独立的静态择档器，不再简单回落到 `candidates.front()`
- `balanced` 默认首档提升到 partial SMT 中高档，但仍保留 runtime 自动回退
- `turbo` 在无 warmup 路径下仍保有 turbo 语义
- CLI 保持原样
- `llmdoc` 已同步到当前真实状态
