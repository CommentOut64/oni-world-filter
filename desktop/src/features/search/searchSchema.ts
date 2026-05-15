import { z } from "zod";

import type { SearchDraft } from "../../state/searchStore";
const nonNegativeInt = z.coerce.number().int().min(0);
const cpuModeSchema = z.enum(["balanced", "turbo"]);
type SearchCpuMode = z.infer<typeof cpuModeSchema>;
export const MIXING_SLOT_COUNT = 11;
export const MIXING_LEVEL_MIN = 0;
export const MIXING_LEVEL_MAX = 4;
export const COUNT_MAX_SENTINEL = -1;

export function getDefaultAllowLowPerfForCpuMode(cpuMode: SearchCpuMode): boolean {
  return cpuMode === "turbo";
}

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

function isCountAutoMax(value: number): boolean {
  return value === COUNT_MAX_SENTINEL;
}

const countRuleSchema = z.object({
  geyser: z.string().min(1, "请选择喷口类型"),
  minCount: z.coerce.number().int().min(1, "必须包含的数量至少为 1"),
  maxCount: z.coerce
    .number()
    .int()
    .refine(
      (value) => isCountAutoMax(value) || value >= 1,
      "必须包含的数量至少为 1"
    ),
});

const traitRuleSchema = z.object({
  traitId: z.string().min(1, "请选择特质"),
  mode: z.enum(["required", "forbidden"]),
});

function normalizeBlankNumberInput(value: unknown): unknown {
  if (value === null || value === undefined || value === "") {
    return undefined;
  }
  if (typeof value === "number") {
    return Number.isNaN(value) ? undefined : value;
  }
  if (typeof value === "string") {
    const trimmed = value.trim();
    if (!trimmed) {
      return undefined;
    }
    const parsed = Number(trimmed);
    return Number.isNaN(parsed) ? value : parsed;
  }
  return value;
}

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
  const worldTypeNumberSchema = hasWorldTypeMax
    ? z
        .number({
          required_error: "请选择具体世界",
          invalid_type_error: "请选择具体世界",
        })
        .int()
        .min(0)
        .max(worldTypeMax, `worldType 不能超过 ${worldTypeMax}`)
    : z
        .number({
          required_error: "请选择具体世界",
          invalid_type_error: "请选择具体世界",
        })
        .int()
        .min(0);
  const worldTypeSchema = z.preprocess(
    normalizeBlankNumberInput,
    worldTypeNumberSchema
  );
  const mixingSchema = hasMixingMax
    ? nonNegativeInt.max(mixingMax, `mixing 不能超过 ${mixingMax}`)
    : nonNegativeInt;

  return z
    .object({
      worldType: worldTypeSchema,
      mixing: mixingSchema,
      seedStart: nonNegativeInt,
      seedEnd: nonNegativeInt,
      cpuMode: cpuModeSchema,
      cpuAllowSmt: z.boolean(),
      cpuAllowLowPerf: z.boolean(),
      required: z.array(geyserItemSchema),
      forbidden: z.array(geyserItemSchema),
      distance: z.array(distanceRuleSchema),
      count: z.array(countRuleSchema),
      traitRules: z.array(traitRuleSchema),
    })
    .superRefine((value, ctx) => {
      if (value.seedStart > value.seedEnd) {
        ctx.addIssue({
          path: ["seedEnd"],
          code: z.ZodIssueCode.custom,
          message: "seedEnd 必须大于等于 seedStart",
        });
      }

      for (let i = 0; i < value.distance.length; i += 1) {
        if (value.distance[i].minDist > value.distance[i].maxDist) {
          ctx.addIssue({
            path: ["distance", i, "maxDist"],
            code: z.ZodIssueCode.custom,
            message: "距离上限不能小于下限",
          });
        }
      }

      for (let i = 0; i < value.count.length; i += 1) {
        if (
          value.count[i].minCount >= 1 &&
          !isCountAutoMax(value.count[i].maxCount) &&
          value.count[i].maxCount >= 1 &&
          value.count[i].minCount > value.count[i].maxCount
        ) {
          ctx.addIssue({
            path: ["count", i, "maxCount"],
            code: z.ZodIssueCode.custom,
            message: "必须包含的最大数量不能小于最小数量",
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

      checkUnique(value.required, "required", "“必须包含”里有重复喷口");
      checkUnique(value.forbidden, "forbidden", "“必须排除”里有重复喷口");
      checkUnique(value.distance, "distance", "同一个喷口只能设置一条距离规则");
      checkUnique(value.count, "count", "同一个喷口只能在“必须包含”里设置一次");

      const requiredSet = new Set(value.required.map((item) => item.geyser));
      value.forbidden.forEach((item, index) => {
        if (!requiredSet.has(item.geyser)) {
          return;
        }
        ctx.addIssue({
          path: ["forbidden", index, "geyser"],
          code: z.ZodIssueCode.custom,
          message: "同一个喷口不能同时出现在“必须包含”和“必须排除”里",
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
          message: "已经设置“必须排除”时，不能再设置距离规则",
        });
      });

      const countSet = new Set(value.count.map((item) => item.geyser));
      value.required.forEach((item, index) => {
        if (!countSet.has(item.geyser)) {
          return;
        }
        ctx.addIssue({
          path: ["required", index, "geyser"],
          code: z.ZodIssueCode.custom,
          message: "这个喷口已经在“必须包含”里设置了数量，不要再单独添加旧的包含条件",
        });
      });

      const distanceSet = new Set(value.distance.map((item) => item.geyser));
      value.required.forEach((item, index) => {
        if (!distanceSet.has(item.geyser)) {
          return;
        }
        ctx.addIssue({
          path: ["required", index, "geyser"],
          code: z.ZodIssueCode.custom,
          message: "已经设置距离规则时，不要再单独添加“必须包含”条件",
        });
      });

      value.count.forEach((item, index) => {
        if (!forbiddenSet.has(item.geyser)) {
          return;
        }
        ctx.addIssue({
          path: ["count", index, "geyser"],
          code: z.ZodIssueCode.custom,
          message: "已经设置“必须排除”时，不能再设置“必须包含”",
        });
      });

      const seenRequiredTraits = new Set<string>();
      const seenForbiddenTraits = new Set<string>();
      value.traitRules.forEach((item, index) => {
        if (item.mode === "required") {
          if (seenRequiredTraits.has(item.traitId)) {
            ctx.addIssue({
              path: ["traitRules", index, "traitId"],
              code: z.ZodIssueCode.custom,
              message: "“主星特质”里有重复特质",
            });
            return;
          }
          seenRequiredTraits.add(item.traitId);
          return;
        }
        if (seenForbiddenTraits.has(item.traitId)) {
          ctx.addIssue({
            path: ["traitRules", index, "traitId"],
            code: z.ZodIssueCode.custom,
            message: "“主星特质”里有重复特质",
          });
          return;
        }
        seenForbiddenTraits.add(item.traitId);
      });

      value.traitRules.forEach((item, index) => {
        if (item.mode !== "forbidden" || !seenRequiredTraits.has(item.traitId)) {
          return;
        }
        ctx.addIssue({
          path: ["traitRules", index, "traitId"],
          code: z.ZodIssueCode.custom,
          message: "同一个主星特质不能同时设置“必须包含”和“必须排除”",
        });
      });
    });
}

export const searchSchema = createSearchSchema();

export type SearchFormValues = z.infer<typeof searchSchema>;

function splitTraitRules(
  traitRules: SearchFormValues["traitRules"]
): Pick<SearchDraft["constraints"], "requiredTraits" | "forbiddenTraits"> {
  const requiredTraits: string[] = [];
  const forbiddenTraits: string[] = [];
  traitRules.forEach((item) => {
    if (!item.traitId) {
      return;
    }
    if (item.mode === "required") {
      requiredTraits.push(item.traitId);
      return;
    }
    forbiddenTraits.push(item.traitId);
  });
  return {
    requiredTraits,
    forbiddenTraits,
  };
}

function mergeRequiredIntoCountRows(
  constraints: SearchDraft["constraints"]
): Array<{ geyser: string; minCount: number; maxCount: number }> {
  const rows = constraints.count.map((item) => ({
    geyser: item.geyser,
    minCount: item.minCount,
    maxCount: item.maxCount,
  }));
  const existing = new Set(rows.map((item) => item.geyser));
  constraints.required.forEach((geyser) => {
    if (!geyser || existing.has(geyser)) {
      return;
    }
    rows.push({
      geyser,
      minCount: 1,
      maxCount: COUNT_MAX_SENTINEL,
    });
  });
  return rows;
}

function mergeTraitRules(
  constraints: SearchDraft["constraints"]
): SearchFormValues["traitRules"] {
  return [
    ...constraints.requiredTraits.map((traitId) => ({
      traitId,
      mode: "required" as const,
    })),
    ...constraints.forbiddenTraits.map((traitId) => ({
      traitId,
      mode: "forbidden" as const,
    })),
  ];
}

export function resolveCountAutoMax(
  draft: SearchDraft,
  possibleMaxCountByType: Record<string, number>
): SearchDraft {
  return {
    ...draft,
    constraints: {
      ...draft.constraints,
      count: draft.constraints.count.map((item) => {
        if (!isCountAutoMax(item.maxCount)) {
          return item;
        }
        const resolvedMax = possibleMaxCountByType[item.geyser];
        if (!Number.isInteger(resolvedMax) || resolvedMax < 0) {
          throw new Error(`缺少喷口上界: ${item.geyser}`);
        }
        return {
          ...item,
          maxCount: resolvedMax,
        };
      }),
    },
  };
}

export function toSearchDraft(values: SearchFormValues): SearchDraft {
  const { requiredTraits, forbiddenTraits } = splitTraitRules(values.traitRules);
  return {
    worldType: values.worldType,
    mixing: values.mixing,
    seedStart: values.seedStart,
    seedEnd: values.seedEnd,
    cpu: {
      mode: values.cpuMode,
      allowSmt: values.cpuAllowSmt,
      allowLowPerf: values.cpuMode === "turbo" ? getDefaultAllowLowPerfForCpuMode(values.cpuMode) : values.cpuAllowLowPerf,
      placement: "strict",
    },
    constraints: {
      required: [],
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
      requiredTraits,
      forbiddenTraits,
    },
  };
}

export function toSearchFormValues(draft: SearchDraft): SearchFormValues {
  return {
    worldType: draft.worldType,
    mixing: draft.mixing,
    seedStart: draft.seedStart,
    seedEnd: draft.seedEnd,
    cpuMode: draft.cpu.mode,
    cpuAllowSmt: draft.cpu.allowSmt,
    cpuAllowLowPerf: draft.cpu.allowLowPerf,
    required: [],
    forbidden: draft.constraints.forbidden.map((geyser) => ({ geyser })),
    distance: draft.constraints.distance.map((item) => ({
      geyser: item.geyser,
      minDist: item.minDist,
      maxDist: item.maxDist,
    })),
    count: mergeRequiredIntoCountRows(draft.constraints),
    traitRules: mergeTraitRules(draft.constraints),
  };
}
