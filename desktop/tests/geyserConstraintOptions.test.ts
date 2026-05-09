import test from "node:test";
import assert from "node:assert/strict";

import {
  buildGeyserOptionAvailability,
  collectSiblingSelectedGeysers,
  findFirstAvailableGeyser,
  buildSectionGeyserOptionAvailability,
  findFirstAvailableGeyserForSection,
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

test("required section blocks geysers that already have count or distance semantics", () => {
  const availability = buildSectionGeyserOptionAvailability({
    section: "required",
    geyserKeys: ["steam", "hot_water", "salt_water", "chlorine_gas"],
    constraints: {
      required: [],
      forbidden: [],
      distance: [{ geyser: "hot_water", minDist: 0, maxDist: 80 }],
      count: [{ geyser: "salt_water", minCount: 1, maxCount: 2 }],
    },
  });

  assert.deepEqual(availability, {
    steam: null,
    hot_water: "已设置距离规则",
    salt_water: "已设置“必须包含”",
    chlorine_gas: null,
  });
});

test("count section allows building count plus distance but blocks required and forbidden", () => {
  const availability = buildSectionGeyserOptionAvailability({
    section: "count",
    geyserKeys: ["steam", "hot_water", "salt_water", "chlorine_gas"],
    constraints: {
      required: [{ geyser: "steam" }],
      forbidden: [{ geyser: "hot_water" }],
      distance: [{ geyser: "salt_water", minDist: 0, maxDist: 80 }],
      count: [],
    },
  });

  assert.deepEqual(availability, {
    steam: "已设置“必须包含”",
    hot_water: "已设置“必须排除”",
    salt_water: null,
    chlorine_gas: null,
  });
});

test("distance section blocks forbidden and required but allows existing count semantics", () => {
  const availability = buildSectionGeyserOptionAvailability({
    section: "distance",
    geyserKeys: ["steam", "hot_water", "salt_water", "chlorine_gas"],
    constraints: {
      required: [{ geyser: "steam" }],
      forbidden: [{ geyser: "hot_water" }],
      distance: [],
      count: [{ geyser: "salt_water", minCount: 1, maxCount: 2 }],
    },
  });

  assert.deepEqual(availability, {
    steam: "已设置“必须包含”",
    hot_water: "已设置“必须排除”",
    salt_water: null,
    chlorine_gas: null,
  });
});

test("findFirstAvailableGeyserForSection skips geysers that are invalid for the target section", () => {
  const first = findFirstAvailableGeyserForSection({
    section: "count",
    geyserKeys: ["steam", "hot_water", "salt_water"],
    constraints: {
      required: [{ geyser: "steam" }],
      forbidden: [{ geyser: "hot_water" }],
      distance: [{ geyser: "salt_water", minDist: 0, maxDist: 80 }],
      count: [],
    },
  });

  assert.equal(first, "salt_water");
});
