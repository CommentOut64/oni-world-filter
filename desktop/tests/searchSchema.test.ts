import test from "node:test";
import assert from "node:assert/strict";

import {
  COUNT_MAX_SENTINEL,
  createSearchSchema,
  getDefaultAllowLowPerfForCpuMode,
  resolveCountAutoMax,
  toSearchDraft,
  toSearchFormValues,
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
      message: "同一个喷口不能同时出现在“必须包含”和“必须排除”里",
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
      message: "已经设置“必须排除”时，不能再设置距离规则",
    },
  ]);
});

test("search schema rejects count rule with zero lower bound", () => {
  const result = schema.safeParse({
    ...buildValidFormValues(),
    count: [{ geyser: "steam", minCount: 0, maxCount: 2 }],
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
        path: "count.0.minCount",
        message: "必须包含的数量至少为 1",
      },
    ]
  );
});

test("search schema rejects count rule with zero upper bound", () => {
  const result = schema.safeParse({
    ...buildValidFormValues(),
    count: [{ geyser: "steam", minCount: 1, maxCount: 0 }],
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
        path: "count.0.maxCount",
        message: "必须包含的数量至少为 1",
      },
    ]
  );
});

test("search schema accepts Max as the upper bound of must-include count", () => {
  const result = schema.safeParse({
    ...buildValidFormValues(),
    count: [{ geyser: "steam", minCount: 1, maxCount: COUNT_MAX_SENTINEL }],
  });

  assert.equal(result.success, true);
});

test("search schema does not report upper-lower inversion when must-include upper bound is Max", () => {
  const result = schema.safeParse({
    ...buildValidFormValues(),
    count: [{ geyser: "steam", minCount: 3, maxCount: COUNT_MAX_SENTINEL }],
  });

  assert.equal(result.success, true);
});

test("search schema rejects required geyser that also has a count rule", () => {
  const result = schema.safeParse({
    ...buildValidFormValues(),
    required: [{ geyser: "steam" }],
    count: [{ geyser: "steam", minCount: 1, maxCount: 2 }],
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
        path: "required.0.geyser",
        message: "这个喷口已经在“必须包含”里设置了数量，不要再单独添加旧的包含条件",
      },
    ]
  );
});

test("search schema rejects required geyser that also has a distance rule", () => {
  const result = schema.safeParse({
    ...buildValidFormValues(),
    required: [{ geyser: "steam" }],
    distance: [{ geyser: "steam", minDist: 0, maxDist: 80 }],
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
        path: "required.0.geyser",
        message: "已经设置距离规则时，不要再单独添加“必须包含”条件",
      },
    ]
  );
});

test("search schema rejects count rule on forbidden geyser", () => {
  const result = schema.safeParse({
    ...buildValidFormValues(),
    forbidden: [{ geyser: "steam" }],
    count: [{ geyser: "steam", minCount: 1, maxCount: 2 }],
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
        path: "count.0.geyser",
        message: "已经设置“必须排除”时，不能再设置“必须包含”",
      },
    ]
  );
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

test("toSearchFormValues migrates legacy required geysers into count rows with [1, Max]", () => {
  const values = toSearchFormValues({
    worldType: 13,
    mixing: 625,
    seedStart: 100000,
    seedEnd: 120000,
    cpu: {
      mode: "balanced",
      allowSmt: true,
      allowLowPerf: false,
      placement: "strict",
    },
    constraints: {
      required: ["steam", "hot_water"],
      forbidden: ["methane"],
      distance: [{ geyser: "steam", minDist: 0, maxDist: 80 }],
      count: [{ geyser: "hot_water", minCount: 2, maxCount: 3 }],
    },
  });

  assert.deepEqual(values.required, []);
  assert.deepEqual(values.count, [
    { geyser: "hot_water", minCount: 2, maxCount: 3 },
    { geyser: "steam", minCount: 1, maxCount: COUNT_MAX_SENTINEL },
  ]);
});

test("resolveCountAutoMax expands Max upper bounds using world profile limits", () => {
  const resolved = resolveCountAutoMax(
    {
      worldType: 13,
      mixing: 625,
      seedStart: 100000,
      seedEnd: 120000,
      cpu: {
        mode: "balanced",
        allowSmt: true,
        allowLowPerf: false,
        placement: "strict",
      },
      constraints: {
        required: [],
        forbidden: ["methane"],
        distance: [{ geyser: "steam", minDist: 0, maxDist: 80 }],
        count: [
          { geyser: "steam", minCount: 1, maxCount: COUNT_MAX_SENTINEL },
          { geyser: "hot_water", minCount: 2, maxCount: 3 },
        ],
      },
    },
    {
      steam: 4,
      hot_water: 3,
    }
  );

  assert.deepEqual(resolved.constraints.count, [
    { geyser: "steam", minCount: 1, maxCount: 4 },
    { geyser: "hot_water", minCount: 2, maxCount: 3 },
  ]);
});
