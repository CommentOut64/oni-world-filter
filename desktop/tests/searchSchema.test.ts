import test from "node:test";
import assert from "node:assert/strict";

import {
  createSearchSchema,
  getDefaultAllowLowPerfForCpuMode,
  toSearchDraft,
} from "../src/features/search/searchSchema.ts";

const schema = createSearchSchema({ worldTypeMax: 20, mixingMax: 48828124 });

function buildValidFormValues() {
  return {
    worldType: 13,
    mixing: 625,
    seedStart: 100000,
    seedEnd: 120000,
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

test("toSearchDraft disables warmup for desktop balanced mode", () => {
  const draft = toSearchDraft(buildValidFormValues());

  assert.deepEqual(draft.cpu, {
    mode: "balanced",
    allowSmt: true,
    allowLowPerf: false,
    placement: "strict",
  });
});

test("toSearchDraft enables low performance cores for desktop turbo mode", () => {
  const draft = toSearchDraft({
    ...buildValidFormValues(),
    cpuMode: "turbo",
  });

  assert.deepEqual(draft.cpu, {
    mode: "turbo",
    allowSmt: true,
    allowLowPerf: true,
    placement: "strict",
  });
});

test("toSearchDraft forces low performance cores on for turbo mode", () => {
  const draft = toSearchDraft({
    ...buildValidFormValues(),
    cpuMode: "turbo",
    cpuAllowLowPerf: false,
  });

  assert.equal(draft.cpu.allowLowPerf, true);
});

test("cpu mode default clears low performance cores for balanced mode", () => {
  assert.equal(getDefaultAllowLowPerfForCpuMode("turbo"), true);
  assert.equal(getDefaultAllowLowPerfForCpuMode("balanced"), false);
});

test("toSearchDraft preserves manual low performance core selection for balanced mode", () => {
  const draft = toSearchDraft({
    ...buildValidFormValues(),
    cpuMode: "balanced",
    cpuAllowLowPerf: true,
  });

  assert.equal(draft.cpu.allowLowPerf, true);
});

test("search schema rejects legacy custom cpu mode", () => {
  const result = schema.safeParse({
    ...buildValidFormValues(),
    cpuMode: "custom",
  });

  assert.equal(result.success, false);
  if (result.success) {
    return;
  }

  assert.equal(result.error.issues[0]?.path.join("."), "cpuMode");
});

test("search schema treats blank world selection as required instead of NaN type error", () => {
  const result = schema.safeParse({
    ...buildValidFormValues(),
    worldType: undefined,
  });

  assert.equal(result.success, false);
  if (result.success) {
    return;
  }

  assert.deepEqual(
    result.error.issues.map((issue) => ({
      path: issue.path.join("."),
      message: issue.message,
    })),
    [
      {
        path: "worldType",
        message: "请选择具体世界",
      },
    ]
  );
});

test("toSearchDraft keeps unified cpu surface for turbo mode", () => {
  const draft = toSearchDraft({
    ...buildValidFormValues(),
    cpuMode: "turbo",
  });

  assert.equal(Object.keys(draft.cpu).sort().join(","), "allowLowPerf,allowSmt,mode,placement");
});
