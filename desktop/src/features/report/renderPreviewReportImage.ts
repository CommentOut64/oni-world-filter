import type { DesktopThemeMode } from "../../app/antdTheme";
import type { GeyserOption, PreviewPayload } from "../../lib/contracts.ts";
import type { SnapshotPreviewSceneRequest } from "../preview/OffscreenPreviewStage.tsx";
import type { ViewportState } from "../preview/viewport.ts";

export const REPORT_IMAGE_WIDTH = 4000;
const REPORT_HORIZONTAL_PADDING = 24;

export interface ReportStageLayout {
  stageWidth: number;
  stageHeight: number;
  viewport: ViewportState;
}

export interface RenderPreviewReportImageRequest {
  preview: PreviewPayload;
  geysers: readonly GeyserOption[];
  themeMode: DesktopThemeMode;
  width?: number;
}

export interface RenderPreviewReportImageDeps {
  snapshotPreviewScene: (request: SnapshotPreviewSceneRequest) => Promise<string>;
}

const DEFAULT_DEPS: RenderPreviewReportImageDeps = {
  snapshotPreviewScene: async (request) => {
    const module = await import("../preview/OffscreenPreviewStage.tsx");
    return await module.snapshotPreviewSceneToDataUrl(request);
  },
};

function normalizeStageWidth(width: number | undefined): number {
  if (!Number.isFinite(width)) {
    return REPORT_IMAGE_WIDTH;
  }
  return Math.max(REPORT_HORIZONTAL_PADDING * 2 + 1, Math.floor(width ?? REPORT_IMAGE_WIDTH));
}

export function buildReportStageLayout(
  preview: PreviewPayload,
  width = REPORT_IMAGE_WIDTH
): ReportStageLayout {
  const stageWidth = normalizeStageWidth(width);
  const worldWidth = Math.max(1, preview.summary.worldSize.w);
  const worldHeight = Math.max(1, preview.summary.worldSize.h);
  const scale = (stageWidth - REPORT_HORIZONTAL_PADDING * 2) / worldWidth;

  return {
    stageWidth,
    stageHeight: Math.max(1, Math.ceil(worldHeight * scale)),
    viewport: {
      scale,
      x: (stageWidth - worldWidth * scale) / 2,
      y: 0,
    },
  };
}

export async function renderPreviewReportImage(
  request: RenderPreviewReportImageRequest,
  deps: RenderPreviewReportImageDeps = DEFAULT_DEPS
): Promise<string> {
  const layout = buildReportStageLayout(request.preview, request.width);

  return await deps.snapshotPreviewScene({
    preview: request.preview,
    geysers: request.geysers,
    themeMode: request.themeMode,
    stageWidth: layout.stageWidth,
    stageHeight: layout.stageHeight,
    viewport: layout.viewport,
    showBoundaries: true,
    showLabels: true,
    showGeysers: true,
    selectedGeyserIndex: null,
  });
}
