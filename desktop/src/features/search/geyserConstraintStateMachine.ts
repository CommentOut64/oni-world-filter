import { COUNT_MAX_SENTINEL } from "./searchSchema.ts";

export interface RequiredConstraintRow {
  geyser: string;
}

export interface ForbiddenConstraintRow {
  geyser: string;
}

export interface DistanceConstraintRow {
  geyser: string;
  minDist: number;
  maxDist: number;
}

export interface CountConstraintRow {
  geyser: string;
  minCount: number;
  maxCount: number;
}

export interface GeyserConstraintStateInput {
  required: readonly RequiredConstraintRow[];
  forbidden: readonly ForbiddenConstraintRow[];
  distance: readonly DistanceConstraintRow[];
  count: readonly CountConstraintRow[];
}

export type GeyserConstraintStateKind =
  | "idle"
  | "required_only"
  | "forbidden_only"
  | "count_only"
  | "distance_only"
  | "count_with_distance"
  | "invalid_legacy";

export interface GeyserConstraintIssue {
  code: string;
  message: string;
  severity: "error";
}

export interface GeyserConstraintGroupState {
  geyser: string;
  state: GeyserConstraintStateKind;
  issues: GeyserConstraintIssue[];
  hasRequired: boolean;
  hasForbidden: boolean;
  countRule: CountConstraintRow | null;
  distanceRule: DistanceConstraintRow | null;
}

export interface GeyserConstraintStateIndex {
  groups: Record<string, GeyserConstraintGroupState>;
}

function createEmptyGroup(geyser: string): GeyserConstraintGroupState {
  return {
    geyser,
    state: "idle",
    issues: [],
    hasRequired: false,
    hasForbidden: false,
    countRule: null,
    distanceRule: null,
  };
}

function getOrCreateGroup(
  groups: Record<string, GeyserConstraintGroupState>,
  geyser: string
): GeyserConstraintGroupState {
  if (!groups[geyser]) {
    groups[geyser] = createEmptyGroup(geyser);
  }
  return groups[geyser];
}

function pushIssue(
  group: GeyserConstraintGroupState,
  code: string,
  message: string
): void {
  group.issues.push({
    code,
    message,
    severity: "error",
  });
}

function resolveGroupState(group: GeyserConstraintGroupState): GeyserConstraintStateKind {
  if (group.issues.length > 0) {
    return "invalid_legacy";
  }
  if (group.countRule && group.distanceRule) {
    return "count_with_distance";
  }
  if (group.hasRequired) {
    return "required_only";
  }
  if (group.hasForbidden) {
    return "forbidden_only";
  }
  if (group.countRule) {
    return "count_only";
  }
  if (group.distanceRule) {
    return "distance_only";
  }
  return "idle";
}

function validateGroup(group: GeyserConstraintGroupState): void {
  if (
    group.countRule &&
    (group.countRule.minCount <= 0 ||
      (group.countRule.maxCount !== COUNT_MAX_SENTINEL && group.countRule.maxCount <= 0))
  ) {
    pushIssue(group, "range.count_zero_not_allowed", "必须包含的数量不能设置为 0");
  }
  if (group.hasRequired && group.hasForbidden) {
    pushIssue(group, "conflict.required_forbidden", "不能同时要求出现和禁止出现");
  }
  if (group.hasRequired && group.countRule) {
    pushIssue(group, "conflict.required_with_count", "已经设置必须包含数量时不能再单独要求出现");
  }
  if (group.hasRequired && group.distanceRule) {
    pushIssue(group, "conflict.required_with_distance", "已经设置距离规则时不能再单独要求出现");
  }
  if (group.hasForbidden && group.countRule) {
    pushIssue(group, "conflict.forbidden_with_count", "已经禁止出现时不能再设置必须包含数量");
  }
  if (group.hasForbidden && group.distanceRule) {
    pushIssue(group, "conflict.forbidden_with_distance", "已经禁止出现时不能再设置距离规则");
  }
}

export function buildGeyserConstraintState(
  input: GeyserConstraintStateInput
): GeyserConstraintStateIndex {
  const groups: Record<string, GeyserConstraintGroupState> = {};

  input.required.forEach((item) => {
    if (!item.geyser) {
      return;
    }
    getOrCreateGroup(groups, item.geyser).hasRequired = true;
  });

  input.forbidden.forEach((item) => {
    if (!item.geyser) {
      return;
    }
    getOrCreateGroup(groups, item.geyser).hasForbidden = true;
  });

  input.count.forEach((item) => {
    if (!item.geyser) {
      return;
    }
    getOrCreateGroup(groups, item.geyser).countRule = item;
  });

  input.distance.forEach((item) => {
    if (!item.geyser) {
      return;
    }
    getOrCreateGroup(groups, item.geyser).distanceRule = item;
  });

  Object.values(groups).forEach((group) => {
    validateGroup(group);
    group.state = resolveGroupState(group);
  });

  return { groups };
}

export function getConstraintStateForGeyser(
  state: GeyserConstraintStateIndex,
  geyser: string
): GeyserConstraintGroupState | null {
  return state.groups[geyser] ?? null;
}
