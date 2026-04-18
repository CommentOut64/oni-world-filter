import test from "node:test";
import assert from "node:assert/strict";

import {
  buildGeyserOptionAvailability,
  collectSiblingSelectedGeysers,
  findFirstAvailableGeyser,
} from "../src/features/search/geyserConstraintOptions.ts";

test("collectSiblingSelectedGeysers excludes current row and empty values", () => {
  const selected = collectSiblingSelectedGeysers(
    [{ geyser: "steam" }, { geyser: "hot_water" }, { geyser: "" }],
    1
  );

  assert.deepEqual([...selected], ["steam"]);
});

test("buildGeyserOptionAvailability disables blocked keys but keeps current value selectable", () => {
  const availability = buildGeyserOptionAvailability({
    geyserKeys: ["steam", "hot_water", "big_volcano"],
    currentValue: "hot_water",
    blockers: [
      {
        keys: new Set(["hot_water"]),
        reason: "已在必须排除中",
      },
    ],
    worldDisabledKeys: new Set(["big_volcano"]),
  });

  assert.deepEqual(availability, {
    steam: null,
    hot_water: null,
    big_volcano: "当前世界不可生成",
  });
});

test("buildGeyserOptionAvailability uses blocker-specific reason in declaration order", () => {
  const availability = buildGeyserOptionAvailability({
    geyserKeys: ["steam", "hot_water", "big_volcano"],
    blockers: [
      {
        keys: new Set(["steam"]),
        reason: "已存在距离规则",
      },
      {
        keys: new Set(["hot_water", "steam"]),
        reason: "已在必须排除中",
      },
    ],
  });

  assert.deepEqual(availability, {
    steam: "已存在距离规则",
    hot_water: "已在必须排除中",
    big_volcano: null,
  });
});

test("findFirstAvailableGeyser skips blocked and world-disabled geysers", () => {
  const first = findFirstAvailableGeyser({
    geyserKeys: ["steam", "hot_water", "big_volcano"],
    blockers: [
      {
        keys: new Set(["steam"]),
        reason: "已存在距离规则",
      },
    ],
    worldDisabledKeys: new Set(["big_volcano"]),
  });

  assert.equal(first, "hot_water");
});
