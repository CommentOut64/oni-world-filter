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

另外，喷口参数复算不能只看世界内的本地显示坐标。游戏实际使用的是：

```cpp
seed = globalWorldSeed + absoluteX + absoluteY;
```

其中：

- `absoluteX = worldOffset.x + localX`
- `absoluteY = worldOffset.y + internalY`
- `worldOffset` 不是模板坐标的一部分，而是 cluster 初始化阶段通过 `BestFitWorlds` 给 asteroid 分配的全局偏移

这也是 Space Out / moonlet cluster 上最容易漏掉的一层。

## 2. 喷口类型确定

### 2.1 通用喷口

`name == "geysers/generic"` 时，类型由世界种子和喷口实例坐标决定：

```cpp
pos.y = worldHeight - template.position.y;
seed = globalWorldSeed + pos.x + template.position.y;
index = KRandom(seed).Next(0, count);
```

其中：

- `globalWorldSeed` 是世界坐标里的种子段
- `template.position.y` 是内部坐标，不能用翻转后的显示 `y`
- `count = 20`（非 Space Out）或 `23`（Space Out）
- 非 Space Out 时，`index == 19` 会被改写成 `21`

`index` 再映射到喷口类型表：

```text
steam, hot_steam, hot_water, slush_water, filthy_water,
slush_salt_water, salt_water, small_volcano, big_volcano,
liquid_co2, hot_co2, hot_hydrogen, hot_po2, slimy_po2,
chlorine_gas, methane, molten_copper, molten_iron,
molten_gold, molten_aluminum, molten_cobalt, oil_drip,
liquid_sulfur, chlorine_gas_cool, molten_tungsten, molten_niobium
```

### 2.2 固定喷口

模板里直接带 `GeyserGeneric_*` 的，类型固定，不再走通用抽签。

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
4. 对预览链路里的主星与稳定 warp 副星，当前实现都已经补齐了 cluster `worldOffset` 对喷口随机种子的影响，不再把本地预览坐标误当成游戏绝对坐标。
5. `oil_reservoir`、`warp_portal` 等非适用对象会显式返回 `hasParameters = false`，不伪造喷口参数。

当前仓库内与该算法配套的协议 / 宿主接入也已经具备以下能力：

1. `preview_geyser_details` sidecar 协议已落地，并以 `target = primary|secondary` 指定目标世界，可直接返回 `geyserDetails[]`。
2. Rust host / Tauri 已暴露 `load_preview_geyser_details`，并完成请求校验、命令构造与事件反序列化。
3. control sidecar 已支持 `preview_geyser_details` 的 one-shot 收集与终态事件判定。

当前仓库内与该算法配套的前端预览链路也已经完成闭环：

1. `previewStore` 已按 `previewKey(worldType:seed:mixing:target)` 聚合 preview 主体与 `geyserDetails[]`，并区分 `activeTarget`、`activePreview`、`activeGeyserDetailsStatus`、`activeGeyserDetailsError`。
2. 地图预览采用“两阶段”体验：地图主体先显示，喷口详情随后异步补齐；旧请求只允许写 cache，不能回写当前激活项。
3. `PreviewPane` 已支持主星 / 副星切换；首次切到副星时按 target 发请求，失败时继续保留主星画面并显示明确错误。
4. `PreviewCanvas` / `PreviewPane` 已通过容器内锚点 + Ant Design `Popover` 的受控浮层，在喷口点位附近展示温度、喷发率、平均总产出、喷发期和活跃期。
5. 副星只做展示，不参与批量筛选，也不允许从副星态触发报告导出。
