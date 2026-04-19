import { z } from "zod";

import type { SearchDraft } from "../../state/searchStore";

const nonNegativeInt = z.coerce.number().int().min(0);
const cpuModeSchema = z.enum(["balanced", "turbo", "custom"]);
export const MIXING_SLOT_COUNT = 11;
export const MIXING_LEVEL_MIN = 0;
export const MIXING_LEVEL_MAX = 4;

function clampMixingLevel(value: number): number {
  if (!Number.isFinite(value)) {
    return 0;
  }
  const normalized = Math.trunc(value);
  if (normalized < MIXING_LEVEL_MIN) {
    return MIXING_LEVEL_MIN;
  }
  if (normalized > MIXING_LEVEL_MAX) {
    return MIXING_LEVEL_MAX;
  }
  return normalized;
}

export function decodeMixingToLevels(mixing: number, slotCount = MIXING_SLOT_COUNT): number[] {
  if (slotCount <= 0) {
    return [];
  }
  const levels = new Array<number>(slotCount).fill(0);
  let value = Number.isFinite(mixing) ? Math.max(0, Math.trunc(mixing)) : 0;
  for (let slot = slotCount - 1; slot >= 0; slot -= 1) {
    levels[slot] = clampMixingLevel(value % 5);
    value = Math.floor(value / 5);
  }
  return levels;
}

export function encodeMixingFromLevels(levels: readonly number[]): number {
  let value = 0;
  for (const level of levels) {
    value = value * 5 + clampMixingLevel(level);
  }
  return value;
}

const geyserItemSchema = z.object({
  geyser: z.string().min(1, "请选择喷口类型"),
});

const distanceRuleSchema = z.object({
  geyser: z.string().min(1, "请选择喷口类型"),
  minDist: z.coerce.number().min(0, "最小距离不能为负数"),
  maxDist: z.coerce.number().min(0, "最大距离不能为负数"),
});

const countRuleSchema = z.object({
  geyser: z.string().min(1, "请选择喷口类型"),
  minCount: z.coerce.number().int().min(0, "最小数量不能为负数"),
  maxCount: z.coerce.number().int().min(0, "最大数量不能为负数"),
});

export interface SearchSchemaOptions {
  worldTypeMax?: number;
  mixingMax?: number;
}

export function createSearchSchema(options?: SearchSchemaOptions) {
  const hasWorldTypeMax =
    typeof options?.worldTypeMax === "number" &&
    Number.isFinite(options.worldTypeMax) &&
    options.worldTypeMax >= 0;
  const hasMixingMax =
    typeof options?.mixingMax === "number" &&
    Number.isFinite(options.mixingMax) &&
    options.mixingMax >= 0;
  const worldTypeMax = hasWorldTypeMax ? (options?.worldTypeMax ?? 0) : 0;
  const mixingMax = hasMixingMax ? (options?.mixingMax ?? 0) : 0;
  const worldTypeSchema = hasWorldTypeMax
    ? z
        .coerce.number()
        .int()
        .min(0)
        .max(worldTypeMax, `worldType 不能超过 ${worldTypeMax}`)
    : z.coerce.number().int().min(0);
  const mixingSchema = hasMixingMax
    ? nonNegativeInt.max(mixingMax, `mixing 不能超过 ${mixingMax}`)
    : nonNegativeInt;

  return z
    .object({
      worldType: worldTypeSchema,
      mixing: mixingSchema,
      seedStart: nonNegativeInt,
      seedEnd: nonNegativeInt,
      threads: nonNegativeInt.max(512, "线程数不能超过 512"),
      cpuMode: cpuModeSchema,
      cpuAllowSmt: z.boolean(),
      cpuAllowLowPerf: z.boolean(),
      required: z.array(geyserItemSchema),
      forbidden: z.array(geyserItemSchema),
      distance: z.array(distanceRuleSchema),
      count: z.array(countRuleSchema),
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

      for (let i = 0; i < value.count.length; i += 1) {
        if (value.count[i].minCount > value.count[i].maxCount) {
          ctx.addIssue({
            path: ["count", i, "maxCount"],
            code: z.ZodIssueCode.custom,
            message: "maxCount 必须大于等于 minCount",
          });
        }
      }

      const checkUnique = (
        list: { geyser: string }[],
        path: "required" | "forbidden" | "distance" | "count",
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
      checkUnique(value.count, "count", "count 规则中存在重复喷口");

      const requiredSet = new Set(value.required.map((item) => item.geyser));
      value.forbidden.forEach((item, index) => {
        if (!requiredSet.has(item.geyser)) {
          return;
        }
        ctx.addIssue({
          path: ["forbidden", index, "geyser"],
          code: z.ZodIssueCode.custom,
          message: "同一喷口不能同时设置为 required 和 forbidden",
        });
      });

      const forbiddenSet = new Set(value.forbidden.map((item) => item.geyser));
      value.distance.forEach((item, index) => {
        if (!forbiddenSet.has(item.geyser)) {
          return;
        }
        ctx.addIssue({
          path: ["distance", index, "geyser"],
          code: z.ZodIssueCode.custom,
          message: "forbidden 喷口不能同时设置距离规则",
        });
      });
    });
}

export const searchSchema = createSearchSchema();

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
      count: values.count.map((item) => ({
        geyser: item.geyser,
        minCount: item.minCount,
        maxCount: item.maxCount,
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
    count: draft.constraints.count.map((item) => ({
      geyser: item.geyser,
      minCount: item.minCount,
      maxCount: item.maxCount,
    })),
  };
}
