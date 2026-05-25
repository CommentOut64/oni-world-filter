import assert from "node:assert/strict";
import fs from "node:fs";

import {
  GEYSER_DISPLAY_NAMES,
  MIXING_SLOT_DISPLAY_NAMES,
  WORLD_DISPLAY_NAMES,
} from "../src/lib/displayNames.ts";
const fallbackData = JSON.parse(
  fs.readFileSync("desktop/src/lib/searchCatalogFallbackData.json", "utf8")
);
const fallbackWorldCodes = fallbackData.worlds.map((item) => item.code);
const fallbackGeyserIds = fallbackData.geysers.map((item) => item.key);
const expectedMixingSlotPaths = Object.keys(MIXING_SLOT_DISPLAY_NAMES);

for (const code of fallbackWorldCodes) {
  assert.ok(WORLD_DISPLAY_NAMES[code], `缺少 world code 映射: ${code}`);
}

for (const key of fallbackGeyserIds) {
  assert.ok(GEYSER_DISPLAY_NAMES[key], `缺少 geyser key 映射: ${key}`);
}

for (const path of expectedMixingSlotPaths) {
  assert.ok(MIXING_SLOT_DISPLAY_NAMES[path], `缺少 mixing slot 映射: ${path}`);
}

console.log("display-name coverage ok");
