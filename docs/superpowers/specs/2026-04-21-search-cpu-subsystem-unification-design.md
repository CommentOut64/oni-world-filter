# 搜索 CPU 子系统统一重构设计

## 1. 背景

当前搜索 CPU 子系统已经出现明显的概念漂移：

- `CpuTopology` 同时承担“硬件事实”和“策略输入”两个角色
- `ThreadPolicy` 同时承担“候选策略描述”“线程绑定目标”“worker 数上限”三个角色
- `SearchRequest.workerCount`、`initialActiveWorkers`、`AdaptiveConcurrencyController`、`BoundedRecoveryController` 在执行层分别表达 ceiling、首档、降档、回拉，但没有共享统一的物理核语义
- `allowSmt` / `allowLowPerf` 的命名和传递方式看起来像 `custom` 专用，但又泄漏进全局策略
- desktop 无 warmup 路径额外引入 `DesktopSearchPolicy`，进一步把 CPU 语义拆成了“候选层”和“执行计划层”两套局部规则
- 线程绑定顺序在构造过程中会被排序打散，导致“先高性能物理核、后 SMT”的意图无法可靠落到执行上

结果是：

- 同一个参数在不同层表达不同含义
- “80%/100%”到底指物理核还是逻辑线程并不统一
- “SMT 开启”到底是允许第二线程、还是扩大上限、还是代表全 SMT ceiling 并不统一
- 运行时 governor 的调节粒度是 worker，不是物理核，因此与策略语义天然错位

本设计的目标不是继续在现有模型上打补丁，而是把搜索 CPU 子系统收敛为一套统一语义，并明确删除旧概念。

## 2. 已确认的业务决策

以下规则已由 `wgh` 明确确认，设计必须严格遵守：

- `balanced` 的绝对上限为允许参与的物理核的 `80%`
- `turbo` 的绝对上限为允许参与的物理核的 `100%`
- 上述百分比一律只指“物理核”，不指逻辑线程
- `balanced` 在异构 CPU 上至少预留 `1` 个高性能物理核
- `balanced` 在同构 CPU 上至少预留 `1` 个物理核
- `turbo` 不预留物理核
- `SMT` 是高级策略，但 `balanced` 和 `turbo` 默认都开启
- `SMT` 只控制“每个已选物理核最多持有几个线程”
- 启动阶段就把每个已选物理核允许的 SMT 线程全部打开
- 如果硬件没有 SMT 能力，不允许报错，必须自动退化为“每个物理核只有 1 个线程”
- 小核参与必须是独立高级策略，不应隐式由 `balanced/turbo` 模式本身决定

## 3. 目标与非目标

### 3.1 目标

- 为 CLI、desktop + sidecar 提供同一套共享 CPU 语义和共享核心数据模型
- 明确分离：探测事实、策略语义、绑定计划、运行时 governor
- 所有上限、保留、上下调都统一按“物理核”建模
- 让 SMT 从“混在 workerCount 里的隐式容量”收敛为“每核线程容量限制”
- 启动前完成完整的核优先级排序和绑定计划，不在运行中重新解释 CPU 拓扑
- 运行时上调下调统一以“物理核”为步长，worker 数只是派生结果
- 在 Intel 异构和无 SMT 新 CPU 上稳定工作
- 在同构 CPU 上保持稳定、可解释、可验证的启发式优先级

### 3.2 非目标

- 不承诺在任何 AMD / Intel 任意代 CPU 上都达到“理论最优”或“绝对最佳”吞吐
- 不在核心 CPU 子系统中引入重型 warmup、在线 benchmark 或机器级持久化校准缓存
- 不继续保留基于候选策略名的字符串协议作为长期核心抽象
- 不把“允许 SMT”和“允许小核”继续伪装成 `custom` 专属参数
- 不保留 `adaptive down` 与 `bounded recovery` 两套独立 governor 语义

## 4. 明确反驳与边界

### 4.1 关于“任何 AMD / Intel 任意一代都拿到相对最佳策略”

这个目标不能作为当前版本的硬承诺。

现有 Windows 可稳定获取的事实主要包括：

- `EfficiencyClass`
- `Group`
- `CoreIndex`
- `NumaNode`
- 逻辑处理器列表及其拓扑关系

这些事实足够区分 Intel 异构中的高性能核与低性能核，但不足以在很多 AMD 同构 CCD 场景下可靠判断“哪个物理核更快”。因此当前版本只能承诺：

- 基于系统可见拓扑事实的高质量统一启发式
- 在 Intel 异构场景下尽量准确地区分高性能核与低性能核
- 在同构 CPU 上保持稳定可解释的物理核优先顺序

如果未来要逼近“相对最佳”，只能作为独立扩展层处理，而不应污染本次核心模型。

### 4.2 关于“每次加 2 worker”

这条规则不应直接写死为 worker。

统一语义应为：

- 每次上调 `1` 个物理核
- 每次下调 `1` 个物理核
- 每个物理核实际贡献的 worker 数由该核可用线程数决定

于是：

- 在主流 SMT=2 的 CPU 上，`+1` 个物理核通常等价于 `+2 worker`
- 在无 SMT 硬件上，`+1` 个物理核自然等价于 `+1 worker`

这样才能避免在新 Intel 无超线程 CPU 上语义崩溃。

## 5. 统一后的核心概念

### 5.1 `CpuTopologyFacts`

只表达硬件事实，不表达策略。

建议结构：

```cpp
struct LogicalThreadFacts {
    uint32_t logicalIndex = 0;
    uint16_t group = 0;
    uint16_t coreIndex = 0;
    uint16_t numaNodeIndex = 0;
    uint8_t efficiencyClass = 0;
    bool parked = false;
    bool allocated = false;
};

enum class CorePerfTier {
    High,
    Low,
    Unknown,
};

struct PhysicalCoreFacts {
    uint16_t group = 0;
    uint16_t coreIndex = 0;
    uint16_t numaNodeIndex = 0;
    CorePerfTier perfTier = CorePerfTier::Unknown;
    std::vector<LogicalThreadFacts> logicalThreadsByPriority;
};

struct CpuTopologyFacts {
    bool detectionSucceeded = false;
    bool usedFallback = false;
    bool isHeterogeneous = false;
    std::vector<PhysicalCoreFacts> physicalCoresBySystemOrder;
    std::string diagnostics;
};
```

关键要求：

- 必须以“物理核”为一等公民，而不是先扁平化为逻辑线程
- 每个物理核内部必须保留逻辑线程优先顺序
- 禁止在这个层级夹带 `mode`、`allowSmt`、`allowLowPerf`、`workerCount` 等策略字段

### 5.2 `RankedPhysicalCores`

这是从拓扑事实中提炼出的“物理核优先级列表”，是后续一切策略与绑定的共同基础。

排序规则：

- 异构 CPU：
  - 高性能物理核在前
  - 低性能物理核在后
  - 同 tier 内按系统顺序稳定排序
- 同构 CPU：
  - 优先使用系统可见的首选物理核顺序
  - 若系统没有额外性能信号，则保持稳定系统顺序

说明：

- 当前版本不承诺同构 AMD CCD 的真实性能最优排序
- 但必须保证顺序稳定、可解释、可测试
- 该顺序一旦生成，就不允许后续再被无意义排序打散

### 5.3 `CpuPolicySpec`

只表达用户希望的策略语义，不表达执行状态。

建议结构：

```cpp
enum class CpuPolicyMode {
    Balanced,
    Turbo,
};

enum class CpuBindingMode {
    Strict,
    Preferred,
    None,
};

struct CpuPolicySpec {
    CpuPolicyMode mode = CpuPolicyMode::Balanced;
    bool allowSmt = true;
    bool allowLowPerf = false;
    CpuBindingMode bindingMode = CpuBindingMode::Strict;
};
```

关键要求：

- `SMT` 和“小核参与”是全局高级策略，不再附着在 `custom` 语义上
- `bindingMode` 是执行约束，不再混在候选字符串里
- `balanced/turbo` 不再通过候选名体现，而通过明确的 envelope 规则体现

### 5.4 `CpuExecutionEnvelope`

这是策略编译后的静态边界，运行时 governor 只能在该 envelope 内部活动。

建议结构：

```cpp
struct CpuExecutionEnvelope {
    uint32_t eligiblePhysicalCoreCount = 0;
    uint32_t reservedPhysicalCoreCount = 0;
    uint32_t absolutePhysicalCoreCap = 0;
    uint32_t startupPhysicalCoreCount = 0;
    uint32_t minActivePhysicalCoreCount = 1;
    uint32_t absoluteWorkerCap = 0;
};
```

统一规则：

- `eligiblePhysicalCoreCount`
  - 由 `allowLowPerf` 过滤后的可参与物理核数决定
- `reservedPhysicalCoreCount`
  - `balanced`：异构预留 `1` 个高性能物理核；同构预留 `1` 个物理核
  - `turbo`：`0`
- `absolutePhysicalCoreCap`
  - `balanced`：`min(ceil(eligible * 0.8), eligible - reserve)`，下限为 `1`
  - `turbo`：`eligible`
- `startupPhysicalCoreCount`
  - 当前版本等于 `absolutePhysicalCoreCap`
- `absoluteWorkerCap`
  - 前 `startupPhysicalCoreCount` 个物理核在 `SMT` 规则下可提供的总线程数

### 5.5 `CpuPlacementPlan`

这是启动前预编译好的绑定计划。

建议结构：

```cpp
struct PlannedCore {
    uint16_t group = 0;
    uint16_t coreIndex = 0;
    CorePerfTier perfTier = CorePerfTier::Unknown;
    std::vector<uint32_t> logicalIndicesByPriority;
};

struct CpuPlacementPlan {
    CpuBindingMode bindingMode = CpuBindingMode::Strict;
    std::vector<PlannedCore> rankedEligibleCores;
};
```

统一规则：

- `rankedEligibleCores` 只包含允许参与的物理核
- 每个 `PlannedCore.logicalIndicesByPriority` 中：
  - 第一个逻辑线程必须是 primary
  - 后续才是 SMT sibling
- `allowSmt=false` 时，只取每核第一个逻辑线程
- `allowSmt=true` 时，取该核所有允许逻辑线程
- 启动阶段直接把前 `startupPhysicalCoreCount` 个物理核的所有允许线程全部打开

关键删除项：

- 删除基于 `UniqueSorted()` 对目标逻辑线程排序的做法
- 删除通过 `workerIndex % targetLogicalProcessors.size()` 直接解释“优先级”的做法

### 5.6 `CpuRuntimeGovernor`

运行时 governor 只维护“活跃物理核数”，不再直接维护抽象 worker ceiling。

建议结构：

```cpp
struct CpuGovernorConfig {
    uint32_t minActivePhysicalCores = 1;
    uint32_t upStepPhysicalCores = 1;
    uint32_t downStepPhysicalCores = 1;
    double dropThreshold = 0.12;
    int dropWindows = 3;
    int stableWindowsForRecovery = 1;
    std::chrono::milliseconds cooldown{8000};
};

struct CpuGovernorState {
    uint32_t activePhysicalCores = 1;
    double peakThroughput = 0.0;
    double stageBaseline = 0.0;
    int consecutiveDrops = 0;
    int consecutiveStableWindows = 0;
    std::chrono::steady_clock::time_point lastAdjustment{};
};
```

统一规则：

- 调整粒度永远是“物理核”
- worker 数是派生值：
  - `activeWorkers = sum(前 activePhysicalCores 个物理核可用线程数)`
- 上调：
  - 每次增加 `1` 个物理核
  - 实际 worker 增量由该物理核线程数决定
- 下调：
  - 每次减少 `1` 个物理核
  - 总是移除当前活跃集合中优先级最低的那个物理核
- 不允许超过 `absolutePhysicalCoreCap`
- 不允许低于 `minActivePhysicalCoreCount`

## 6. 新策略语义

### 6.1 `balanced`

- 以允许参与的物理核集合为基数
- 绝对上限为 `80%`
- 异构：
  - 默认只使用高性能物理核
  - 至少预留 `1` 个高性能物理核
- 同构：
  - 使用允许参与的物理核集合
  - 至少预留 `1` 个物理核
- `SMT=on` 时：
  - 已选中的每个物理核在启动阶段直接开满所有允许线程
- `SMT=off` 时：
  - 每个已选中的物理核只开第一个逻辑线程

### 6.2 `turbo`

- 以允许参与的物理核集合为基数
- 绝对上限为 `100%`
- 不预留物理核
- `SMT=on` 时：
  - 所有已选物理核在启动阶段直接开满所有允许线程
- `SMT=off` 时：
  - 所有已选物理核只开每核第一个逻辑线程

### 6.3 `allowLowPerf`

- 它不改变策略百分比
- 它只改变“允许参与的物理核集合”
- 也就是说：
  - `balanced 80%`
  - `turbo 100%`
  都是作用于过滤后的 `eligiblePhysicalCoreCount`

### 6.4 `allowSmt`

- 它不改变物理核选择比例
- 它只改变“每个已选物理核贡献多少线程”
- 如果硬件没有 SMT 能力：
  - `allowSmt=true` 仍然合法
  - 每个物理核只贡献 `1` 个线程
  - 不报错，不分叉

## 7. 绑定与亲和性要求

### 7.1 绑定必须在启动前固定

CPU 绑定计划必须在 worker 启动前编译完成。

原因：

- 运行时不应该再重新解释拓扑
- governor 只应该决定“前多少个已排序物理核处于活跃态”
- 否则高性能核优先级和 SMT 线程优先级会再次漂移

### 7.2 默认绑定模式

搜索执行默认应使用 `Strict` 绑定。

理由：

- `Preferred` 只能表达调度偏好，不能保证线程真正停留在预期核上
- 当前用户诉求是 CPU 子系统行为必须稳定、可解释、启动前固定

但系统仍应保留降级路径：

- 当平台 API 或权限不支持严格绑定时：
  - 降级到 `Preferred`
  - 输出明确诊断
  - 不允许静默失败

### 7.3 超过 64 逻辑线程

当前基于 `SetThreadAffinityMask` 的简单实现不足以覆盖多 processor group 场景。

新系统必须支持组感知绑定：

- 优先使用 group-aware 的线程亲和性 API
- 绑定计划中必须显式保留 `group`
- 不允许继续假设“逻辑线程编号 < 64”

## 8. 运行时状态机

统一后的 governor 状态机如下：

1. `Startup`
   - 激活 `startupPhysicalCoreCount`
   - 每个活跃物理核按 `allowSmt` 规则开满线程
2. `Stable`
   - 维持当前活跃物理核数，采样吞吐
3. `CoolingDown`
   - 发生调整后进入 cooldown，期间不允许再次调整
4. `Dropped`
   - 连续检测到吞吐劣化后，下调 `1` 个物理核
5. `Recovering`
   - 稳定窗口满足后，上调 `1` 个物理核
6. `Capped`
   - 达到 `absolutePhysicalCoreCap`，不再继续上调

关键点：

- 当前系统中 `AdaptiveConcurrencyController` 和 `BoundedRecoveryController` 的分裂语义应被统一到一个 governor 状态机里
- 外部 `activeWorkerCap` 仍可保留为运行时安全上限，但它的语义应改为“最大活跃物理核数”或清晰映射后的 worker cap，不应继续与内部 governor 状态混杂

## 9. 需要删除的旧概念

以下旧概念应明确退出长期设计：

- `ThreadPolicy.name` 驱动核心 CPU 语义
- `BuildCandidates()` 输出一堆命名候选，再由调用方用字符串挑选
- `DesktopSearchPolicy` 作为 desktop 专属 CPU 解释层
- `SearchRequest.workerCount + initialActiveWorkers` 这套 ceiling / 首档双重模型
- `AdaptiveConcurrencyController + BoundedRecoveryController` 并列存在
- `customAllowSmt` / `customAllowLowPerf` 这种误导性命名
- 任何会打散物理核与逻辑线程优先顺序的排序去重逻辑

## 10. 新旧映射关系

| 当前概念 | 新概念 | 说明 |
| --- | --- | --- |
| `CpuTopology` | `CpuTopologyFacts` | 只保留探测事实 |
| `ThreadPolicy` | `CpuPlacementPlan + CpuExecutionEnvelope` | 不再混合候选名、worker 数、绑定目标 |
| `ThreadPolicyPlanner::BuildCandidates()` | `CompileCpuPlan()` | 直接编译统一 CPU 计划 |
| `DesktopSearchPolicy` | 删除 | desktop 不再单独解释 CPU 语义 |
| `workerCount` | `absoluteWorkerCap` | 派生结果，不再是输入真义 |
| `initialActiveWorkers` | 删除 | 启动阶段直接等于绝对物理核上限派生的 worker 数 |
| `AdaptiveConcurrencyController` | `CpuRuntimeGovernor` | 统一上下调模型 |
| `BoundedRecoveryController` | `CpuRuntimeGovernor` | 统一上下调模型 |

## 11. 迁移方案

### Phase 1: 引入新共享模型

- 新增 `CpuTopologyFacts`
- 新增 `PhysicalCoreFacts`
- 新增 `CpuPolicySpec`
- 新增 `CpuExecutionEnvelope`
- 新增 `CpuPlacementPlan`
- 新增 `CpuRuntimeGovernor`

要求：

- 先并行引入，不立即删除旧实现
- 为新模型补齐纯单测

### Phase 2: 新建统一编译入口

- 新增统一入口，例如：

```cpp
CompiledCpuPlan CompileCpuPlan(const CpuTopologyFacts&, const CpuPolicySpec&);
```

- 该入口直接输出：
  - `CpuExecutionEnvelope`
  - `CpuPlacementPlan`
  - `CpuGovernorConfig`

要求：

- desktop 与 CLI 都只能通过这一入口拿到 CPU 计划
- 不再允许调用方自己拼候选字符串

### Phase 3: 接管执行层

- `SearchRequest` 从“零散 CPU 参数”改为接收统一 `SearchCpuPlan`
- `BatchSearchService` 内部只认：
  - 当前活跃物理核数
  - 由 placement plan 派生的当前活跃 worker 数
- 替换旧的 adaptive / recovery 控制器

### Phase 4: 删除旧路径

- 删除 `DesktopSearchPolicy`
- 删除 `ThreadPolicyPlanner::BuildCandidates()` 对搜索主路径的职责
- 删除旧 governor
- 删除 `initialActiveWorkers`
- 删除所有与旧字符串候选名强绑定的文档和测试

## 12. 验证要求

### 12.1 探测与排序

- 异构 synthetic topology：
  - 高性能物理核必须全部排在低性能物理核之前
- 同构 synthetic topology：
  - 排序必须稳定
- 无 SMT synthetic topology：
  - `allowSmt=true` 仍只能得到每核 `1` 线程
- 多 group synthetic topology：
  - placement plan 必须保留 group 信息

### 12.2 策略 envelope

- `balanced` 异构：
  - 80% 基于高性能物理核
  - 至少预留 1 个高性能物理核
- `balanced` 同构：
  - 80% 基于允许参与的物理核
  - 至少预留 1 个物理核
- `turbo`：
  - 100% 使用所有允许参与的物理核，不预留
- `allowLowPerf=false`：
  - 异构时不得选入低性能物理核

### 12.3 运行时 governor

- 上调每次只增加 1 个物理核
- 下调每次只减少 1 个物理核
- 在 SMT=2 的 synthetic topology 上，worker 变化应表现为 `±2`
- 在无 SMT 的 synthetic topology 上，worker 变化应表现为 `±1`
- 永不超过绝对物理核上限

### 12.4 绑定

- `Strict` 模式下，绑定失败必须可诊断
- 无 SMT 时不能因“缺失 sibling”而报错
- >64 逻辑线程时不能因 affinity mask 宽度不足而直接失效

## 13. 主要风险

### 13.1 风险：同构 CPU 的“高性能核优先”只能是启发式

这是当前版本必须接受的边界。

缓解方式：

- 明确文档中不承诺“绝对最优”
- 保持排序稳定、可解释
- 若未来要进一步逼近最优，只能作为独立校正层实现

### 13.2 风险：强绑定可能暴露旧 API 的能力不足

缓解方式：

- 先补齐 group-aware 绑定能力
- 明确降级路径和诊断输出
- 不允许静默从 `Strict` 退化为无绑定

### 13.3 风险：大规模重构会影响 desktop 与 CLI 共享链路

缓解方式：

- 先引入新模型，再分阶段迁移
- 每个阶段必须有 synthetic topology 单测和真实 smoke 验证
- 未通过验证前，不允许删除旧路径

## 14. 最终结论

本设计选择的不是“继续调候选和阈值”，而是彻底统一 CPU 子系统的真义：

- 先探测物理核事实
- 再形成稳定的物理核优先级
- 再根据 `balanced/turbo + SMT + allowLowPerf` 编译成静态 envelope 和绑定计划
- 最后由运行时 governor 只按物理核上下调

这样才能同时满足：

- 物理核比例语义统一
- SMT 语义统一
- 高性能核优先统一
- 运行时上下调统一
- 无 SMT 硬件安全退化

任何继续保留“候选名 + workerCount + startupWorkers + adaptive/recovery 拼装”的路线，都会再次回到当前的混乱状态。
