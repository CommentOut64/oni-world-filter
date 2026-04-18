import { formatGeyserNameFromSummary } from "../../lib/displayResolvers";
import type { GeyserOption, GeyserSummary } from "../../lib/contracts";

interface GeyserCountItem {
  label: string;
  count: number;
}

function collectGeyserCounts(
  geysers: readonly GeyserSummary[],
  geyserOptions: readonly GeyserOption[]
): GeyserCountItem[] {
  const counts = new Map<string, GeyserCountItem>();
  for (const geyser of geysers) {
    const label = formatGeyserNameFromSummary(geyser, geyserOptions);
    const current = counts.get(label);
    if (current) {
      current.count += 1;
      continue;
    }
    counts.set(label, { label, count: 1 });
  }
  return [...counts.values()];
}

export function formatGeyserCountSummary(
  geysers: readonly GeyserSummary[],
  geyserOptions: readonly GeyserOption[],
  maxItems = 4
): string {
  if (!geysers.length) {
    return "-";
  }

  const items = collectGeyserCounts(geysers, geyserOptions);
  const visibleItems = items.slice(0, Math.max(1, maxItems));
  const summary = visibleItems
    .map((item) => (item.count > 1 ? `${item.label} x${item.count}` : item.label))
    .join(", ");

  if (items.length <= visibleItems.length) {
    return summary;
  }

  return `${summary}，等 ${items.length} 种`;
}
