# 喷口参数算法

本文说明缺氧喷口的权威生成链路。结论先写在前面：

- 喷口参数不是地图里“额外再随机一层”的黑盒值。
- 对于同一个世界坐标，喷口类型与原生参数都是确定性的。
- 真正的随机源只有世界种子和喷口实例坐标；地图生成后，喷口参数可以直接复算。

## 1. 生成入口

喷口相关逻辑分两步：

1. 先在世界生成阶段确定喷口实例和喷口类型。
2. 再为该喷口实例生成原生参数。

对应源码：

- `src/WorldGen.cpp` 的 `GetGeysers()`
- `src/Setting/SettingsCache.cpp` 的 `ParseAndApplyMixingSettingsCode()`
- `src/entry_sidecar.cpp` 的坐标解析链路

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

## 7. 结论

如果世界码和喷口坐标不变，喷口参数就是确定的，可提前批量计算。

## 8. 已验证类型样例

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
*** Add File: f:\oni_world_app-master\llmdoc\reference\geyser-parameter-algorithm.md
# 喷口参数算法

> Type: Reference | Status: Active

请以 `docs/api/geyser-parameter-algorithm.md` 为准。
