# Preview World Offset Unification Design

**Goal:** 让主星 / 副星喷口参数链路只消费真实 `worldOffsetX/Y`，彻底删除按坐标前缀、`primaryPlacementIndex` 或 `82/212` 推断 offset 的旧逻辑。

## 背景

当前仓库的喷口参数链路分成了两层：

1. `AppRuntime` / `WorldGen` 负责生成世界本地预览数据：
   - 地图多边形
   - 本地喷口坐标
   - `worldAssetId`
   - `worldPlacementIndex`
2. `entry_sidecar.cpp` 在预览之后再额外构造 `GeyserSeedContext`：
   - `geyserSeed`
   - `worldOffsetX`
   - `worldOffsetY`

问题在于：当前 native 生成阶段并没有把真实 asteroid `worldOffset` 保存下来，sidecar 只能事后推断。现有推断逻辑存在三个根本缺陷：

1. 同时混用了两套来源：
   - `clusterCategory + secondaryPlacementIndex`
   - `coordinatePrefix` 硬编码
2. 只猜 `worldOffsetX`，`worldOffsetY` 永远是 `0`
3. 这套逻辑与游戏侧真实来源不一致。`mapsnotincluded` 抓取模块直接读取的是 `targetAsteroid.WorldOffset.X/Y`

因此，继续在 `ResolvePreviewWorldOffset()` 上打补丁，不可能得到统一且可信的结果。

## 目标状态

新的喷口参数链路必须满足：

1. 真实 `worldOffsetX/Y` 在生成阶段产出，而不是参数阶段猜测
2. `GeneratedWorldSummary` / `PreviewWorldSession` 明确携带目标 asteroid 的真实 offset
3. `BuildGeyserDetails(...)` 和 `BuildWorldReportData(...)` 只消费生成结果里携带的 offset
4. `ResolvePreviewWorldOffset()` 删除所有按前缀或列号硬编码 offset 的逻辑
5. 对无法证明 offset 正确的世界，参数链路必须 fail-closed，而不是返回错误参数

## 设计选择

### 方案 A：补 cluster 布局求解并生成真实 offset

这是本次推荐方案，也是唯一能长期保证正确性的方案。

做法：

1. 在 native 预览生成阶段增加“cluster 布局求解”步骤
2. 为当前 cluster 的每个 asteroid 求出真实 `worldOffsetX/Y`
3. 生成目标世界预览时，把对应 offset 写入 `GeneratedWorldSummary`
4. sidecar / 报告 / 喷口参数层全部改为消费该字段

优点：

- 逻辑统一
- 与游戏数据模型一致
- 不再需要 worldType/prefix 特判

缺点：

- 需要在本仓库内补一段当前尚不存在的 cluster 布局求解逻辑

### 方案 B：引入权威 offset 表

做法：

1. 从游戏实测或可信导出结果中整理一张 `coordinatePrefix + worldAssetId + placementIndex -> worldOffsetX/Y` 表
2. 预览阶段查表并写入 summary

优点：

- 实现最小
- 性能最好

缺点：

- 需要一份现成且可信的权威表
- 当前仓库与本地参考中没有可直接消费的 offset 数据源

### 方案 C：删除旧逻辑并对副星参数 fail-closed

做法：

1. 移除所有 offset 猜测逻辑
2. 对没有真实 offset 的目标世界直接不返回参数

优点：

- 正确性高
- 实现简单

缺点：

- 功能退化，不满足当前“副星参数要正确显示”的需求

## 当前执行结论

当前仓库内已经确认：

- 没有真实 asteroid `worldOffset.X/Y` 的生成通路
- 没有可直接复用的权威 offset 表
- 继续保留 `ResolvePreviewWorldOffset()` 的硬编码分支只会稳定产出错误参数

因此当前落地状态调整为 **方案 C 的兼容变体（主星恢复，副星继续安全态）**：

- 主星恢复到旧实现等价的 legacy offset policy，但规则被收敛进独立模块，不再散落在 `entry_sidecar.cpp`
- 副星继续因为缺少权威 `worldOffset` 而 fail-closed
- 方案 A 仍然是后续真正恢复副星参数能力的唯一正确路线

## 影响范围

### Native

- `src/App/ResultModels.hpp`
- `src/App/AppRuntime.cpp`
- `src/App/AppRuntime.hpp`
- `src/entry_sidecar.cpp`
- `tests/native/test_preview_world_session.cpp`

### Docs

- `docs/api/geyser-parameter-algorithm.md`
- `devdoc/v1.0.0/眼冒金星副星独立预览实施方案.md`

## 验证标准

1. `entry_sidecar.cpp` 中不再存在按 `coordinatePrefix`、`primaryPlacementIndex` 或 `82 / 212` 猜 offset 的分支
2. 主星参数请求能恢复到旧实现等价结果；副星参数请求仍会显式失败，不能返回看似合理但实际错误的参数
3. 主星 / 副星地图预览切换仍保持可用
4. native 回归测试能覆盖：
   - 主星 / 副星预览仍能生成
   - 主星参数链路恢复为旧实现等价
   - 副星参数链路继续 fail-closed
