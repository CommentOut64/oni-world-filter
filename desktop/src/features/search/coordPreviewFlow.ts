import type {
  CoordPreviewEvent,
  PreviewPayload,
  SearchMatchSummary,
} from "../../lib/contracts.ts";
import { buildSearchMatchSummaryFromPreview } from "../../lib/searchMatchSummary.ts";

export interface CoordPreviewFlowDeps {
  loadPreviewByCoord: (coord: string) => Promise<CoordPreviewEvent>;
  openDirectCoordResult: (match: SearchMatchSummary) => void;
  primeResolvedPreview: (match: SearchMatchSummary, preview: PreviewPayload) => void;
  setError: (message: string) => void;
  openResults: () => void;
}

function formatCoordPreviewError(error: unknown): string {
  if (error instanceof Error) {
    return error.message;
  }
  return String(error);
}

export async function runCoordPreviewFlow(
  deps: CoordPreviewFlowDeps,
  coord: string
): Promise<void> {
  try {
    const result = await deps.loadPreviewByCoord(coord);
    const match = buildSearchMatchSummaryFromPreview({
      coord: result.coord,
      worldType: result.worldType,
      seed: result.seed,
      mixing: result.mixing,
      summary: result.preview.summary,
    });
    deps.openDirectCoordResult(match);
    deps.primeResolvedPreview(match, result.preview);
    deps.openResults();
  } catch (error) {
    deps.setError(formatCoordPreviewError(error));
  }
}
