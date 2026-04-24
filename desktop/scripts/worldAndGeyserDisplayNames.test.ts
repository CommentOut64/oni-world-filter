import test from "node:test";
import assert from "node:assert/strict";

import { GEYSER_DISPLAY_NAMES, WORLD_DISPLAY_NAMES } from "../src/lib/displayNames.ts";

test("world display names follow the legacy frontend mapping for major world and cluster labels", () => {
  const expected = new Map<string, { zh: string; en: string }>([
    ["SNDST-A-", { zh: "类地星体", en: "Terra" }],
    ["OCAN-A-", { zh: "海洋星体", en: "Oceania" }],
    ["S-FRZ-", { zh: "冰霜星体", en: "Rime" }],
    ["LUSH-A-", { zh: "翠绿星体", en: "Verdante" }],
    ["FRST-A-", { zh: "乔木星体", en: "Arboria" }],
    ["VOLCA-", { zh: "火山星体", en: "Volcanea" }],
    ["BAD-A-", { zh: "荒芜之地", en: "The Badlands" }],
    ["HTFST-A-", { zh: "干热星体", en: "Aridio" }],
    ["OASIS-A-", { zh: "绿洲星体", en: "Oasisse" }],
    ["CER-A-", { zh: "谷神星", en: "Ceres" }],
    ["CERS-A-", { zh: "炸裂的谷神星", en: "Blasted Ceres" }],
    ["PRE-A-", { zh: "古迹星", en: "Relica" }],
    ["PRES-A-", { zh: "古迹啊啊啊嗷星", en: "RelicAAAAGH" }],
    ["V-SNDST-C-", { zh: "类地星群", en: "Terra Cluster" }],
    ["V-OCAN-C-", { zh: "海洋星群", en: "Oceania Cluster" }],
    ["V-SWMP-C-", { zh: "泥泞星群", en: "Squelchy Cluster" }],
    ["V-SFRZ-C-", { zh: "冰霜星群", en: "Rime Cluster" }],
    ["V-LUSH-C-", { zh: "翠绿星群", en: "Verdante Cluster" }],
    ["V-FRST-C-", { zh: "乔木星群", en: "Arboria Cluster" }],
    ["V-VOLCA-C-", { zh: "火山星群", en: "Volcanea Cluster" }],
    ["V-BAD-C-", { zh: "荒芜星群", en: "The Badlands Cluster" }],
    ["V-HTFST-C-", { zh: "干热星群", en: "Aridio Cluster" }],
    ["V-OASIS-C-", { zh: "绿洲星群", en: "Oasisse Cluster" }],
    ["V-CER-C-", { zh: "谷神星星群", en: "Ceres Cluster" }],
    ["V-CERS-C-", { zh: "炸裂的谷神星星群", en: "Blasted Ceres Cluster" }],
    ["V-PRE-C-", { zh: "古迹星星群", en: "Relica Cluster" }],
    ["V-PRES-C-", { zh: "古迹啊啊啊嗷星星群", en: "RelicAAAAGH Cluster" }],
    ["SNDST-C-", { zh: "砂土星群", en: "Terrania Cluster" }],
    ["PRE-C-", { zh: "小古迹星星群", en: "Relica Minor Cluster" }],
    ["CER-C-", { zh: "小谷神星星群", en: "Ceres Minor Cluster" }],
    ["FRST-C-", { zh: "绿叶星群", en: "Folia Cluster" }],
    ["SWMP-C-", { zh: "泥潭星群", en: "Quagmiris Cluster" }],
    ["M-SWMP-C-", { zh: "卫星星群 - 金属沼泽", en: "Moonlet Cluster - Metallic Swampy" }],
    ["M-BAD-C-", { zh: "卫星星群 - 荒凉", en: "Moonlet Cluster - The Desolands" }],
    ["M-FRZ-C-", { zh: "卫星星群 - 冰冻森林", en: "Moonlet Cluster - Frozen Forest" }],
    ["M-FLIP-C-", { zh: "卫星星群 - 倒置", en: "Moonlet Cluster - Flipped" }],
    ["M-RAD-C-", { zh: "卫星星群 - 放射性海洋", en: "Moonlet Cluster - Radioactive Ocean" }],
    ["M-CERS-C-", { zh: "卫星星群 - 谷神星地幔", en: "Moonlet Cluster - Ceres Mantle" }],
  ]);

  for (const [code, displayName] of expected) {
    assert.deepEqual(WORLD_DISPLAY_NAMES[code], displayName);
  }
});

test("geyser display names keep the legacy frontend spelling", () => {
  assert.deepEqual(GEYSER_DISPLAY_NAMES.slimy_po2, {
    zh: "含菌污氧喷孔",
    en: "InFectious Polluted Oxygen Vent",
  });
});
