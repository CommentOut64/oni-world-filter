import { buildGeyserConstraintState } from "./geyserConstraintStateMachine.ts";

const WORLD_DISABLED_REASON = "当前世界不可生成";

export interface GeyserRowValue {
  geyser: string;
}

export interface GeyserOptionAvailabilityInput {
  geyserKeys: readonly string[];
  currentValue?: string;
  blockers?: ReadonlyArray<{
    keys: ReadonlySet<string>;
    reason: string;
  }>;
  worldDisabledKeys?: ReadonlySet<string>;
}

export type GeyserConstraintSection = "required" | "forbidden" | "distance" | "count";

export interface GeyserConstraintSectionAvailabilityInput {
  section: GeyserConstraintSection;
  geyserKeys: readonly string[];
  constraints: {
    required: readonly GeyserRowValue[];
    forbidden: readonly GeyserRowValue[];
    distance: readonly { geyser: string; minDist: number; maxDist: number }[];
    count: readonly { geyser: string; minCount: number; maxCount: number }[];
  };
  currentValue?: string;
  worldDisabledKeys?: ReadonlySet<string>;
}

export function collectSiblingSelectedGeysers(
  rows: readonly GeyserRowValue[],
  excludeIndex?: number
): Set<string> {
  const selected = new Set<string>();
  rows.forEach((row, index) => {
    if (index === excludeIndex) {
      return;
    }
    if (!row.geyser) {
      return;
    }
    selected.add(row.geyser);
  });
  return selected;
}

export function buildGeyserOptionAvailability({
  geyserKeys,
  currentValue = "",
  blockers,
  worldDisabledKeys,
}: GeyserOptionAvailabilityInput): Record<string, string | null> {
  const availability: Record<string, string | null> = {};
  for (const key of geyserKeys) {
    if (key === currentValue) {
      availability[key] = null;
      continue;
    }
    if (worldDisabledKeys?.has(key)) {
      availability[key] = WORLD_DISABLED_REASON;
      continue;
    }
    if (blockers) {
      const blocker = blockers.find((entry) => entry.keys.has(key));
      if (blocker) {
        availability[key] = blocker.reason;
        continue;
      }
    }
    availability[key] = null;
  }
  return availability;
}

export function findFirstAvailableGeyser(input: GeyserOptionAvailabilityInput): string {
  const availability = buildGeyserOptionAvailability(input);
  return input.geyserKeys.find((key) => availability[key] === null) ?? "";
}

function getSectionBlockReason(
  section: GeyserConstraintSection,
  group: ReturnType<typeof buildGeyserConstraintState>["groups"][string] | undefined
): string | null {
  if (!group) {
    return null;
  }

  if (section === "required") {
    if (group.hasForbidden) {
      return "已设置“必须排除”";
    }
    if (group.countRule) {
      return "已设置“必须包含”";
    }
    if (group.distanceRule) {
      return "已设置距离规则";
    }
    if (group.hasRequired) {
      return "已设置“必须包含”";
    }
    return null;
  }

  if (section === "forbidden") {
    if (group.hasRequired) {
      return "已设置“必须包含”";
    }
    if (group.countRule) {
      return "已设置“必须包含”";
    }
    if (group.distanceRule) {
      return "已设置距离规则";
    }
    if (group.hasForbidden) {
      return "已设置“必须排除”";
    }
    return null;
  }

  if (section === "count") {
    if (group.hasForbidden) {
      return "已设置“必须排除”";
    }
    if (group.hasRequired) {
      return "已设置“必须包含”";
    }
    if (group.countRule) {
      return "已设置“必须包含”";
    }
    return null;
  }

  if (group.hasForbidden) {
    return "已设置“必须排除”";
  }
  if (group.hasRequired) {
    return "已设置“必须包含”";
  }
  if (group.distanceRule) {
    return "已设置距离规则";
  }
  return null;
}

export function buildSectionGeyserOptionAvailability({
  section,
  geyserKeys,
  constraints,
  currentValue = "",
  worldDisabledKeys,
}: GeyserConstraintSectionAvailabilityInput): Record<string, string | null> {
  const state = buildGeyserConstraintState({
    required: constraints.required,
    forbidden: constraints.forbidden,
    distance: constraints.distance.map((item) => ({
      geyser: item.geyser,
      minDist: Number(item.minDist ?? 0),
      maxDist: Number(item.maxDist ?? 0),
    })),
    count: constraints.count.map((item) => ({
      geyser: item.geyser,
      minCount: Number(item.minCount ?? 0),
      maxCount: Number(item.maxCount ?? 0),
    })),
  });

  const availability: Record<string, string | null> = {};
  geyserKeys.forEach((geyser) => {
    if (geyser === currentValue) {
      availability[geyser] = null;
      return;
    }
    if (worldDisabledKeys?.has(geyser)) {
      availability[geyser] = WORLD_DISABLED_REASON;
      return;
    }
    availability[geyser] = getSectionBlockReason(section, state.groups[geyser]);
  });

  return availability;
}

export function findFirstAvailableGeyserForSection(
  input: GeyserConstraintSectionAvailabilityInput
): string {
  const availability = buildSectionGeyserOptionAvailability(input);
  return input.geyserKeys.find((key) => availability[key] === null) ?? "";
}
