# Desktop 坐标直达预览设计

> Type: Design | Status: Approved

## 目标

在 `desktop/` 搜索页顶部新增一个仅支持标准坐标码的直达入口。用户输入标准坐标码后，系统不触发批量搜索，而是直接跳转到现有结果页，并自动显示与该坐标完全对应的地图预览。

## wgh 已确认的约束

- 只支持完整标准坐标码，不支持纯 `seed`。
- 输入入口必须放在页面顶部，并与“搜索参数”标题、主题切换按钮同行。
- 单项坐标预览不能误触发批量搜索。
- 方案优先最小化扩展，但必须保证“输入坐标”和“显示地图”严格对应。

## 当前代码事实

- [`desktop/src/features/search/SearchPanel.tsx`](f:/oni_world_app-master/desktop/src/features/search/SearchPanel.tsx) 当前把整页搜索参数包在同一个 `<form>` 内，任何放进这个表单里的输入框按 `Enter` 都可能命中批量搜索提交。
- [`desktop/src/app/App.tsx`](f:/oni_world_app-master/desktop/src/app/App.tsx) 没有路由，搜索页和结果页通过 `activePage` 在同一壳层内切换。
- [`desktop/src/features/preview/PreviewPane.tsx`](f:/oni_world_app-master/desktop/src/features/preview/PreviewPane.tsx) 会根据 `searchStore.results + selectedSeed` 自动调用 [`previewStore.loadByMatch`](f:/oni_world_app-master/desktop/src/state/previewStore.ts) 拉取预览。
- 当前桌面端预览链路只接受 `worldType + seed + mixing`，再由 C++ sidecar 在 [`BuildWorldCode`](f:/oni_world_app-master/src/entry_sidecar.cpp) 拼回坐标，然后在 [`AppRuntime::Generate`](f:/oni_world_app-master/src/App/AppRuntime.cpp) 内通过 [`SettingsCache::CoordinateChanged`](f:/oni_world_app-master/src/Setting/SettingsCache.cpp) 解析并生成地图。

## 问题判断

如果继续沿用“前端自己解析坐标，再拆成 `worldType + seed + mixing` 调 preview”的做法，虽然改动更少，但前端会复制一套坐标语义，后续一旦与后端 `CoordinateChanged` 漂移，就无法保证坐标和地图严格一致。

因此本次不采用“纯前端解析 + 现有 preview 请求”的极限补丁，而采用范围受控的局部权威化方案：只有“坐标直达预览”这条新链路改为由后端直接解释原始坐标；现有批量搜索和结果表行选中预览链路保持不变。

## 最终方案

### 1. 顶部入口与事件隔离

在 [`SearchPanel.tsx`](f:/oni_world_app-master/desktop/src/features/search/SearchPanel.tsx) 的页头行新增一个独立的“坐标直达”控件区，布局变为：

- 左侧：`搜索参数` 标题
- 中间：坐标输入框 + `搜索` 按钮
- 右侧：主题切换按钮

这个控件区在视觉上与标题同行，但在 DOM 结构上必须放在批量搜索 `<form>` 之外，并使用独立的 `button` 事件处理器，不复用 `submit`、`methods.handleSubmit` 或 `startSearchJob()`。

这样可以从根源上避免：

- 输入框按 `Enter` 误触发批量搜索
- 坐标直达逻辑写入批量搜索状态机
- 单项预览与批量搜索共用提交入口

### 2. 后端权威解析坐标

新增一条专用的 `preview_coord` 请求链路：

- 前端发送原始坐标码，不在前端做最终语义解析
- Tauri / Rust sidecar 只做“非空请求”校验和命令转发
- C++ sidecar 负责坐标解析、规范化和地图生成

后端处理规则如下：

1. 通过 `SearchAnalysis::GetWorldPrefixes()` 做世界前缀匹配
2. 从原始坐标中提取 `seed` 与 base36 `mixing`
3. 使用 `SettingsCache::Base36ToBinary()` 反解 `mixing`
4. 使用现有 [`BuildWorldCode`](f:/oni_world_app-master/src/entry_sidecar.cpp) 反向重建 canonical 坐标码
5. 若重建结果与用户输入不完全一致，则拒绝请求
6. 使用 canonical 坐标码进入 `AppRuntime::Generate()`，由 `CoordinateChanged()` 建立最终生成状态

这套链路的关键点是：**同一个后端既负责解释坐标，也负责实际生成地图，并在生成前先做一次 canonical 回写校验。**

### 3. 结果页复用方式

坐标直达成功后不启动批量搜索，而是直接复用现有结果页骨架：

- 后端返回 canonical `coord + worldType + seed + mixing + preview`
- 前端基于返回的 `preview.summary` 构造一条完整的 `SearchMatchSummary`
- 将该记录写入 `searchStore.results = [match]`
- 将 `searchStore.selectedSeed = seed`
- 将 `preview` 预热进 `previewStore.cache` 并设为当前活动预览
- 然后调用现有 `onViewResults()` 切到结果页

这样结果页右侧预览可以直接显示，不需要额外新页面，也不会重复发一次预览请求。

### 4. 批量搜索状态边界

新增的坐标直达链路必须与批量搜索状态机完全隔离：

- 不调用 `startSearchJob()`
- 不写入 `isSearching = true`
- 不生成 `activeJobId`
- 不构造 `lastSubmittedRequest`
- 不覆盖当前批量搜索表单 `draft`

坐标直达只做“一次性解析 + 一次性预览 + 单条结果注入”。用户查看后返回搜索页时，原有批量搜索条件仍然保留。

为避免和正在运行的批量搜索共用结果状态，页头坐标入口在 `isSearching === true` 时直接禁用。

### 5. 前端最小新增单元

为控制修改面，本次新增/抽离的前端单元只包含三类：

- 一个纯展示组件：`CoordQuickSearch`
- 一个纯流程函数：负责“调用 `loadPreviewByCoord` -> 构造单条结果 -> 预热 preview -> 切页”
- 一个共享结果构造 helper：统一由搜索命中事件和坐标直达预览构造 `SearchMatchSummary`

这能避免把新的直达逻辑堆进现有体积已经较大的 `SearchPanel.tsx` 和 `searchStore.ts`。

## 不做项

- 不支持纯 `seed` 输入
- 不支持空格清洗、别名格式、小写容错和模糊解析
- 不把“坐标直达”改造成“单 seed 批量搜索”
- 不改现有批量搜索 sidecar 流式事件协议
- 不新增页面或路由

## 成功标准

1. 页面顶部出现与标题同行的坐标直达输入区。
2. 输入标准坐标码后，不触发批量搜索，直接进入结果页。
3. 结果页右侧地图预览与输入坐标完全对应，坐标解析权威逻辑只在后端存在。
4. 坐标直达成功后，结果表只出现一条记录，且该记录使用后端返回的 canonical 坐标。
5. 坐标格式非法或解析失败时，停留在搜索页并展示错误，不跳页。
6. 现有批量搜索、结果表点击预览、主题切换和结果清空行为不回归。

