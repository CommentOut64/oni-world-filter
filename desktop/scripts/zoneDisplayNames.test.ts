import test from "node:test";
import assert from "node:assert/strict";

import { ZONE_TYPE_DISPLAY_NAMES } from "../src/lib/displayNames.ts";

test("zone type display names follow the legacy frontend biome labels where they existed", () => {
  const legacyLabels = new Map<number, { zh: string; en: string }>([
    [0, { zh: "苔原生态", en: "Tundra Biome" }],
    [2, { zh: "湿地生态", en: "Marsh Biome" }],
    [3, { zh: "砂岩生态", en: "Sandstone Biome" }],
    [4, { zh: "丛林生态", en: "Jungle Biome" }],
    [5, { zh: "岩浆生态", en: "Magma Biome" }],
    [6, { zh: "油质生态", en: "Oily Biome" }],
    [7, { zh: "太空生态", en: "Space Biome" }],
    [8, { zh: "海洋生态", en: "Ocean Biome" }],
    [9, { zh: "铁锈生态", en: "Rust Biome" }],
    [10, { zh: "森林生态", en: "Forest Biome" }],
    [11, { zh: "辐射生态", en: "Radioactive Biome" }],
    [12, { zh: "沼泽生态", en: "Swampy Biome" }],
    [13, { zh: "荒地生态", en: "Wasteland Biome" }],
    [15, { zh: "金属生态", en: "Metallic Biome" }],
    [16, { zh: "岩漠生态", en: "Barren Biome" }],
    [17, { zh: "海牛生态", en: "Moo Biome" }],
    [18, { zh: "冰窟生态", en: "Ice Cave Biome" }],
    [19, { zh: "冷池生态", en: "Cool Pool Biome" }],
    [20, { zh: "花蜜生态", en: "Nectar Biome" }],
    [21, { zh: "花园生态", en: "Garden Biome" }],
    [22, { zh: "寒羽生态", en: "Feather Biome" }],
    [23, { zh: "险沼生态", en: "Wetlands Biome" }],
  ]);

  for (const [index, expected] of legacyLabels) {
    assert.deepEqual(ZONE_TYPE_DISPLAY_NAMES[index], expected);
  }
});
