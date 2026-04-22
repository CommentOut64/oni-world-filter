import test from "node:test";
import assert from "node:assert/strict";

import type { MixingSlotMeta, WorldOption } from "../src/lib/contracts.ts";
import {
  applyChildMode,
  applyPackageMode,
  classifyWorld,
  findCategoryForWorld,
  getCategoryForWorld,
  getPackageMode,
  groupMixingSlots,
  groupWorldsByCategory,
  isWorldTypeVisibleInCategory,
  levelToUiMode,
  uiModeToLevel,
} from "../src/features/search/worldParameterUi.ts";

const worlds: WorldOption[] = [
  { id: 0, code: "SNDST-A-" },
  { id: 12, code: "PRES-A-" },
  { id: 13, code: "V-SNDST-C-" },
  { id: 26, code: "V-PRES-C-" },
  { id: 32, code: "M-SWMP-C-" },
];

const mixingSlots: MixingSlotMeta[] = [
  { slot: 0, path: "DLC2_ID", type: "dlc", name: "The Frosty Planet Pack", description: "" },
  {
    slot: 1,
    path: "dlc2::worldMixing/CeresMixingSettings",
    type: "world",
    name: "Ceres Fragment",
    description: "",
  },
  {
    slot: 2,
    path: "dlc2::subworldMixing/IceCavesMixingSettings",
    type: "subworld",
    name: "Ice Cave Biome",
    description: "",
  },
  { slot: 5, path: "DLC3_ID", type: "dlc", name: "The Bionic Booster Pack", description: "" },
  { slot: 6, path: "DLC4_ID", type: "dlc", name: "The Prehistoric Planet Pack", description: "" },
  {
    slot: 7,
    path: "dlc4::worldMixing/PrehistoricMixingSettings",
    type: "world",
    name: "Relica Fragment",
    description: "",
  },
];

test("classifyWorld and getCategoryForWorld map current world codes to UI categories", () => {
  assert.equal(classifyWorld("SNDST-A-"), "baseAsteroid");
  assert.equal(classifyWorld("PRES-A-"), "baseAsteroid");
  assert.equal(classifyWorld("V-SNDST-C-"), "classicCluster");
  assert.equal(classifyWorld("V-PRES-C-"), "classicCluster");
  assert.equal(classifyWorld("M-SWMP-C-"), "moonletCluster");

  const grouped = groupWorldsByCategory(worlds);
  assert.deepEqual(
    grouped.baseAsteroid.map((item) => item.id),
    [0]
  );
  assert.deepEqual(
    grouped.classicCluster.map((item) => item.id),
    [13]
  );
  assert.deepEqual(
    grouped.moonletCluster.map((item) => item.id),
    [32]
  );

  assert.equal(
    grouped.baseAsteroid.some((item) => item.code === "PRES-A-"),
    false
  );
  assert.equal(
    grouped.classicCluster.some((item) => item.code === "V-PRES-C-"),
    false
  );

  assert.equal(getCategoryForWorld(worlds, 32), "moonletCluster");
});

test("world selector helpers only sync category from valid selection and clear cross-category values", () => {
  assert.equal(findCategoryForWorld(worlds, 13), "classicCluster");
  assert.equal(findCategoryForWorld(worlds, Number.NaN), null);
  assert.equal(findCategoryForWorld(worlds, 999), null);

  assert.equal(isWorldTypeVisibleInCategory(worlds, 13, "classicCluster"), true);
  assert.equal(isWorldTypeVisibleInCategory(worlds, 13, "moonletCluster"), false);
  assert.equal(isWorldTypeVisibleInCategory(worlds, 32, "moonletCluster"), true);
  assert.equal(isWorldTypeVisibleInCategory(worlds, Number.NaN, "moonletCluster"), false);
});

test("groupMixingSlots groups package slots with child world/subworld slots", () => {
  const groups = groupMixingSlots(mixingSlots);

  assert.equal(groups.length, 3);
  assert.equal(groups[0].packageSlot.path, "DLC2_ID");
  assert.deepEqual(
    groups[0].children.map((item) => item.slot),
    [1, 2]
  );
  assert.equal(groups[1].packageSlot.path, "DLC3_ID");
  assert.deepEqual(groups[1].children, []);
  assert.equal(groups[2].packageSlot.path, "DLC4_ID");
  assert.deepEqual(
    groups[2].children.map((item) => item.slot),
    [7]
  );
});

test("mixing ui mode collapses level 3 and 4 into normal mode", () => {
  assert.equal(levelToUiMode(0), "off");
  assert.equal(levelToUiMode(1), "normal");
  assert.equal(levelToUiMode(2), "guaranteed");
  assert.equal(levelToUiMode(3), "normal");
  assert.equal(levelToUiMode(4), "normal");

  assert.equal(uiModeToLevel("off"), 0);
  assert.equal(uiModeToLevel("normal"), 1);
  assert.equal(uiModeToLevel("guaranteed"), 2);
});

test("package mode and child mode updates preserve checkbox-first behavior", () => {
  const groups = groupMixingSlots(mixingSlots);
  const frosty = groups[0];
  let levels = new Array(11).fill(0);

  levels = applyPackageMode(levels, frosty, "normal");
  assert.equal(levels[0], 1);
  assert.equal(getPackageMode(levels, frosty), "normal");

  levels = applyChildMode(levels, 1, "guaranteed");
  assert.equal(levels[1], 2);
  assert.equal(levelToUiMode(levels[1]), "guaranteed");

  levels = applyChildMode(levels, 1, "off");
  assert.equal(levels[1], 0);

  levels[2] = 4;
  assert.equal(levelToUiMode(levels[2]), "normal");

  levels = applyPackageMode(levels, frosty, "off");
  assert.equal(levels[0], 0);
  assert.equal(levels[1], 0);
  assert.equal(levels[2], 0);
});
