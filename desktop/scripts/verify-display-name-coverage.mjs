import assert from "node:assert/strict";
import fs from "node:fs";

import {
  GEYSER_DISPLAY_NAMES,
  MIXING_SLOT_DISPLAY_NAMES,
  WORLD_DISPLAY_NAMES,
} from "../src/lib/displayNames.ts";

function extractConstArray(source, constName) {
  const marker = `const ${constName} = [`;
  const start = source.indexOf(marker);
  if (start < 0) {
    throw new Error(`未找到常量数组: ${constName}`);
  }
  const end = source.indexOf("] as const;", start);
  if (end < 0) {
    throw new Error(`常量数组缺少结束标记: ${constName}`);
  }
  const segment = source.slice(start + marker.length, end);
  return [...segment.matchAll(/"([^"]+)"/g)].map((item) => item[1]);
}

const searchCatalogSource = fs.readFileSync("desktop/src/lib/searchCatalog.ts", "utf8");
const fallbackWorldCodes = extractConstArray(searchCatalogSource, "FALLBACK_WORLD_CODES");
const fallbackGeyserIds = extractConstArray(searchCatalogSource, "FALLBACK_GEYSER_IDS");
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
