# 搜索 catalog 协议兼容与结果会话恢复设计

## 目标

本次改动只解决两个直接问题：

1. Rust host 读取 sidecar `search_catalog` 时，对 `TraitMeta.forbiddenDLCIds` 字段兼容不足，导致 catalog 偶发整体加载失败。
2. 前端在热更新或页面 reload 后，`useSearchStore` 的内存态被重建，历史 `results / draft / selectedSeed` 全部丢失。

## 方案

### 1. catalog 协议兼容

- 保持 C++ sidecar 外发字段继续使用当前前端合同里的 `forbiddenDLCIds`。
- Rust host 的 `TraitMeta.forbidden_dlc_ids` 同时兼容：
  - `forbiddenDlcIds`
  - `forbiddenDLCIds`
- `get_search_catalog()` 的反序列化失败信息补充 `search_catalog` 上下文，避免只有裸 `JSON 错误`。

### 2. 前端会话恢复

- 新增纯函数模块处理 `sessionStorage` 的读写与脏数据清理。
- 仅持久化最小必要状态：
  - `draft`
  - `results`
  - `selectedSeed`
  - `lastSubmittedRequest`
- 不持久化运行中进程态：
  - `isSearching`
  - `activeJobId`
  - `bindingSidecar`
  - `listening`
- 恢复后：
  - `activeWorldType / activeMixing` 由 `draft` 回填
  - `stats.totalMatches` 由 `results.length` 回填
  - 若 `selectedSeed` 不存在于结果集中，则安全降级为 `null`

## 验证

- Rust 单元测试先覆盖 `forbiddenDLCIds` / `forbiddenDlcIds` 两种 catalog 输入都能反序列化。
- 前端用纯函数测试覆盖：
  - 正常快照恢复
  - 非法 `selectedSeed` 自动清理
  - 损坏的 session payload 自动删除
- 最后运行相关测试、`cargo test` 与前端 `tsc/vite build` 验证闭环。
