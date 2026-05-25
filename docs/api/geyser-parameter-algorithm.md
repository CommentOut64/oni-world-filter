# 喷口参数算法

本文说明缺氧喷口的权威生成链路。结论先写在前面：

- 喷口参数不是地图里“额外再随机一层”的黑盒值。
- 对于同一个世界坐标，喷口类型与原生参数都是确定性的。
- 真正的随机源只有进入喷口生成链路的 `globalWorldSeed` 和喷口实例的绝对坐标；地图生成后，喷口参数可以直接复算。

## 1. 生成入口

喷口相关逻辑分两步：

1. 先在世界生成阶段确定喷口实例和喷口类型。
2. 再为该喷口实例生成原生参数。

对应源码：

- `src/WorldGen.cpp` 的 `GetGeysers()`
- `src/App/AppRuntime.cpp` 的 `BuildSummary()`
- `src/Setting/SettingsCache.cpp` 的 `ParseAndApplyMixingSettingsCode()`
- `src/entry_sidecar.cpp` 的坐标解析链路

这里需要特别区分两种 seed：

- 世界坐标里的展示 seed，例如 `V-SNDST-C-1927980015-0-3A-0` 里的 `1927980015`
- 真正传给 `WorldGen::GetGeysers(...)` 和喷口参数复算的 `globalWorldSeed`

在当前仓库的 preview 链路里，权威做法已经和游戏实现对齐：

```cpp
int geyserSeed = baseSeed + static_cast<int>(cluster->worldPlacements.size()) - 1;
```

因此 `BuildGeyserDetails(...)` 必须使用这个 `geyserSeed`，不能直接把 `preview.summary.seed`
当成喷口参数的随机源。

另外，喷口参数复算不能只看世界内的本地显示坐标。当前仓库已经把喷口坐标明确拆成两轨：

- `summary.x / summary.y`：预览显示坐标，其中 `y = worldHeight - worldY`
- `summary.worldX / summary.worldY`：worldgen 原始坐标，直接进入参数复算

游戏实际使用的是：

```cpp
seed = globalWorldSeed + absoluteX + absoluteY;
```

其中：

- `absoluteX = worldOffset.x + worldX`
- `absoluteY = worldOffset.y + worldY`
- `worldOffset` 不是模板坐标的一部分，而是 cluster 初始化阶段通过 `BestFitWorlds` 给 asteroid 分配的全局偏移

也就是说，预览链和参数链现在不再共用同一份 `y` 语义；显示翻转只发生在预览坐标，参数链始终消费原始 world 坐标。

这也是 Space Out / moonlet cluster 上最容易漏掉的一层。

这里还有一个容易把预览点位整体“翻到天上去”的实现细节：

- 游戏模板放置阶段的 `TemplateSpawner.position` 本身就是 worldgen 原始坐标
- 模板内 `otherEntities[].location_x / location_y` 也是直接相对这个 root cell 相加
- 游戏不会在“取喷口实体坐标”这一步额外做一次 `worldHeight - y`

也就是说，`WorldGen::GetGeysers()` 的权威语义应该是：

```cpp
worldX = templateRootX + entityOffsetX;
worldY = templateRootY + entityOffsetY;
```

而不是先把模板 root 翻成显示坐标，再去叠加实体偏移。显示翻转只能发生在
`BuildSummary()` 这种专门给预览链产出 `summary.y` 的地方。

## 2. 喷口类型确定

当前仓库已经不再把喷口识别限定为单一 `GeyserGeneric_*` 字符串前缀，而是走统一喷口目录：

- `generic geyser`：模板名是 `geysers/generic`，先生成一个绝对实体 `GeyserGeneric`
- `fixed template geyser`：模板 `otherEntities` 中直接带 `GeyserGeneric_<key>`
- `reservoir / warp / cryopod`：模板 `otherEntities` 中的实体 id 直接映射到目录项
- `aquatic vent / aquatic geyser-like`：同样通过实体 id 进入统一目录

权威实现位于：

- `src/WorldGen.cpp` 的 `ExpandTemplateEntities()` / `GetGeysers()`
- `src/Geyser/GeyserCatalog.cpp`

### 2.1 通用喷口

`container.name == "geysers/generic"` 时，本地实现会先生成一个位于模板 root 的绝对实体：

```cpp
entityId = "GeyserGeneric";
worldX = templateRootX;
worldY = templateRootY;
```

随后再用绝对世界坐标决定类型：

```cpp
seed = globalWorldSeed + worldX + worldY;
index = KRandom(seed).Next(0, genericPoolIds.size());
```

其中：

- `globalWorldSeed` 是世界坐标里的种子段
- `worldX / worldY` 是 worldgen 原始坐标，不是预览显示坐标
- `genericPoolIds` 由 `Geyser::GetGenericRandomPoolIds(...)` 提供
- 非 Space Out 返回 20 个可选 id，Space Out 返回 23 个可选 id

也就是说，generic 抽签现在直接绑定“最终绝对喷口实体格子”，而不是绑定某个先翻转过的模板显示坐标。

### 2.2 固定喷口与实体喷口

非 generic 模板不再按模板 root 坐标直接猜喷口位置，而是先展开模板实体：

```cpp
worldX = templateRootX + entity.location_x;
worldY = templateRootY + entity.location_y;
```

再按实体 id 识别喷口类型：

- `GeyserGeneric_<key>` -> 固定模板喷口
- `OilWell` -> `oil_reservoir`
- `WarpPortal` -> `warp_portal`
- `WarpConduitSender` -> `warp_sender`
- `WarpConduitReceiver` -> `warp_receiver`
- `CryoTank` -> `cryo_tank`
- `GeyserGeneric_murky_brine` / `SmallReefGeyser` / `UnderwaterVent` -> aquatic 类对象

这一步的核心意义是：目录识别和坐标提取已经统一到同一条“模板实体绝对展开链”，预览链与参数链消费的是同一份喷口事实。

## 3. 原生参数生成

喷口的主随机参数是独立抽取的，不互相联动。社区资料和游戏实现一致地把它们视为 5 个主随机量：

- 平均活跃产量
- 喷发周期
- 喷发占比
- 活跃周期
- 活跃占比

每个喷口类型自己的区间表来自官方 `GeyserGenericConfig.GenerateConfigs()`；算法负责抽样，类型表负责给出每类喷口的 `min/max`。

其中：

- `活跃占比` 永远在 `40% ~ 80%`
- 其“中间 20%”是 `56% ~ 64%`
- 其它参数都按“中间 20% 更容易出现”的分布抽样，而不是纯均匀随机

### 3.1 分布

不是简单的 `min + rand * (max - min)`。

随机值 `x ∈ [0,1]` 会先经过一段 S 型映射，再落到目标区间，因此：

- 中间 20% 的出现概率约为 50%
- 两端更稀有

可复算时，使用游戏反编译后整理出的等价映射：

```text
f(x) = (ln(1 / (0.995054754 * x + 0.002472623) - 1.0) + 6) / 12
value = min + f(x) * (max - min)
```

这组常量不是随手拟合出来的，它们对应：

```text
0.0024726231566347743 = 1 / (1 + e^6)
0.9950547536867305    = tanh(3)
                        = 1 / (1 + e^-6) - 1 / (1 + e^6)
```

因此更接近源码语义的写法是：

```text
y = lerp(sigmoid(-6), sigmoid(6), x)
f(x) = (ln((1 - y) / y) + 6) / 12
```

如果要尽量贴近游戏实际运行结果，应注意：

- Unity / C# 这类逻辑通常按 `float` 单精度计算，不是 `double`
- 真正做 bit-level 复现时，应该使用单精度常量和单精度对数函数
- 仅用于人类可读展示时，写成上面的十进制等价式已经足够

### 3.2 线性换算

抽样后，再按区间线性缩放：

```text
value = min + f(x) * (max - min)
```

单位约定：

- `kg/cycle` 需要换算成 `g/s` 时，乘 `1000 / 600`
- `1 cycle = 600 s`

## 4. 派生值

以下不是原生随机参数，而是由原生参数算出来的：

- 喷发率 `eruption rate`
- 平均总产出 `average overall yield`
- 喷发期里的“喷发秒数”
- 活跃期里的“活跃周期数”

公式：

```text
eruption rate = average active yield / eruption amount
average overall yield = average active yield * active amount
eruption seconds = eruption period * eruption amount
active seconds = active period * active amount
active cycles = active seconds / 600
total cycles = active period / 600
```

因此游戏 UI 中最上面那一行的 `kg/s`，本质上是 `eruption rate`，不是 `average active yield` 本身。

## 5. 你在游戏里看到的数值

游戏界面常把原生参数显示成：

- `盐水：12.4 kg/s, 95°C`
- `喷发期：每 805 秒喷发 324 秒`
- `活跃期：每 81.8 周期活跃 56.4 周期`
- `平均产出：3437.3 g/s`

这些都是同一组原生参数的不同展示形式，不是额外随机出来的第二套结果。

## 6. 已验证样例

### 盐水泉

`V-SNDST-C-1927980015-0-3A-0`

- `12.4 kg/s`
- `95°C`
- `805 s / 324 s`
- `81.8 cycles / 56.4 cycles`
- `3437.3 g/s`

### 熔融铁火山

`V-SNDST-C-1927980015-0-3A-0`

- `7.3 kg/s`
- `2526.9°C`
- `730 s / 53 s`
- `95.9 cycles / 63.7 cycles`
- `355.6 g/s`

### 热蒸汽喷孔

`M-FLIP-C-644400493-0-0-0`

- `2.8386 kg/s`
- `500°C`
- `576.2 s / 259.8 s`
- `129.6 cycles / 80.4 cycles`
- `794.0 g/s`

## 7. 已验证类型区间

以下是本仓库已按该算法复核过的两类喷口区间：

### 盐水泉

- 平均活跃产量：`2000..4000 kg/cycle`
- 喷发周期：`60..1140 s`
- 喷发占比：`0.1..0.9`
- 活跃周期：`15000..135000 s`
- 活跃占比：`0.4..0.8`

### 熔融铁火山

- 平均活跃产量：`200..400 kg/cycle`
- 喷发周期：`480..1080 s`
- 喷发占比：`0.016666668..0.1`
- 活跃周期：`15000..135000 s`
- 活跃占比：`0.4..0.8`

## 8. 结论

当前已经可以确认：

- 喷口参数绑定 `globalWorldSeed` 与喷口实例坐标，不是地图生成后的额外黑盒随机
- 对于 Space Out cluster，喷口实例坐标必须先叠加 `worldOffset`，再参与随机种子
- 统一的参数抽样算法形式已经明确
- `GeyserGenericConfig.GenerateConfigs()` 的 26 类权威区间表已经可以从官方资料与游戏反编译中恢复

## 9. 当前实现状态

当前仓库内与算法直接相关的 native 实现已经具备以下能力：

1. `src/Geyser/GeyserParameterCalculator.cpp` 已实现 `GeyserGenericConfig.GenerateConfigs()` 的等价区间表。
2. `BuildGeyserDetails(...)` 已按游戏的 5 次随机抽样顺序复算原生参数。
3. `BuildGeyserDetails(...)` 与 `BuildWorldReportData(...)` 都以真实 `geyserSeed` 为输入，不再混用展示 world seed。
4. `oil_reservoir`、`warp_portal` 等非适用对象会显式返回 `hasParameters = false`，不伪造喷口参数。

当前预览链路对 `worldOffset` 的实现边界还需特别注意：

1. 游戏里的权威坐标来源是运行时 asteroid `WorldOffset.X/Y`，不是 `coordinatePrefix`、`worldPlacementIndex` 或 teleporter 列号。
2. 当前仓库已在 native 生成阶段复刻游戏的 `BestFit.BestFitWorlds(...)` 排版逻辑：先基于 world/world mixing 生效后的真实世界尺寸生成 `WorldPlacement.width/height`，再按高度降序扫描布局，产出每个 placement 的权威 `worldOffsetX/Y`。
3. `GeneratedWorldSummary` 已直接携带 `geyserSeed`、`worldOffsetX`、`worldOffsetY`；`preview`、`preview_geyser_details` 和 `world_report` 都只消费这份 summary，不再按主星前缀或副星门控二次猜 offset。
4. 眼冒金星多世界 cluster 下，主星与副星现在都走同一条 offset 数据通路；像 `M-BAD-C -> 82`、`M-FRZ-C -> 212` 这类结果如果出现，来源也是 `BestFit` 排版结果本身，而不是 sidecar 硬编码。
5. 单世界 cluster 仍会自然得到 `worldOffset = 0,0`，这是 `BestFit` 在单 asteroid 输入下的直接结果，不是单独特判。

当前仓库内与该算法配套的协议 / 宿主接入也已经具备以下能力：

1. `preview_geyser_details` sidecar 协议已落地，并以 `target = primary|secondary` 指定目标世界，可直接返回 `geyserDetails[]`。
2. Rust host / Tauri 已暴露 `load_preview_geyser_details`，并完成请求校验、命令构造与事件反序列化。
3. control sidecar 已支持 `preview_geyser_details` 的 one-shot 收集与终态事件判定。

当前仓库内与该算法配套的前端预览链路也已经完成闭环：

1. `previewStore` 已按 `previewKey(worldType:seed:mixing:target)` 聚合 preview 主体与 `geyserDetails[]`，并区分 `activeTarget`、`activePreview`、`activeGeyserDetailsStatus`、`activeGeyserDetailsError`。
2. 地图预览采用“两阶段”体验：地图主体先显示，喷口详情随后异步补齐；旧请求只允许写 cache，不能回写当前激活项。
3. `PreviewPane` 已支持主星 / 副星切换；首次切到副星时按 target 发请求，失败时继续保留主星画面并显示明确错误。
4. `PreviewCanvas` / `PreviewPane` 已通过容器内锚点 + Ant Design `Popover` 的受控浮层，在喷口点位附近展示温度、喷发率、平均总产出、喷发期和活跃期。
5. 当前主星 / 副星喷口参数都通过同一套 summary offset 计算，预览详情与 world report 不再分裂出单独的 offset 推断路径。
6. 副星只做展示，不参与批量筛选，也不允许从副星态触发报告导出。
