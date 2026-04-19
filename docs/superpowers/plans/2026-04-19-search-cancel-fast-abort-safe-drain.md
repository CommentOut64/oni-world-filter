# Search Cancel Fast-Abort Safe-Drain Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在不修改 C++ 世界生成内核的前提下，把桌面端搜索取消改成“宿主快停 + stdout 排空”，让取消尽快生效，同时尽量保住已经输出到管道中的命中结果。

**Architecture:** 本次不尝试把 `runtime->Generate()` 做成可中断，而是把 Rust 宿主改成取消主导方。用户点击取消后，宿主立即把任务切到 `cancelling`，best-effort 发送 sidecar `cancel`，只给一个很短的优雅退出窗口；超时后直接 `kill` 子进程，但继续把该进程 stdout 里已经在管道中的 NDJSON 读到 EOF，再由宿主统一合成最终 `cancelled` 终态事件。前端在 `cancelling` 阶段继续接受同一 `jobId` 的 `match`，直到真正收到终态事件才清掉活动任务。

**Tech Stack:** Tauri 2 Rust 宿主、C++ sidecar、React 19、Zustand、PowerShell smoke 脚本

---

## File Map

### Existing Files To Modify

- `src-tauri/src/state.rs`
  - 现状：只有 `Running / Completed / Failed / Cancelled` 四种状态，且不保存任何搜索进度快照。
  - 目标：增加 `Cancelling` 状态，并为宿主合成取消终态保留最后一次进度/命中统计快照。
- `src-tauri/src/sidecar.rs`
  - 现状：`cancel_search()` 最多等 1 秒 sidecar 自行取消，再 `kill`；`stdout` 转发与进程退出收尾彼此独立，无法保证“kill 后仍把已在管道中的输出读完再终结 job”。
  - 目标：让宿主成为取消最终裁决方，支持短等待、超时强杀、继续排空 stdout、EOF 后统一发最终 `cancelled`。
- `desktop/src/state/searchStore.ts`
  - 现状：`cancelSearchJob()` 调完 Tauri 命令后，无论是否真正终止都会把 `isCancelling` 复位；终态事件只负责切 `isSearching`。
  - 目标：前端在 `cancelling` 阶段继续保留 `activeJobId` 并接受同 job 的 `match`，直到真实终态事件到达才结束。

### New Files To Create

- `desktop/tests/searchCancelState.test.ts`
  - 职责：验证前端取消状态机，确保 `cancelling` 期间不会丢同 job 的晚到 `match`，且 `cancelled` 后结果列表仍保留。
- `scripts/smoke/run-sidecar-cancel-latency.ps1`
  - 职责：以“宿主快停 + 排空 stdout”的同等策略直接驱动 sidecar，测取消延迟、终态类型和已输出结果保留情况。

### Existing Files To Reuse For Verification

- `src-tauri/src/sidecar.rs` 现有单测
- `src-tauri/src/state.rs` 现有单测
- `scripts/smoke/run-tauri-search.ps1`
- `scripts/smoke/run-sidecar-consistency-check.ps1`

## Constraints

- 不修改 `src/App/`、`src/WorldGen.cpp`、`src/entry_sidecar.cpp` 的世界生成/匹配内核逻辑。
- 不把本次方案包装成“彻底可中断世界生成”；本次只解决“取消体验”和“已输出结果尽量不丢”。
- 不把 `cancelled` 伪装成 `failed`。
- 不在 `cancel_search()` 里提前给前端发终态 `cancelled`，必须等 stdout 排空或真实终态到达。
- 不自动提交；提交由 `wgh` 手动执行。

## Non-Goals

- 不保证保住“还没 flush 到 stdout 的最后几个 match”。
- 不为 sidecar 增加持久化检查点。
- 不修改 llmdoc。

## Current Root Cause Summary

- 前端取消发得不慢：`cancelSearchJob()` 直接调用 `invoke("cancel_search")`，无防抖，无额外等待。
- 最新 sidecar 已在使用中，但搜索 worker 对取消的检查只发生在“进入 seed 前 / seed 与 seed 之间”。
- 单个 `evaluateSeed(seed)` 内部会进入 `runtime->Generate()`，当前不可中断。
- 因此“协作式取消”会卡在当前正在生成的首个 seed 上。
- 现有 `kill` 兜底虽然存在，但 job 终态与 stdout 排空没有建立严格顺序，无法保证“快停”和“尽量不丢已输出结果”同时成立。

## Success Criteria

- 点击取消后，前端立即进入 `取消中...` 状态，不再继续显示“正常搜索中”。
- 对大范围搜索执行取消时，最终终态在 `1s` 内到达；目标值为 `300ms` 级，硬上界为 `1000ms`。
- 已经进入前端 `results` 的结果不能因取消而被清空。
- 已经由 sidecar 写入 stdout 且仍在宿主管道中的 `match`，在 kill 后仍尽量被前端接收。
- 用户取消的任务最终必须落为 `cancelled`，不能变成 `failed`。
- 同一个 job 只允许一个终态事件。

## Residual Risk To Document

- 如果某个命中还停留在 sidecar 内存里、尚未写入 stdout，就可能在 `kill` 时丢失。
- 这是本方案接受的剩余风险；要完全消除，必须做世界生成内核级取消注入，超出本计划范围。

## Chunk 1: Host Cancellation State And Snapshot

### Task 1: 扩展 Rust job 状态与进度快照

**Files:**
- Modify: `src-tauri/src/state.rs`

- [ ] **Step 1: 写失败测试，覆盖 `Cancelling` 状态与快照读写**

新增或扩展 `state.rs` 单测，至少覆盖：

- `JobStatus::Cancelling` 可被写入和读取
- registry 可保存/读取最后一次 `processedSeeds / totalSeeds / totalMatches / activeWorkers`
- `Running -> Cancelling -> Cancelled` 状态流转可用

- [ ] **Step 2: 运行单测确认红灯**

Run: `cargo test --manifest-path src-tauri/Cargo.toml state::tests -- --nocapture`

Expected:

- 新增状态或快照相关测试失败，原因与缺少实现一致

- [ ] **Step 3: 在 `state.rs` 增加最小数据结构**

建议最小新增：

- `JobStatus::Cancelling`
- `JobProgressSnapshot`
- registry 的 `update_progress_snapshot()` / `get_progress_snapshot()`

要求：

- 不引入与搜索无关的新全局状态
- 只保存合成 `cancelled` 所需字段

- [ ] **Step 4: 再跑同一组测试确认通过**

Run: `cargo test --manifest-path src-tauri/Cargo.toml state::tests -- --nocapture`

Expected:

- `state.rs` 相关测试全部通过

## Chunk 2: Host Fast-Abort And Stdout Drain

### Task 2: 重构 `cancel_search()` 为“短等待 + 强杀 + 延后终结”

**Files:**
- Modify: `src-tauri/src/sidecar.rs`

- [ ] **Step 1: 写失败测试，覆盖宿主取消状态机的关键边界**

至少覆盖以下语义：

- 用户取消后，job 状态先进入 `Cancelling`
- `cancel_search()` 不直接发本地 `cancelled`
- grace timeout 到期后会强杀子进程
- 强杀后不会把 `Cancelling` job 误标成 `Failed`

如直接测试线程/`AppHandle` 太重，可先抽小 helper 再测 helper。

- [ ] **Step 2: 运行相关 Rust 测试确认红灯**

Run: `cargo test --manifest-path src-tauri/Cargo.toml sidecar::tests -- --nocapture`

Expected:

- 与取消状态机相关的新测试失败

- [ ] **Step 3: 修改 `cancel_search()` 主流程**

实现要求：

- 收到取消后，先把 registry 状态改成 `Cancelling`
- best-effort 写 sidecar `cancel`
- 最多等待 `50-100ms`
- 若仍未收到真实终态，则：
  - 关闭 `stdin`
  - `kill` 子进程
- `cancel_search()` 返回时不直接发送终态 `cancelled`

- [ ] **Step 4: 修改 `spawn_stdout_forwarder()`**

实现要求：

- 在 `started / progress / match` 到来时持续更新 registry 进度快照
- 真实收到 `completed / failed / cancelled` 时仍按现有协议转发，并更新状态
- 当 stdout 读到 EOF 时：
  - 如果当前状态是 `Cancelling`
  - 且此前没有真实终态
  - 由宿主基于最后快照合成一个 `cancelled` 事件再发给前端

- [ ] **Step 5: 修改 `spawn_waiter()`**

实现要求：

- 对于普通退出仍保持现有逻辑
- 对于 `Cancelling` 状态下的非零退出码，不要额外发 `failed`
- 让“取消后的最终终态”只由真实 sidecar 终态或 stdout EOF synth 二选一产生

- [ ] **Step 6: 保持单一终态约束**

要求：

- 同一 job 不能出现“先 synth cancelled 再收到 failed”或“先真实 cancelled 再 synth cancelled”
- 若需要，增加布尔标志或状态判断，但范围只限当前 job 生命周期

- [ ] **Step 7: 再次运行 Rust 侧测试**

Run: `cargo test --manifest-path src-tauri/Cargo.toml sidecar::tests -- --nocapture`

Expected:

- `sidecar.rs` 现有测试继续通过
- 新增取消状态机测试通过

## Chunk 3: Frontend Cancel Lifecycle

### Task 3: 让前端在 `cancelling` 期间继续接收同 job 的晚到 `match`

**Files:**
- Modify: `desktop/src/state/searchStore.ts`
- Create: `desktop/tests/searchCancelState.test.ts`

- [ ] **Step 1: 写失败测试，覆盖取消状态流**

测试至少覆盖：

- 调用取消后，`isCancelling = true`
- 在收到终态前，`activeJobId` 仍保留
- `cancelling` 期间到来的同 job `match` 仍会追加到 `results`
- 收到 `cancelled` 后：
  - `isSearching = false`
  - `isCancelling = false`
  - `activeJobId = null`
  - `results` 保留

- [ ] **Step 2: 运行前端测试确认红灯**

Run: `node --test --experimental-strip-types desktop/tests/searchCancelState.test.ts`

Expected:

- 新测试失败，原因与当前 `isCancelling` 提前复位或终态收尾逻辑不符

- [ ] **Step 3: 修改 `cancelSearchJob()`**

实现要求：

- 不要在 `finally` 里无条件把 `isCancelling` 设回 `false`
- 只有取消命令调用失败时，才回滚 `isCancelling`
- 成功发出取消后，保持 `isSearching = true`、`isCancelling = true`

- [ ] **Step 4: 修改终态事件处理**

在 `failed / completed / cancelled` 三种终态里统一：

- `isSearching = false`
- `isCancelling = false`
- `activeJobId = null`

额外要求：

- `cancelled` 不清空 `results`
- `cancelled` 以事件 payload 更新 stats，但不覆盖已有结果列表

- [ ] **Step 5: 再跑同一测试确认通过**

Run: `node --test --experimental-strip-types desktop/tests/searchCancelState.test.ts`

Expected:

- 前端取消状态测试通过

## Chunk 4: Smoke Verification

### Task 4: 增加取消延迟 smoke 脚本

**Files:**
- Create: `scripts/smoke/run-sidecar-cancel-latency.ps1`

- [ ] **Step 1: 写 smoke 脚本**

脚本职责：

- 指定 sidecar 路径
- 发送大范围 `search`
- 延迟 `100ms` 后发送 `cancel`
- 模拟宿主的 `50-100ms` grace timeout
- 超时后 `kill`
- 继续排空 stdout 到 EOF
- 输出：
  - `cancel -> terminal event` 耗时
  - 是否使用了 `kill`
  - 已接收 `match` 数量
  - 终态事件类型

- [ ] **Step 2: 运行脚本验证最新 sidecar**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\smoke\run-sidecar-cancel-latency.ps1 -SidecarPath out\build\mingw-release\src\oni-sidecar.exe
```

Expected:

- 返回 `cancelled`
- 不出现宿主级 `failed`
- 终态耗时满足计划的硬上界

- [ ] **Step 3: 如有必要，再用 `src-tauri\binaries\oni-sidecar.exe` 复跑一次**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\smoke\run-sidecar-cancel-latency.ps1 -SidecarPath src-tauri\binaries\oni-sidecar.exe
```

Expected:

- 行为与 `mingw-release` 一致

## Chunk 5: Integrated Verification

### Task 5: 交付前门禁

**Files:**
- Verify only

- [ ] **Step 1: 运行 Rust 侧单测**

Run: `cargo test --manifest-path src-tauri/Cargo.toml sidecar::tests -- --nocapture`

- [ ] **Step 2: 运行状态 registry 单测**

Run: `cargo test --manifest-path src-tauri/Cargo.toml state::tests -- --nocapture`

- [ ] **Step 3: 运行前端取消状态测试**

Run: `node --test --experimental-strip-types desktop/tests/searchCancelState.test.ts`

- [ ] **Step 4: 运行取消延迟 smoke**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\smoke\run-sidecar-cancel-latency.ps1 -SidecarPath out\build\mingw-release\src\oni-sidecar.exe
```

- [ ] **Step 5: 手工验证桌面端**

手工检查：

- 发起一轮较大搜索
- 点击取消后，按钮立即变成 `取消中...`
- 在终态到来前，若 stdout 里还有晚到 match，结果列表仍可继续增加
- 终态到来后，页面变为非搜索中，结果列表保留

- [ ] **Step 6: 如实记录结果与残余风险**

必须明确记录：

- 最终取消延迟
- 是否触发过 `kill`
- 是否仍存在“未 flush match 可能丢失”的残余风险

## Acceptance Checklist

- [ ] 用户点击取消后，前端立即进入 `取消中...`
- [ ] 最终终态在 `1s` 内到达
- [ ] 已进入前端 `results` 的结果不会因取消而丢失
- [ ] stdout 中已排队的 `match` 在 kill 后仍可尽量被接收
- [ ] 用户取消不会被错误显示为 `failed`
- [ ] 同一 job 不会发出重复终态

## Notes For Implementation

- 这是一条“有效且简单”的宿主层方案，不是世界生成内核级根修。
- 本方案的核心不是“更早发 cancelled”，而是“更早 kill，但更晚 finalize”。
- 如果实现过程中发现必须改 C++ 内核才能满足硬上界，不要继续扩散；那是另一份计划。
- 本计划不包含自动提交步骤；由 `wgh` 手动提交。
