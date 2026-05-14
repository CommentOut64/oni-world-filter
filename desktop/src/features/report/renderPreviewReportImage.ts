import type { DesktopThemeMode } from "../../app/antdTheme";
import type { GeyserOption, PreviewPayload } from "../../lib/contracts.ts";
import type { SnapshotPreviewSceneRequest } from "../preview/OffscreenPreviewStage.tsx";
import { LABEL_FONT_PX } from "../preview/previewLabelLayout.ts";
import { toPreviewViewModel } from "../preview/previewModel.ts";
import type { ViewportState } from "../preview/viewport.ts";

export const REPORT_IMAGE_WIDTH = 4000;
const REPORT_HORIZONTAL_PADDING = 24;
const REPORT_STAGE_BASELINE_WIDTH = 560;
const REPORT_LABEL_CHAR_WIDTH = 0.55;
const REPORT_LABEL_PADDING = 4;
const REPORT_REGION_LABEL_HALF_WIDTH = 40;
const REPORT_REGION_LABEL_MAX_SHIFT_X = 25;
const REPORT_REGION_LABEL_MAX_SHIFT_Y = 20;

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

function buildReportLabelSafeInsets(
  preview: PreviewPayload,
  geysers: readonly GeyserOption[],
  stageWidth: number
): { left: number; right: number; top: number; bottom: number } {
  const model = toPreviewViewModel(preview, geysers);
  const reportVisualScale = stageWidth / REPORT_STAGE_BASELINE_WIDTH;
  const scaledFontPx = LABEL_FONT_PX * reportVisualScale;
  const maxInlineLabelChars = model.labelCandidates.reduce((max, label) => {
    if (label.kind === "region") {
      return max;
    }
    return Math.max(max, label.text.length);
  }, 0);

  // 报告导出会把 label 渲染到 world rect 外，需按同一套视觉尺寸预留安全边距。
  const regionHorizontalOverflow =
    (REPORT_REGION_LABEL_HALF_WIDTH + REPORT_REGION_LABEL_MAX_SHIFT_X) * reportVisualScale +
    REPORT_LABEL_PADDING;
  const regionVerticalOverflow =
    (LABEL_FONT_PX / 2 + REPORT_REGION_LABEL_MAX_SHIFT_Y) * reportVisualScale + REPORT_LABEL_PADDING;
  const inlineRightOverflow =
    reportVisualScale +
    maxInlineLabelChars * scaledFontPx * REPORT_LABEL_CHAR_WIDTH +
    REPORT_LABEL_PADDING;
  const inlineBottomOverflow = reportVisualScale + scaledFontPx + REPORT_LABEL_PADDING;

  return {
    left: Math.ceil(regionHorizontalOverflow),
    right: Math.ceil(Math.max(regionHorizontalOverflow, inlineRightOverflow)),
    top: Math.ceil(regionVerticalOverflow),
    bottom: Math.ceil(Math.max(regionVerticalOverflow, inlineBottomOverflow)),
  };
}

export function buildReportStageLayout(
  preview: PreviewPayload,
  geysers: readonly GeyserOption[],
  width = REPORT_IMAGE_WIDTH
): ReportStageLayout {
  const stageWidth = normalizeStageWidth(width);
  const worldWidth = Math.max(1, preview.summary.worldSize.w);
  const worldHeight = Math.max(1, preview.summary.worldSize.h);
  const safeInsets = buildReportLabelSafeInsets(preview, geysers, stageWidth);
  const scale =
    Math.max(1, stageWidth - REPORT_HORIZONTAL_PADDING * 2 - safeInsets.left - safeInsets.right) / worldWidth;

  return {
    stageWidth,
    stageHeight: Math.max(1, Math.ceil(safeInsets.top + worldHeight * scale + safeInsets.bottom)),
    viewport: {
      scale,
      x: REPORT_HORIZONTAL_PADDING + safeInsets.left,
      y: safeInsets.top,
    },
  };
}

export async function renderPreviewReportImage(
  request: RenderPreviewReportImageRequest,
  deps: RenderPreviewReportImageDeps = DEFAULT_DEPS
): Promise<string> {
  const layout = buildReportStageLayout(request.preview, request.geysers, request.width);

  return await deps.snapshotPreviewScene({
    preview: request.preview,
    geysers: request.geysers,
    themeMode: request.themeMode,
    stageWidth: layout.stageWidth,
    stageHeight: layout.stageHeight,
    viewport: layout.viewport,
    showBoundaries: true,
    showBiomes: true,
    showGeysers: true,
    selectedGeyserIndex: null,
  });
}
