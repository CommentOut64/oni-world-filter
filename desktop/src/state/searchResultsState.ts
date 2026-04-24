import type { SearchMatchSummary } from "../lib/contracts";

function isSameSearchResult(
  current: SearchMatchSummary,
  next: SearchMatchSummary
): boolean {
  return (
    current.seed === next.seed &&
    current.worldType === next.worldType &&
    current.mixing === next.mixing
  );
}

export function appendUniqueSearchResult(
  results: SearchMatchSummary[],
  next: SearchMatchSummary
): SearchMatchSummary[] {
  if (results.some((current) => isSameSearchResult(current, next))) {
    return results;
  }
  return [...results, next];
}
