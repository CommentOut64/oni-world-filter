import {
  formatGeyserNameFromSummary,
  geyserKeyFromType,
} from "../../lib/displayResolvers";
import type { GeyserOption, GeyserSummary } from "../../lib/contracts";

interface GeyserCountItem {
  key: string | null;
  label: string;
  count: number;
  firstSeenIndex: number;
}

function collectGeyserCounts(
  geysers: readonly GeyserSummary[],
  geyserOptions: readonly GeyserOption[]
): GeyserCountItem[] {
  const counts = new Map<string, GeyserCountItem>();
  geysers.forEach((geyser, index) => {
    const label = formatGeyserNameFromSummary(geyser, geyserOptions);
    const current = counts.get(label);
    if (current) {
      current.count += 1;
      return;
    }
    counts.set(label, {
      key: geyser.id ?? geyserKeyFromType(geyser.type, geyserOptions),
      label,
      count: 1,
      firstSeenIndex: index,
    });
  });
  return [...counts.values()];
}

export function formatGeyserCountSummary(
  geysers: readonly GeyserSummary[],
  geyserOptions: readonly GeyserOption[],
  maxItems = 4,
  prioritizedGeyserKeys: readonly string[] = []
): string {
  if (!geysers.length) {
    return "-";
  }

  const priorityIndex = new Map<string, number>();
  prioritizedGeyserKeys.forEach((key, index) => {
    if (!priorityIndex.has(key)) {
      priorityIndex.set(key, index);
    }
  });

  const items = collectGeyserCounts(geysers, geyserOptions).sort((lhs, rhs) => {
    const leftPriority = lhs.key ? priorityIndex.get(lhs.key) ?? Number.POSITIVE_INFINITY : Number.POSITIVE_INFINITY;
    const rightPriority = rhs.key ? priorityIndex.get(rhs.key) ?? Number.POSITIVE_INFINITY : Number.POSITIVE_INFINITY;
    if (leftPriority !== rightPriority) {
      return leftPriority - rightPriority;
    }
    return lhs.firstSeenIndex - rhs.firstSeenIndex;
  });
  const visibleItems = items.slice(0, Math.max(1, maxItems));
  const summary = visibleItems
    .map((item) => (item.count > 1 ? `${item.label} x${item.count}` : item.label))
    .join(", ");

  if (items.length <= visibleItems.length) {
    return summary;
  }

  return `${summary}，等 ${items.length} 种`;
}
