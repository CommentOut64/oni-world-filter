import { save } from "@tauri-apps/plugin-dialog";

import type {
  ExportReportPdfRequest,
  GeyserOption,
  SearchCatalog,
  SearchMatchSummary,
  WorldReportData,
  WorldReportEvent,
  WorldReportRequest,
} from "../../lib/contracts.ts";
import {
  exportReportPdf,
  getWorldReport,
  inTauriRuntime,
} from "../../lib/tauri.ts";
import { buildWorldReportHtml } from "./buildWorldReportHtml.ts";
import {
  buildWorldReportViewModel,
} from "./buildWorldReportViewModel.ts";
import { renderPreviewReportImage, type RenderPreviewReportImageRequest } from "./renderPreviewReportImage.ts";
import type { WorldReportViewModel } from "./reportTypes.ts";
import type { DesktopThemeMode } from "../../app/antdTheme";

export interface ExportWorldReportRequest {
  match: SearchMatchSummary;
  geysers: readonly GeyserOption[];
  catalog: SearchCatalog | null;
  themeMode: DesktopThemeMode;
}

export interface ExportWorldReportDeps {
  inTauriRuntime: () => boolean;
  chooseSavePath: (defaultPath: string) => Promise<string | null>;
  getWorldReport: (request: WorldReportRequest) => Promise<WorldReportEvent>;
  renderPreviewReportImage: (request: RenderPreviewReportImageRequest) => Promise<string>;
  buildWorldReportViewModel: (
    report: WorldReportData,
    catalog: SearchCatalog | null
  ) => WorldReportViewModel;
  buildWorldReportHtml: (viewModel: WorldReportViewModel, mapImageDataUrl: string) => string;
  exportReportPdf: (request: ExportReportPdfRequest) => Promise<void>;
}

const DEFAULT_DEPS: ExportWorldReportDeps = {
  inTauriRuntime,
  chooseSavePath: async (defaultPath) =>
    save({
      title: "生成坐标地图 PDF 报告",
      defaultPath,
      filters: [
        {
          name: "PDF 文档",
          extensions: ["pdf"],
        },
      ],
    }),
  getWorldReport,
  renderPreviewReportImage,
  buildWorldReportViewModel,
  buildWorldReportHtml,
  exportReportPdf,
};

function sanitizeFileNamePart(value: string): string {
  return value.replace(/[<>:"/\\|?*\u0000-\u001F]/g, "_");
}

export function buildWorldReportPdfFileName(match: SearchMatchSummary): string {
  const suffix = match.coord.trim().length > 0 ? sanitizeFileNamePart(match.coord) : String(match.seed);
  return `oni-world-report-${suffix}.pdf`;
}

export async function exportWorldReport(
  request: ExportWorldReportRequest,
  deps: Partial<ExportWorldReportDeps> = {}
): Promise<boolean> {
  const resolvedDeps: ExportWorldReportDeps = {
    ...DEFAULT_DEPS,
    ...deps,
  };

  if (!resolvedDeps.inTauriRuntime()) {
    throw new Error("当前不在 Tauri 运行时，无法生成 PDF 报告。");
  }

  const outputPath = await resolvedDeps.chooseSavePath(buildWorldReportPdfFileName(request.match));
  if (!outputPath) {
    return false;
  }

  const reportEvent = await resolvedDeps.getWorldReport({
    jobId: `world-report-${Date.now()}-${request.match.seed}`,
    worldType: request.match.worldType,
    seed: request.match.seed,
    mixing: request.match.mixing,
  });
  const mapImageDataUrl = await resolvedDeps.renderPreviewReportImage({
    preview: reportEvent.report.preview,
    geysers: request.geysers,
    themeMode: request.themeMode,
  });
  const viewModel = resolvedDeps.buildWorldReportViewModel(reportEvent.report, request.catalog);
  const html = resolvedDeps.buildWorldReportHtml(viewModel, mapImageDataUrl);

  await resolvedDeps.exportReportPdf({
    html,
    outputPath,
    title: "坐标地图报告",
  });
  return true;
}
