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
