import test from "node:test";
import assert from "node:assert/strict";

import { createSearchSchema } from "../src/features/search/searchSchema.ts";

const schema = createSearchSchema({ worldTypeMax: 20, mixingMax: 48828124 });

function buildValidFormValues() {
  return {
    worldType: 13,
    mixing: 625,
    seedStart: 100000,
    seedEnd: 120000,
    threads: 0,
    cpuMode: "balanced" as const,
    cpuAllowSmt: true,
    cpuAllowLowPerf: false,
    required: [] as { geyser: string }[],
    forbidden: [] as { geyser: string }[],
    distance: [] as { geyser: string; minDist: number; maxDist: number }[],
    count: [] as { geyser: string; minCount: number; maxCount: number }[],
  };
}

test("search schema rejects geyser that is both required and forbidden", () => {
  const result = schema.safeParse({
    ...buildValidFormValues(),
    required: [{ geyser: "hot_water" }],
    forbidden: [{ geyser: "hot_water" }],
  });

  assert.equal(result.success, false);
  if (result.success) {
    return;
  }

  const issues = result.error.issues.map((issue) => ({
    path: issue.path.join("."),
    message: issue.message,
  }));
  assert.deepEqual(issues, [
    {
      path: "forbidden.0.geyser",
      message: "同一喷口不能同时设置为 required 和 forbidden",
    },
  ]);
});

test("search schema rejects distance rule on forbidden geyser", () => {
  const result = schema.safeParse({
    ...buildValidFormValues(),
    forbidden: [{ geyser: "steam" }],
    distance: [{ geyser: "steam", minDist: 0, maxDist: 80 }],
  });

  assert.equal(result.success, false);
  if (result.success) {
    return;
  }

  const issues = result.error.issues.map((issue) => ({
    path: issue.path.join("."),
    message: issue.message,
  }));
  assert.deepEqual(issues, [
    {
      path: "distance.0.geyser",
      message: "forbidden 喷口不能同时设置距离规则",
    },
  ]);
});
