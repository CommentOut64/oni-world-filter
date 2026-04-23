import type { PreviewPayload, SearchMatchEvent, SearchMatchSummary } from "./contracts.ts";

function roundNearestDistance(value: number): number {
  return Number(value.toFixed(1));
}

export function computeNearestDistanceFromSummary(
  start: SearchMatchEvent["summary"]["start"] | PreviewPayload["summary"]["start"],
  geysers: readonly SearchMatchEvent["summary"]["geysers"][number][]
): number | null {
  if (geysers.length === 0) {
    return null;
  }

  let minDistance = Number.POSITIVE_INFINITY;
  for (const geyser of geysers) {
    const dx = geyser.x - start.x;
    const dy = geyser.y - start.y;
    const value = Math.sqrt(dx * dx + dy * dy);
    if (value < minDistance) {
      minDistance = value;
    }
  }

  return Number.isFinite(minDistance) ? roundNearestDistance(minDistance) : null;
}

export function buildSearchMatchSummaryFromPreview(args: {
  coord: string;
  worldType: number;
  seed: number;
  mixing: number;
  summary: PreviewPayload["summary"];
}): SearchMatchSummary {
  return {
    seed: args.seed,
    worldType: args.worldType,
    mixing: args.mixing,
    coord: args.coord,
    traits: args.summary.traits,
    start: args.summary.start,
    worldSize: args.summary.worldSize,
    geysers: args.summary.geysers,
    nearestDistance: computeNearestDistanceFromSummary(args.summary.start, args.summary.geysers),
  };
}
