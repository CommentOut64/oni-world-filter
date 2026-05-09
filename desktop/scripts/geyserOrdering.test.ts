import test from "node:test";
import assert from "node:assert/strict";

import type { GeyserOption } from "../src/lib/contracts.ts";
import {
  sortGeyserOptions,
  sortGeyserOptionsByAvailability,
  sortResolvedGeyserItems,
} from "../src/lib/geyserOrdering.ts";

const SAMPLE_GEYSERS: GeyserOption[] = [
  { id: 1, key: "printing_pod" },
  { id: 2, key: "steam" },
  { id: 3, key: "hot_water" },
  { id: 4, key: "big_volcano" },
  { id: 5, key: "slush_water" },
  { id: 6, key: "hot_steam" },
  { id: 7, key: "small_volcano" },
  { id: 8, key: "molten_gold" },
  { id: 9, key: "oil_reservoir" },
];

test("sortGeyserOptions orders enabled items before disabled, then category, then display name", () => {
  const sorted = sortGeyserOptions(SAMPLE_GEYSERS, new Set(["hot_water", "printing_pod"]));

  assert.deepEqual(
    sorted.map((item) => item.key),
    [
      "oil_reservoir",
      "slush_water",
      "steam",
      "hot_steam",
      "big_volcano",
      "small_volcano",
      "molten_gold",
      "hot_water",
      "printing_pod",
    ]
  );
});

test("sortGeyserOptions keeps facility items after volcanoes when availability is the same", () => {
  const sorted = sortGeyserOptions([
    { id: 1, key: "warp_sender" },
    { id: 2, key: "small_volcano" },
    { id: 3, key: "hot_co2" },
    { id: 4, key: "molten_copper" },
  ]);

  assert.deepEqual(sorted.map((item) => item.key), [
    "hot_co2",
    "small_volcano",
    "molten_copper",
    "warp_sender",
  ]);
});

test("sortGeyserOptionsByAvailability pushes mutually blocked items after enabled items", () => {
  const sorted = sortGeyserOptionsByAvailability(SAMPLE_GEYSERS, {
    printing_pod: "已设置“必须包含”",
    hot_water: "当前世界不可生成",
    steam: null,
    big_volcano: null,
    slush_water: null,
    hot_steam: "已设置距离规则",
    small_volcano: null,
    molten_gold: null,
    oil_reservoir: null,
  });

  assert.deepEqual(
    sorted.map((item) => item.key),
    [
      "oil_reservoir",
      "slush_water",
      "steam",
      "big_volcano",
      "small_volcano",
      "molten_gold",
      "hot_water",
      "hot_steam",
      "printing_pod",
    ]
  );
});

test("sortResolvedGeyserItems reuses the same category and display-name ordering for preview rows", () => {
  const sorted = sortResolvedGeyserItems(
    [
      { key: "small_volcano", label: "小型火山" },
      { key: "steam", label: "低温蒸汽喷孔" },
      { key: "hot_water", label: "清水泉" },
      { key: "molten_gold", label: "金火山" },
    ],
    (item) => ({
      geyserKey: item.key,
      name: item.label,
      disabled: false,
      stableKey: item.key,
    })
  );

  assert.deepEqual(
    sorted.map((item) => item.key),
    ["hot_water", "steam", "small_volcano", "molten_gold"]
  );
});
