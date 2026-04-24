import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

import { PLAYER_BIOME_DISPLAY_NAMES, ZONE_TYPE_DISPLAY_NAMES } from "../src/lib/displayNames.ts";
import { formatPlayerBiomeNameByZoneType } from "../src/lib/displayResolvers.ts";

const LEGACY_INDEX_SOURCE = readFileSync(new URL("../../src/index.tsx", import.meta.url), "utf8");
const LEGACY_LANGUAGE_SOURCE = readFileSync(
  new URL("../../src/jsUtils/language.ts", import.meta.url),
  "utf8"
);

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
    [16, { zh: "浮土生态", en: "Barren Biome" }],
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

test("player biome display names hide internal zone types and include missing player biomes", () => {
  assert.equal(
    PLAYER_BIOME_DISPLAY_NAMES.some((item) => item.en === "Crystal Caverns"),
    false
  );
  assert.equal(
    PLAYER_BIOME_DISPLAY_NAMES.some((item) => item.en === "Rocket Interior"),
    false
  );
  assert.deepEqual(
    PLAYER_BIOME_DISPLAY_NAMES
      .filter((item) =>
        ["Aquatic Biome", "Niobium Biome", "Regolith Biome"].includes(item.en)
      )
      .map((item) => item.zh),
    ["水域生态", "铌质生态", "岩漠生态"]
  );
});

test("legacy biome gallery hides internal entries and includes missing player biomes", () => {
  assert.doesNotMatch(LEGACY_INDEX_SOURCE, /"Crystal Caverns"/);
  assert.doesNotMatch(LEGACY_INDEX_SOURCE, /"Rocket Interior"/);
  assert.match(LEGACY_INDEX_SOURCE, /"Aquatic Biome"/);
  assert.match(LEGACY_INDEX_SOURCE, /"Niobium Biome"/);
  assert.match(LEGACY_INDEX_SOURCE, /"Regolith Biome"/);
});

test("legacy translations rename barren and include missing player biomes", () => {
  assert.match(LEGACY_LANGUAGE_SOURCE, /"Barren Biome": "浮土生态"/);
  assert.match(LEGACY_LANGUAGE_SOURCE, /"Aquatic Biome": "水域生态"/);
  assert.match(LEGACY_LANGUAGE_SOURCE, /"Niobium Biome": "铌质生态"/);
  assert.match(LEGACY_LANGUAGE_SOURCE, /"Regolith Biome": "岩漠生态"/);
});

test("desktop player biome resolver hides internal zone types and renames barren", () => {
  assert.equal(formatPlayerBiomeNameByZoneType(1), null);
  assert.equal(formatPlayerBiomeNameByZoneType(14), null);
  assert.equal(formatPlayerBiomeNameByZoneType(16), "浮土生态");
  assert.equal(formatPlayerBiomeNameByZoneType(21), "花园生态");
});
