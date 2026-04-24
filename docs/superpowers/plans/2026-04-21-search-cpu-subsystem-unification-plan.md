# Search CPU Subsystem Unification Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 用一套基于“物理核优先 + SMT 容量 + 统一 governor”的共享模型替换当前分裂的搜索 CPU 子系统，并同时覆盖 CLI 与 desktop + sidecar。

**Architecture:** 本方案不再继续叠加 `ThreadPolicy` / `DesktopSearchPolicy` / `initialActiveWorkers` / `adaptive + recovery` 这些局部补丁，而是引入新的共享编译入口：先把 CPU 拓扑探测为物理核事实，再按 `balanced/turbo + allowSmt + allowLowPerf + binding` 编译成静态执行 envelope、绑定计划和统一 governor 配置。运行时只按“物理核”上调下调，worker 数由活跃物理核集合派生。实现过程按“先并行引入新模型、再迁移调用方、最后删除旧路径”的顺序推进，保证每一步都有等价性和稳定性验证。

**Tech Stack:** C++23、Batch/BatchCpu、Windows CPU Set / 线程亲和性 API、Tauri sidecar、React 19 + TypeScript、PowerShell smoke script、native tests

---

## File Map

### New Files To Create

- `src/BatchCpu/SearchCpuPlan.hpp`
  - 职责：声明统一 CPU 子系统的核心模型，包括 `CpuTopologyFacts`、`PhysicalCoreFacts`、`CpuPolicySpec`、`CpuExecutionEnvelope`、`CpuPlacementPlan`、`CompiledSearchCpuPlan`。
- `src/BatchCpu/SearchCpuPlan.cpp`
  - 职责：实现从拓扑事实到统一 CPU 计划的编译逻辑，负责物理核过滤、排序、预留、绝对上限、startup 物理核数和 worker slot 展平。
- `src/BatchCpu/SearchCpuGovernor.hpp`
  - 职责：声明统一 governor 配置、状态和观测接口，明确按“物理核”而不是按裸 worker 调节。
- `src/BatchCpu/SearchCpuGovernor.cpp`
  - 职责：实现统一 governor 的上下调逻辑、cooldown、稳定窗口与吞吐回落判断。
- `tests/native/test_search_cpu_plan.cpp`
  - 职责：以 synthetic topology 固定 `balanced=80%`、`turbo=100%`、`allowSmt`、`allowLowPerf`、无 SMT 退化、多 group 顺序保留等核心语义。
- `tests/native/test_search_cpu_governor.cpp`
  - 职责：验证 governor 以物理核为步长上下调，并在 SMT=2 / 无 SMT 拓扑下派生出正确 worker 增量。

### Existing Files To Modify

- `src/BatchCpu/CpuOptimization.hpp`
  - 收缩为 CPU 拓扑探测与平台绑定的薄封装，移除旧的 `ThreadPolicy` / `PlannerInput` / `AdaptiveConfig` / `RecoveryConfig` 作为主模型的职责。
- `src/BatchCpu/CpuOptimization.cpp`
  - 只保留：Windows CPU 拓扑探测、group-aware 绑定实现、必要的兼容辅助函数；删除候选生成、排序打散和旧 governor 主逻辑。
- `src/Batch/CpuTopology.hpp`
  - 从 `using CpuTopology = BatchCpu::CpuTopology` 迁移为 `using CpuTopologyFacts = BatchCpu::CpuTopologyFacts` 或等价共享入口。
- `src/Batch/CpuTopology.cpp`
  - 提供新的 `DetectCpuTopologyFacts()` 包装函数。
- `src/Batch/ThreadPolicy.hpp`
  - 迁移为兼容层或在最终阶段删除；不再承载核心策略定义。
- `src/Batch/ThreadPolicy.cpp`
  - 在迁移阶段仅保留旧请求到 `CpuPolicySpec` 的兼容映射；最终删除旧候选生成逻辑。
- `src/Batch/SearchRequest.hpp`
  - 用统一的 `CompiledSearchCpuPlan` / `SearchCpuRuntimePlan` 替换 `workerCount`、`initialActiveWorkers`、`AdaptiveConfig`、`RecoveryConfig`。
- `src/Batch/BatchSearchService.cpp`
  - 改为基于统一 CPU 计划创建线程、派生活跃 worker 数、执行严格绑定，并用新 governor 管理活跃物理核数。
- `src/Batch/FilterConfig.hpp`
  - 收缩 CPU 配置为统一最小表面，删除 warmup、旧 adaptive/recovery 参数和误导性的旧模式字段。
- `src/Batch/FilterConfig.cpp`
  - 同步解析与默认值。
- `src/Batch/SidecarProtocol.hpp`
  - 收缩 `SidecarCpuConfig`，去除已确认要删除的旧 CPU 字段。
- `src/Batch/SidecarProtocol.cpp`
  - 同步 JSON 反序列化与默认值。
- `src/entry_sidecar.cpp`
  - 删除 `DesktopSearchPolicy`、warmup、旧 CPU 选档分支，统一改为编译共享 CPU 计划。
- `src/entry_cli.cpp`
  - 删除 warmup 与旧候选打印；统一改为编译共享 CPU 计划，并输出新的 envelope / placement 摘要。
- `src/CMakeLists.txt`
  - 注册新源文件和测试 target，并在最终阶段移除旧 CPU 模块与测试。
- `tests/native/test_cpu_topology.cpp`
  - 从“候选策略存在性”改为验证拓扑探测事实与物理核分组的基本正确性。
- `tests/native/test_batch_search_smoke.cpp`
  - 改为验证统一 CPU 计划接入后的启动/取消/失败/绑定上限行为。
- `tests/native/test_adaptive_concurrency.cpp`
  - 迁移为新 governor 的运行期集成测试，去掉旧 `AdaptiveConcurrencyController` / `BoundedRecoveryController` 假设。
- `desktop/src/lib/contracts.ts`
  - 同步前端 CPU 协议结构。
- `desktop/src/features/search/searchSchema.ts`
  - 删除 warmup / 自定义 worker / 旧调参项，只保留统一 CPU 配置表面。
- `desktop/src/state/searchStore.ts`
  - 同步默认 CPU 配置。
- `desktop/src/state/searchStorePersistence.ts`
  - 清洗旧会话里的已废弃 CPU 字段。
- `desktop/tests/searchStorePersistence.test.ts`
  - 覆盖旧 CPU 草稿归一化。
- `desktop/scripts/searchAnalysisDisplay.test.ts`
  - 同步 CPU 夹具结构。
- `src-tauri/src/sidecar.rs`
  - 收缩 Rust host 里的 `SearchCpuConfig` 结构并同步 sidecar payload 生成。
- `llmdoc/overview/project.md`
  - 重写当前 CPU 子系统事实。
- `llmdoc/reference/filter-config.md`
  - 重写 CPU 配置表面与字段语义。
- `llmdoc/decisions/2026-04-21-desktop-search-stability-first-performance-status.md`
  - 替换为新 CPU 子系统状态描述，删除旧 desktop-only 临时策略事实。

### Existing Files To Delete

- `src/Batch/DesktopSearchPolicy.hpp`
- `src/Batch/DesktopSearchPolicy.cpp`
- `tests/native/test_desktop_search_policy.cpp`
- `src/Batch/ThroughputCalibration.hpp`
- `src/Batch/ThroughputCalibration.cpp`
- `src/Batch/SessionWarmupPlanner.hpp`
- `src/Batch/SessionWarmupPlanner.cpp`
- `tests/native/test_throughput_calibration.cpp`
- `tests/native/test_session_warmup_planner.cpp`

## Constraints

- 搜索结果、命中集合、取消语义、失败语义不能漂移。
- 新系统的所有百分比、预留、上下调必须统一按“物理核”定义，不能再按逻辑线程或旧 worker ceiling 偷换语义。
- `SMT=on` 只表示“已选物理核可使用全部允许逻辑线程”，不允许重新定义为“百分比基数扩大”。
- 无 SMT 硬件必须无害退化，不能报错，也不能要求前端做特判。
- 运行时不能在 CPU governor 内重新解释拓扑，只能消费启动前编译好的 plan。
- 如果严格绑定在当前平台不可用，必须输出明确诊断；不允许静默降级。
- 实施期间可以短暂保留兼容适配层，但最终交付状态不能继续依赖旧 `DesktopSearchPolicy` / `ThreadPolicy` 候选命名 / warmup。
- 不自动提交；提交由 `wgh` 手动执行。

## ROI Gate

这次选择 **B. 子系统级局部重构**。触发证据是：当前 CPU 语义同时分散在 `CpuTopology`、`ThreadPolicy`、`DesktopSearchPolicy`、`SearchRequest`、`AdaptiveConcurrencyController`、`BoundedRecoveryController` 中，继续打补丁只会加深概念漂移。重构范围限制在“搜索 CPU 子系统”及其直接接线，不扩散到世界生成算法或与 CPU 无关的搜索业务逻辑。

## Migration Principles

- 先并行引入新模型，再迁移调用方，最后删除旧实现。
- 每个 chunk 都必须先写失败测试，再最小实现，再跑定向验证。
- 任何阶段一旦出现结果漂移、取消回退或绑定不稳定，立即停在当前 chunk 修正，不得继续推进。
- 外部配置表面可以经历一个短暂兼容阶段，但在最终 chunk 必须删掉已废弃字段。

## Chunk 1: Freeze Unified CPU Semantics

### Task 1: 用 synthetic topology 固定统一语义

**Files:**
- Create: `tests/native/test_search_cpu_plan.cpp`
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: 为统一 CPU 计划写失败测试**

至少覆盖：

- 异构 + `balanced` + `allowLowPerf=false` + `allowSmt=true`
  - 只允许高性能物理核参与
  - 至少预留 `1` 个高性能物理核
  - 绝对上限按 `ceil(eligible * 0.8)` 再与预留约束共同 clamp
  - startup 物理核数等于绝对物理核上限
  - worker slot 数等于已选物理核允许线程数总和
- 异构 + `turbo` + `allowLowPerf=true`
  - 允许高性能核与低性能核共同进入 eligible 集合
  - 绝对上限等于 eligible 物理核数
- 同构 + 无 SMT 拓扑 + `allowSmt=true`
  - 每个物理核仍只贡献 `1` 个线程
- placement plan 必须保留物理核优先顺序，禁止打散成逻辑线程升序

- [ ] **Step 2: 运行新测试，确认红灯**

Run:

```powershell
cmake --build out/build/mingw-debug --target test_search_cpu_plan
```

Expected:

- 编译失败，提示 `SearchCpuPlan` 相关符号不存在

- [ ] **Step 3: 在 CMake 注册新测试 target**

要求：

- 只引入实现新测试所需的最小源文件
- 不把旧 CPU 规划器整包塞进新 target

- [ ] **Step 4: 重新构建并确认仍然红灯**

Run:

```powershell
cmake --build out/build/mingw-debug --target test_search_cpu_plan
```

Expected:

- 成功编译测试 target
- 运行时断言失败，因为新模型尚未实现

## Chunk 2: Introduce Shared CPU Plan Compiler

### Task 2: 引入统一 CPU 计划模型与编译入口

**Files:**
- Create: `src/BatchCpu/SearchCpuPlan.hpp`
- Create: `src/BatchCpu/SearchCpuPlan.cpp`
- Modify: `src/BatchCpu/CpuOptimization.hpp`
- Modify: `src/BatchCpu/CpuOptimization.cpp`
- Test: `tests/native/test_search_cpu_plan.cpp`

- [ ] **Step 1: 在头文件中声明统一模型**

至少声明：

```cpp
struct LogicalThreadFacts;
struct PhysicalCoreFacts;
struct CpuTopologyFacts;
struct CpuPolicySpec;
struct CpuExecutionEnvelope;
struct PlannedCore;
struct WorkerBindingSlot;
struct CpuPlacementPlan;
struct CompiledSearchCpuPlan;

CompiledSearchCpuPlan CompileSearchCpuPlan(const CpuTopologyFacts& topology,
                                           const CpuPolicySpec& spec);
```

- [ ] **Step 2: 在 `SearchCpuPlan.cpp` 中最小实现编译公式**

要求：

- 先按物理核过滤 eligible 集合
- 再按稳定优先顺序排序
- 再计算：
  - `reservedPhysicalCoreCount`
  - `absolutePhysicalCoreCap`
  - `startupPhysicalCoreCount`
  - `absoluteWorkerCap`
- 再展平 `workerSlotsByPriority`
  - 物理核优先
  - 同一物理核内 primary logical 在线程优先序中排首位
  - `allowSmt=false` 时只保留每核首线程

- [ ] **Step 3: 从旧探测实现中提取物理核事实，而不是逻辑线程扁平表**

要求：

- 保留现有 Windows CPU Set 探测逻辑
- 新增到 `CpuTopologyFacts` 的转换
- 不得继续依赖 `UniqueSorted()` 打散逻辑顺序

- [ ] **Step 4: 运行新测试，确认绿灯**

Run:

```powershell
cmake --build out/build/mingw-debug --target test_search_cpu_plan
out\build\mingw-debug\src\test_search_cpu_plan.exe
```

Expected:

- 输出 `[PASS] test_search_cpu_plan`

### Task 3: 把当前基本拓扑测试切到新事实模型

**Files:**
- Modify: `tests/native/test_cpu_topology.cpp`
- Modify: `src/Batch/CpuTopology.hpp`
- Modify: `src/Batch/CpuTopology.cpp`

- [ ] **Step 1: 为新探测包装层补失败断言**

至少覆盖：

- `DetectCpuTopologyFacts()` 返回非空 diagnostics
- `physicalCoresBySystemOrder.size() >= 1`
- 每个 `PhysicalCoreFacts` 至少有 `1` 个逻辑线程

- [ ] **Step 2: 运行 `test_cpu_topology`，确认红灯**

Run:

```powershell
cmake --build out/build/mingw-debug --target test_cpu_topology
out\build\mingw-debug\src\test_cpu_topology.exe
```

Expected:

- 至少一个断言失败，因为旧测试仍绑定旧结构

- [ ] **Step 3: 迁移包装层与测试**

要求：

- `Batch::DetectCpuTopology()` 迁移或新增为返回新事实模型的入口
- 旧 `using CpuTopology = ...` 不再作为长期主入口

- [ ] **Step 4: 重新运行测试，确认绿灯**

Run:

```powershell
cmake --build out/build/mingw-debug --target test_cpu_topology
out\build\mingw-debug\src\test_cpu_topology.exe
```

Expected:

- 输出 `[PASS] test_cpu_topology`

## Chunk 3: Replace Old Placement Semantics

### Task 4: 用有序 worker slot 替换旧 `ThreadPolicy` 绑定语义

**Files:**
- Modify: `src/BatchCpu/CpuOptimization.hpp`
- Modify: `src/BatchCpu/CpuOptimization.cpp`
- Modify: `src/BatchCpu/SearchCpuPlan.hpp`
- Modify: `src/BatchCpu/SearchCpuPlan.cpp`
- Test: `tests/native/test_search_cpu_plan.cpp`

- [ ] **Step 1: 为 worker slot 到平台绑定目标的映射写失败测试**

至少覆盖：

- 第 `0..N-1` 个 slot 与 placement plan 中前若干物理核的逻辑线程顺序一致
- 多 group synthetic topology 下，每个 slot 保留 `group + logicalIndex`
- `allowSmt=false` 时，不得为同一物理核生成 sibling slot

- [ ] **Step 2: 运行 `test_search_cpu_plan`，确认红灯**

Run:

```powershell
out\build\mingw-debug\src\test_search_cpu_plan.exe
```

Expected:

- 新增顺序或 group 断言失败

- [ ] **Step 3: 实现 group-aware 绑定辅助**

要求：

- 新增或改造平台绑定接口，使其消费 `WorkerBindingSlot`
- Windows 路径优先使用 group-aware 亲和性 API
- 若 `Strict` 绑定失败，必须返回明确错误信息
- 不再使用“`workerIndex % targetLogicalProcessors.size()` + 逻辑线程排序数组”解释绑定

- [ ] **Step 4: 重新运行测试，确认绿灯**

Run:

```powershell
cmake --build out/build/mingw-debug --target test_search_cpu_plan
out\build\mingw-debug\src\test_search_cpu_plan.exe
```

Expected:

- 输出 `[PASS] test_search_cpu_plan`

## Chunk 4: Introduce Unified Governor And Runtime Plan

### Task 5: 实现按物理核调节的统一 governor

**Files:**
- Create: `src/BatchCpu/SearchCpuGovernor.hpp`
- Create: `src/BatchCpu/SearchCpuGovernor.cpp`
- Create: `tests/native/test_search_cpu_governor.cpp`
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: 为新 governor 写失败测试**

至少覆盖：

- 初始活跃物理核数等于 `startupPhysicalCoreCount`
- 上调每次只增加 `1` 个物理核
- 下调每次只减少 `1` 个物理核
- SMT=2 synthetic topology 上 worker 变化等于 `±2`
- 无 SMT synthetic topology 上 worker 变化等于 `±1`
- cooldown 期间不得连续调整

- [ ] **Step 2: 运行新测试，确认红灯**

Run:

```powershell
cmake --build out/build/mingw-debug --target test_search_cpu_governor
```

Expected:

- 编译失败，提示 `SearchCpuGovernor` 相关符号不存在

- [ ] **Step 3: 实现统一 governor**

建议接口：

```cpp
class SearchCpuGovernor {
public:
    SearchCpuGovernor(const CpuExecutionEnvelope& envelope,
                      const SearchCpuGovernorConfig& config);

    std::optional<uint32_t> Observe(double seedsPerSecond,
                                    uint32_t currentActivePhysicalCores,
                                    std::chrono::steady_clock::time_point now);
};
```

要求：

- 只返回新的活跃物理核数
- 不直接暴露旧 `adaptive` / `recovery` 术语
- 下调和回升共享同一个状态机

- [ ] **Step 4: 重新构建并运行测试，确认绿灯**

Run:

```powershell
cmake --build out/build/mingw-debug --target test_search_cpu_governor
out\build\mingw-debug\src\test_search_cpu_governor.exe
```

Expected:

- 输出 `[PASS] test_search_cpu_governor`

### Task 6: 把 `BatchSearchService` 切到统一 CPU 计划

**Files:**
- Modify: `src/Batch/SearchRequest.hpp`
- Modify: `src/Batch/BatchSearchService.cpp`
- Modify: `tests/native/test_batch_search_smoke.cpp`
- Modify: `tests/native/test_adaptive_concurrency.cpp`

- [ ] **Step 1: 先把执行层集成断言改成新语义**

至少覆盖：

- startup `activeWorkers` 等于 `startupPhysicalCoreCount` 派生的 worker 数
- governor 上调时，`activeWorkers` 只以当前新增物理核贡献的线程数增长
- `allowSmt=false` 场景下，同样的物理核上调只增加 `1 worker/core`
- 外部 cap 或取消不会破坏内部 CPU 状态机

- [ ] **Step 2: 运行 `test_batch_search_smoke` 和 `test_adaptive_concurrency`，确认红灯**

Run:

```powershell
cmake --build out/build/mingw-debug --target test_batch_search_smoke test_adaptive_concurrency
out\build\mingw-debug\src\test_batch_search_smoke.exe
out\build\mingw-debug\src\test_adaptive_concurrency.exe
```

Expected:

- 至少一个新增断言失败，因为执行层还在消费旧字段

- [ ] **Step 3: 在 `SearchRequest` 中接入统一 CPU 计划**

目标结构至少包含：

```cpp
struct SearchRequest {
    int seedStart = 1;
    int seedEnd = 0;
    BatchCpu::CompiledSearchCpuPlan cpuPlan{};
    BatchCpu::SearchCpuGovernorConfig cpuGovernorConfig{};
    std::atomic<bool>* cancelRequested = nullptr;
    std::atomic<int>* activeWorkerCap = nullptr;
    SearchWorkerInitializer initializeWorker;
    SearchThreadPlacementApplier applyThreadPlacement;
    SearchSeedEvaluator evaluateSeed;
};
```

要求：

- 删除 `workerCount`
- 删除 `initialActiveWorkers`
- 删除旧 `enableAdaptive` / `AdaptiveConfig`
- 删除旧 `enableRecovery` / `RecoveryConfig`
- `activeWorkerCap` 若继续保留，必须明确映射为“最大活跃物理核数”或与当前 worker slot 语义一致的限制

- [ ] **Step 4: 重写 `BatchSearchService::Run()` 的 CPU 运行时部分**

要求：

- 线程总创建数等于 `cpuPlan.envelope.absoluteWorkerCap`
- 当前活跃 worker 数由“当前活跃物理核数”派生
- 绑定时按 `workerSlotsByPriority[workerIndex]` 解释，不再用旧策略数组
- governor 只管理活跃物理核数

- [ ] **Step 5: 重新运行两组测试，确认绿灯**

Run:

```powershell
cmake --build out/build/mingw-debug --target test_batch_search_smoke test_adaptive_concurrency
out\build\mingw-debug\src\test_batch_search_smoke.exe
out\build\mingw-debug\src\test_adaptive_concurrency.exe
```

Expected:

- 两个测试程序均输出 `[PASS]`

## Chunk 5: Migrate CLI, Sidecar And Frontend Surface

### Task 7: 用统一 CPU 计划替换 CLI 和 sidecar 旧选档路径

**Files:**
- Modify: `src/Batch/ThreadPolicy.hpp`
- Modify: `src/Batch/ThreadPolicy.cpp`
- Modify: `src/entry_sidecar.cpp`
- Modify: `src/entry_cli.cpp`
- Delete: `src/Batch/DesktopSearchPolicy.hpp`
- Delete: `src/Batch/DesktopSearchPolicy.cpp`
- Delete: `tests/native/test_desktop_search_policy.cpp`

- [ ] **Step 1: 为 sidecar / CLI 接线写失败测试或 smoke 断言**

至少覆盖：

- sidecar `balanced` 不再经过 `DesktopSearchPolicy`
- CLI 不再经过 warmup / 候选字符串选档
- CLI 输出的 CPU 摘要来自统一 envelope 和 placement plan

- [ ] **Step 2: 运行相关构建或 smoke，确认红灯**

Run:

```powershell
cmake --build out/build/mingw-debug --target oni-sidecar oniWorldApp
```

Expected:

- 编译失败或现有 smoke 断言失败，因为入口还依赖旧 CPU 结构

- [ ] **Step 3: 引入统一 CPU 计划编译入口**

要求：

- 旧 `ThreadPolicyRequest` 不再作为长期主模型
- 若兼容阶段仍需旧字段，必须只保留到本 chunk 结束
- sidecar 与 CLI 都通过同一个函数编译 CPU 计划

- [ ] **Step 4: 删除 desktop 专属 CPU 解释层**

要求：

- 删除 `DesktopSearchPolicy`
- 删除无 warmup / warmup 两套 CPU 选档分支
- 删除 CLI warmup、throughput calibration、session warmup planner 依赖

- [ ] **Step 5: 重新构建主要二进制并做基础 smoke**

Run:

```powershell
cmake --build out/build/mingw-debug --target oni-sidecar oniWorldApp test_cpu_topology test_search_cpu_plan test_search_cpu_governor test_batch_search_smoke test_adaptive_concurrency
out\build\mingw-debug\src\test_cpu_topology.exe
out\build\mingw-debug\src\test_search_cpu_plan.exe
out\build\mingw-debug\src\test_search_cpu_governor.exe
out\build\mingw-debug\src\test_batch_search_smoke.exe
out\build\mingw-debug\src\test_adaptive_concurrency.exe
```

Expected:

- 所有测试输出 `[PASS]`

### Task 8: 收缩前后端 CPU 配置表面

**Files:**
- Modify: `src/Batch/FilterConfig.hpp`
- Modify: `src/Batch/FilterConfig.cpp`
- Modify: `src/Batch/SidecarProtocol.hpp`
- Modify: `src/Batch/SidecarProtocol.cpp`
- Modify: `src-tauri/src/sidecar.rs`
- Modify: `desktop/src/lib/contracts.ts`
- Modify: `desktop/src/features/search/searchSchema.ts`
- Modify: `desktop/src/state/searchStore.ts`
- Modify: `desktop/src/state/searchStorePersistence.ts`
- Modify: `desktop/tests/searchStorePersistence.test.ts`
- Modify: `desktop/scripts/searchAnalysisDisplay.test.ts`

- [ ] **Step 1: 先把前后端夹具和 schema 改成新 CPU 表面**

最终只保留最小字段：

- `mode`
- `allowSmt`
- `allowLowPerf`
- `binding` 或等价命名

- [ ] **Step 2: 运行前端相关测试，确认红灯**

Run:

```powershell
node --test --experimental-strip-types desktop\tests\searchStorePersistence.test.ts
node --test --experimental-strip-types desktop\scripts\searchAnalysisDisplay.test.ts
```

Expected:

- 至少一个测试因旧 CPU 字段仍存在而失败

- [ ] **Step 3: 同步 sidecar / FilterConfig / Rust host 解析**

要求：

- 删除 warmup 字段
- 删除旧 adaptive / recovery 调参字段
- 删除 `workers`
- 删除 `custom` / `conservative` 作为公开模式
- 若需要兼容旧持久化或旧协议，兼容逻辑必须只做输入归一化，不能继续把旧字段传入核心 CPU 子系统

- [ ] **Step 4: 重新运行前端测试并做 desktop build**

Run:

```powershell
node --test --experimental-strip-types desktop\tests\searchStorePersistence.test.ts
node --test --experimental-strip-types desktop\scripts\searchAnalysisDisplay.test.ts
corepack yarn --cwd desktop build
```

Expected:

- 两组测试通过
- desktop build 成功

## Chunk 6: Delete Legacy Modules And Rewrite Docs

### Task 9: 删除旧 CPU 子系统模块并清理 CMake

**Files:**
- Modify: `src/CMakeLists.txt`
- Delete: `src/Batch/ThroughputCalibration.hpp`
- Delete: `src/Batch/ThroughputCalibration.cpp`
- Delete: `src/Batch/SessionWarmupPlanner.hpp`
- Delete: `src/Batch/SessionWarmupPlanner.cpp`
- Delete: `tests/native/test_throughput_calibration.cpp`
- Delete: `tests/native/test_session_warmup_planner.cpp`

- [ ] **Step 1: 先清理构建系统引用**

要求：

- 移除旧源文件和测试 target
- 确保没有残余目标继续依赖旧模块

- [ ] **Step 2: 重新构建主要二进制与测试，确认不会再引用旧符号**

Run:

```powershell
cmake --build out/build/mingw-debug --target oni-sidecar oniWorldApp test_cpu_topology test_search_cpu_plan test_search_cpu_governor test_batch_search_smoke test_adaptive_concurrency
```

Expected:

- 构建通过，且不再链接旧 warmup / calibration 模块

- [ ] **Step 3: 删除旧文件并再次构建**

Run:

```powershell
cmake --build out/build/mingw-debug --target oni-sidecar oniWorldApp test_cpu_topology test_search_cpu_plan test_search_cpu_governor test_batch_search_smoke test_adaptive_concurrency
```

Expected:

- 构建仍通过

### Task 10: 重写 llmdoc 当前 CPU 事实

**Files:**
- Modify: `llmdoc/overview/project.md`
- Modify: `llmdoc/reference/filter-config.md`
- Modify: `llmdoc/decisions/2026-04-21-desktop-search-stability-first-performance-status.md`
- Modify: `llmdoc/index.md`（仅当索引需要补新文档入口时）

- [ ] **Step 1: 先列出现有文档中的旧概念**

至少清理：

- `ThreadPolicy` 候选名驱动搜索 CPU 语义
- desktop 专属 `DesktopSearchPolicy`
- warmup / calibration / initialActiveWorkers / bounded recovery 的旧描述
- `customAllowSmt/customAllowLowPerf` 这种误导性命名

- [ ] **Step 2: 按新统一语义重写文档**

必须明确写出：

- `balanced=80%` 物理核
- `turbo=100%` 物理核
- `SMT` 只影响每核线程容量
- 启动阶段直接打开已选物理核的全部允许线程
- governor 按物理核上下调
- 无 SMT 硬件无害退化

- [ ] **Step 3: 执行最终回归验证**

Run:

```powershell
cmake --build out/build/mingw-debug --target oni-sidecar oniWorldApp test_cpu_topology test_search_cpu_plan test_search_cpu_governor test_batch_search_smoke test_adaptive_concurrency
out\build\mingw-debug\src\test_cpu_topology.exe
out\build\mingw-debug\src\test_search_cpu_plan.exe
out\build\mingw-debug\src\test_search_cpu_governor.exe
out\build\mingw-debug\src\test_batch_search_smoke.exe
out\build\mingw-debug\src\test_adaptive_concurrency.exe
node --test --experimental-strip-types desktop\tests\searchStorePersistence.test.ts
node --test --experimental-strip-types desktop\scripts\searchAnalysisDisplay.test.ts
corepack yarn --cwd desktop build
```

Expected:

- 所有 native 测试输出 `[PASS]`
- 两组前端测试通过
- desktop build 成功

## Execution Notes

- 优先顺序必须是：`Chunk 1 -> Chunk 2 -> Chunk 3 -> Chunk 4 -> Chunk 5 -> Chunk 6`
- 不允许跳过 `Chunk 1` 直接改执行层，否则新语义没有冻结，后续实现极易漂移
- 不允许在 `Chunk 4` 之前删除旧 governor，因为运行时仍无替代品
- 不允许在 `Chunk 5` 之前删除旧 `DesktopSearchPolicy` / warmup 模块，因为入口仍可能引用它们
- 不允许在最终回归通过前宣称 CPU 子系统重构“完成”
