import { formatGeyserNameByKey } from "../../lib/displayResolvers.ts";

export interface SearchConstraintAlertItem {
  severity: "error" | "warning" | "info";
  message: string;
}

interface SearchConstraintPresentationInput {
  constraints: {
    required: readonly { geyser: string }[];
    forbidden: readonly { geyser: string }[];
    distance: readonly { geyser: string; minDist: number; maxDist: number }[];
    count: readonly { geyser: string; minCount: number; maxCount: number }[];
  };
  disabledGeyserKeys: ReadonlySet<string>;
}

function formatNames(geyserKeys: readonly string[]): string {
  return geyserKeys.map((item) => formatGeyserNameByKey(item)).join("、");
}

function collectDisabledGeysers<T extends { geyser: string }>(
  rows: readonly T[],
  disabledGeyserKeys: ReadonlySet<string>
): string[] {
  return rows
    .map((item) => item.geyser)
    .filter((geyser, index, list) => geyser && disabledGeyserKeys.has(geyser) && list.indexOf(geyser) === index);
}

export function buildWorldConstraintAlertItems({
  constraints,
  disabledGeyserKeys,
}: SearchConstraintPresentationInput): SearchConstraintAlertItem[] {
  if (disabledGeyserKeys.size === 0) {
    return [];
  }

  const blockingGeysers = [
    ...collectDisabledGeysers(constraints.required, disabledGeyserKeys),
    ...collectDisabledGeysers(constraints.distance, disabledGeyserKeys),
    ...collectDisabledGeysers(constraints.count, disabledGeyserKeys),
  ].filter((geyser, index, list) => list.indexOf(geyser) === index);
  const redundantForbiddenGeysers = collectDisabledGeysers(
    constraints.forbidden,
    disabledGeyserKeys
  ).filter((geyser) => !blockingGeysers.includes(geyser));

  const alerts: SearchConstraintAlertItem[] = [];
  if (blockingGeysers.length > 0) {
    alerts.push({
      severity: "error",
      message: `当前世界不会生成这些喷口：${formatNames(blockingGeysers)}。请修改相关条件后再开始搜索。`,
    });
  }
  if (redundantForbiddenGeysers.length > 0) {
    alerts.push({
      severity: "warning",
      message: `当前世界已经天然排除了这些喷口：${formatNames(redundantForbiddenGeysers)}。相关“必须排除”条件可以考虑删除。`,
    });
  }

  return alerts;
}
