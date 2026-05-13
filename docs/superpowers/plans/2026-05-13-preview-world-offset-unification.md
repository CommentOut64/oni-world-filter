# Preview World Offset Unification Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让主星 / 副星喷口参数链路只消费生成阶段产出的真实 `worldOffsetX/Y`，删除当前所有按前缀或列号推断 offset 的旧逻辑。

**Architecture:** 先在 native 生成链路中为目标 asteroid 建立真实 offset 数据通路，再让 sidecar 的喷口参数和报告层只消费这份数据。如果当前仓库无法提供可信的 offset 求解来源，则立即 fail-closed 并停止继续硬编码。

**Tech Stack:** C++23、Tauri sidecar、jsoncpp、ctest、PowerShell

---

## 当前状态

- 已确认当前仓库内不存在真实 asteroid `worldOffset.X/Y` 的生成或导出通路。
- 已确认副星当前仍没有可信 offset 来源，不能继续猜。
- 当前已先把旧主星 offset 规则收敛进独立 `PreviewWorldOffsetPolicy` 模块，并恢复主星到旧实现等价行为。
- 若后续要重新开启副星喷口参数，仍必须先补齐方案 A 所需的真实 offset 来源。

## File Map

- `src/Geyser/PreviewWorldOffsetPolicy.hpp`
  - 声明主星 legacy-equivalent offset policy
- `src/Geyser/PreviewWorldOffsetPolicy.cpp`
  - 实现主星旧 offset 规则，并集中管理
- `src/entry_sidecar.cpp`
  - 改为消费独立 offset policy；主星恢复，副星继续 gate
- `tests/native/test_preview_world_session.cpp`
  - 锁定“主星恢复旧实现等价、副星继续 fail-closed”的行为
- `docs/api/geyser-parameter-algorithm.md`
  - 同步新的 offset 来源
- `devdoc/v1.0.0/眼冒金星副星独立预览实施方案.md`
  - 同步实现约束

## Chunk 1: Lock The New Contract

### Task 1: 先写“offset 由生成阶段提供”的失败测试

**Files:**
- Modify: `tests/native/test_preview_world_session.cpp`
- Reference: `src/App/ResultModels.hpp`
- Reference: `src/entry_sidecar.cpp`

- [ ] **Step 1: 为 `GeneratedWorldSummary` 新契约补测试断言**

断言目标：

1. 主星 / 副星 summary 最终都应带 `worldOffsetX`
2. 若当前目标世界可生成参数，则 `worldOffsetY` 也必须明确来自生成结果，而不是 helper 默认值

- [ ] **Step 2: 为 sidecar 参数链路补失败断言**

断言目标：

1. `ResolvePreviewWorldContext(...)` 或等价上下文结构必须从 preview session 读取 offset
2. 参数层不允许再单独解析 prefix 得出 offset

- [ ] **Step 3: 运行测试，确认当前失败**

Run:

```powershell
ctest --test-dir out/build/x64-release -C Release --output-on-failure -R test_preview_world_session
```

Expected:

- 失败点落在“summary/session 不携带真实 offset”或“参数层仍在猜 offset”

## Chunk 2: Verify Offset Source Feasibility

### Task 2: 在现有仓库内确认真实 offset 的可实现来源

**Files:**
- Modify: `src/App/AppRuntime.cpp`
- Optional Modify: `src/App/AppRuntime.hpp`

- [ ] **Step 1: 明确当前生成阶段是否持有真实 cluster 布局结果**

检查点：

1. `AppRuntime`
2. `WorldGen`
3. `SettingsCache`
4. cluster / placement 相关结构

- [ ] **Step 2: 若存在可复用来源，提取最小数据通路**

要求：

1. 不引入新的猜测逻辑
2. 直接把真实 `worldOffsetX/Y` 附着到 summary

- [ ] **Step 3: 若不存在可复用来源，立即停止并记录 blocker**

要求：

1. 不继续实现新的硬编码映射
2. 明确缺的是哪段 cluster 布局求解能力

## Chunk 3: Thread Offset Through Preview Data

### Task 3: 把真实 offset 写入预览结果

**Files:**
- Modify: `src/App/ResultModels.hpp`
- Modify: `src/App/AppRuntime.cpp`

- [ ] **Step 1: 扩展 `GeneratedWorldSummary`**

新增字段：

```cpp
int worldOffsetX{};
int worldOffsetY{};
```

- [ ] **Step 2: 在生成 summary 时写入真实 offset**

要求：

1. 主星 / 副星都走同一条赋值路径
2. 不允许在 summary 构造后再补写猜测值

- [ ] **Step 3: 运行相关 native 测试**

Run:

```powershell
ctest --test-dir out/build/x64-release -C Release --output-on-failure -R test_preview_world_session
```

## Chunk 4: Delete Old Offset Guessing

### Task 4: 清除 `entry_sidecar.cpp` 中旧 offset helper

**Files:**
- Modify: `src/entry_sidecar.cpp`
- Modify: `tests/native/test_preview_world_session.cpp`

- [ ] **Step 1: 写失败测试锁定“参数层不再猜 offset”**

断言目标：

1. `ResolvePreviewWorldOffset()` 不再保留 prefix 分支
2. `GeyserSeedContext` 的 offset 来源于 preview/session summary

- [ ] **Step 2: 删除旧逻辑并改接真实 offset**

要求：

1. 移除 `M-*` / `V-*` / `82/212` 分支
2. 若真实 offset 缺失，则显式失败

- [ ] **Step 3: 运行 native 回归**

Run:

```powershell
ctest --test-dir out/build/x64-release -C Release --output-on-failure -R "test_preview_world_session|test_geyser_parameter_calculator|test_sidecar_protocol"
```

## Chunk 5: Docs Sync

### Task 5: 同步文档为“真实 offset 来源”

**Files:**
- Modify: `docs/api/geyser-parameter-algorithm.md`
- Modify: `devdoc/v1.0.0/眼冒金星副星独立预览实施方案.md`

- [ ] **Step 1: 重写算法说明**

要求：

1. 删除“按 prefix / 82 / 212 推断”的现行叙述
2. 明确 offset 来自生成阶段

- [ ] **Step 2: 更新实施方案状态**

要求：

1. 写清当前统一方案
2. 写清 blocker 条件与 fail-closed 约束

- [ ] **Step 3: 运行最终回归**

Run:

```powershell
ctest --test-dir out/build/x64-release -C Release --output-on-failure -R "test_preview_world_session|test_geyser_parameter_calculator|test_sidecar_protocol"
```

Plan complete and saved to `docs/superpowers/plans/2026-05-13-preview-world-offset-unification.md`. Ready to execute.
