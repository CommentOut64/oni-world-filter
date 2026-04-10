import { z } from "zod";

import type { SearchDraft } from "../../state/searchStore";

const nonNegativeInt = z.coerce.number().int().min(0);
const cpuModeSchema = z.enum(["balanced", "turbo", "custom"]);

const geyserItemSchema = z.object({
  geyser: z.string().min(1, "请选择喷口类型"),
});

const distanceRuleSchema = z.object({
  geyser: z.string().min(1, "请选择喷口类型"),
  minDist: z.coerce.number().min(0, "最小距离不能为负数"),
  maxDist: z.coerce.number().min(0, "最大距离不能为负数"),
});

export const searchSchema = z
  .object({
    worldType: z.coerce.number().int().min(0),
    mixing: nonNegativeInt.max(1_000_000),
    seedStart: nonNegativeInt,
    seedEnd: nonNegativeInt,
    threads: nonNegativeInt.max(512, "线程数不能超过 512"),
    cpuMode: cpuModeSchema,
    cpuAllowSmt: z.boolean(),
    cpuAllowLowPerf: z.boolean(),
    required: z.array(geyserItemSchema),
    forbidden: z.array(geyserItemSchema),
    distance: z.array(distanceRuleSchema),
  })
  .superRefine((value, ctx) => {
    if (value.seedStart > value.seedEnd) {
      ctx.addIssue({
        path: ["seedEnd"],
        code: z.ZodIssueCode.custom,
        message: "seedEnd 必须大于等于 seedStart",
      });
    }

    if (value.cpuMode === "custom" && value.threads < 1) {
      ctx.addIssue({
        path: ["threads"],
        code: z.ZodIssueCode.custom,
        message: "自定义模式下线程数必须 >= 1",
      });
    }

    for (let i = 0; i < value.distance.length; i += 1) {
      if (value.distance[i].minDist > value.distance[i].maxDist) {
        ctx.addIssue({
          path: ["distance", i, "maxDist"],
          code: z.ZodIssueCode.custom,
          message: "maxDist 必须大于等于 minDist",
        });
      }
    }

    const checkUnique = (
      list: { geyser: string }[],
      path: "required" | "forbidden" | "distance",
      message: string
    ) => {
      const set = new Set<string>();
      list.forEach((item, index) => {
        if (set.has(item.geyser)) {
          ctx.addIssue({
            path: [path, index, "geyser"],
            code: z.ZodIssueCode.custom,
            message,
          });
          return;
        }
        set.add(item.geyser);
      });
    };

    checkUnique(value.required, "required", "required 约束中存在重复喷口");
    checkUnique(value.forbidden, "forbidden", "forbidden 约束中存在重复喷口");
    checkUnique(value.distance, "distance", "distance 规则中存在重复喷口");
  });

export type SearchFormValues = z.infer<typeof searchSchema>;

export function toSearchDraft(values: SearchFormValues): SearchDraft {
  return {
    worldType: values.worldType,
    mixing: values.mixing,
    seedStart: values.seedStart,
    seedEnd: values.seedEnd,
    threads: values.cpuMode === "custom" ? values.threads : 0,
    cpu: {
      mode: values.cpuMode,
      workers: values.cpuMode === "custom" ? values.threads : 0,
      allowSmt: values.cpuAllowSmt,
      allowLowPerf: values.cpuAllowLowPerf,
      placement: "preferred",
      enableWarmup: true,
      enableAdaptiveDown: true,
      chunkSize: 64,
      progressInterval: 1000,
      sampleWindowMs: 2000,
      adaptiveMinWorkers: 1,
      adaptiveDropThreshold: 0.12,
      adaptiveDropWindows: 3,
      adaptiveCooldownMs: 8000,
    },
    constraints: {
      required: values.required.map((item) => item.geyser),
      forbidden: values.forbidden.map((item) => item.geyser),
      distance: values.distance.map((item) => ({
        geyser: item.geyser,
        minDist: item.minDist,
        maxDist: item.maxDist,
      })),
    },
  };
}

export function toSearchFormValues(draft: SearchDraft): SearchFormValues {
  return {
    worldType: draft.worldType,
    mixing: draft.mixing,
    seedStart: draft.seedStart,
    seedEnd: draft.seedEnd,
    threads: draft.cpu.mode === "custom" ? Math.max(1, draft.cpu.workers || draft.threads) : 0,
    cpuMode: draft.cpu.mode,
    cpuAllowSmt: draft.cpu.allowSmt,
    cpuAllowLowPerf: draft.cpu.allowLowPerf,
    required: draft.constraints.required.map((geyser) => ({ geyser })),
    forbidden: draft.constraints.forbidden.map((geyser) => ({ geyser })),
    distance: draft.constraints.distance.map((item) => ({
      geyser: item.geyser,
      minDist: item.minDist,
      maxDist: item.maxDist,
    })),
  };
}
