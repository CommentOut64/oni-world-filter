import test from "node:test";
import assert from "node:assert/strict";

import type { SearchMatchSummary, WorldReportEvent } from "../src/lib/contracts.ts";
import { FALLBACK_SEARCH_CATALOG } from "../src/lib/searchCatalog.ts";
import {
  buildWorldReportPdfFileName,
  exportWorldReport,
} from "../src/features/report/exportWorldReport.ts";

const MATCH: SearchMatchSummary = {
  seed: 100123,
  worldType: 13,
  mixing: 123,
  coord: "V-SNDST-C-100123-0-D3-HD",
  traits: [],
  start: { x: 12, y: 34 },
  worldSize: { w: 256, h: 384 },
  geysers: [{ type: 0, x: 70, y: 90, id: "steam" }],
  nearestDistance: null,
};

const REPORT_EVENT: WorldReportEvent = {
  event: "world_report",
  jobId: "report-1",
  report: {
    preview: {
      summary: {
        seed: 100123,
        worldType: 13,
        start: { x: 12, y: 34 },
        worldSize: { w: 256, h: 384 },
        traits: [],
        geysers: [{ type: 0, x: 70, y: 90, id: "steam" }],
      },
      polygons: [],
    },
    geyserDetails: [],
    mixing: 123,
    coord: "V-SNDST-C-100123-0-D3-HD",
  },
};

test("buildWorldReportPdfFileName uses coord when available", () => {
  assert.equal(
    buildWorldReportPdfFileName(MATCH),
    "oni-world-report-V-SNDST-C-100123-0-D3-HD.pdf"
  );
});

test("exportWorldReport wires report data, rendered map image and html into exportReportPdf", async () => {
  const calls: string[] = [];
  const pdfRequests: Array<{ html: string; outputPath: string; title: string }> = [];

  const exported = await exportWorldReport(
    {
      match: MATCH,
      geysers: [{ id: 0, key: "steam" }],
      catalog: FALLBACK_SEARCH_CATALOG,
      themeMode: "dark",
    },
    {
      inTauriRuntime: () => true,
      chooseSavePath: async (defaultPath) => {
        calls.push(`save:${defaultPath}`);
        return "C:\\Users\\wgh\\Desktop\\oni-world-report.pdf";
      },
      getWorldReport: async (request) => {
        calls.push(`report:${request.worldType}:${request.seed}:${request.mixing}`);
        return REPORT_EVENT;
      },
      renderPreviewReportImage: async (request) => {
        calls.push(`image:${request.preview.summary.seed}:${request.themeMode}`);
        return "data:image/png;base64,AAA=";
      },
      buildWorldReportViewModel: (report) => {
        calls.push(`vm:${report.coord}`);
        return {
          worldCategoryLabel: "经典",
          worldName: "类地星群",
          coord: report.coord,
          seedLabel: "100123",
          worldSizeLabel: "256 × 384",
          startLabel: "(12, 34)",
          mixingSummary: "寒霜行星包",
          geyserRows: [],
        };
      },
      buildWorldReportHtml: (viewModel, mapImageDataUrl) => {
        calls.push(`html:${viewModel.coord}:${mapImageDataUrl}`);
        return "<html>report</html>";
      },
      exportReportPdf: async (request) => {
        calls.push(`pdf:${request.outputPath}`);
        pdfRequests.push(request);
      },
    }
  );

  assert.equal(exported, true);
  assert.deepEqual(calls, [
    "save:oni-world-report-V-SNDST-C-100123-0-D3-HD.pdf",
    "report:13:100123:123",
    "image:100123:dark",
    "vm:V-SNDST-C-100123-0-D3-HD",
    "html:V-SNDST-C-100123-0-D3-HD:data:image/png;base64,AAA=",
    "pdf:C:\\Users\\wgh\\Desktop\\oni-world-report.pdf",
  ]);
  assert.deepEqual(pdfRequests, [
    {
      html: "<html>report</html>",
      outputPath: "C:\\Users\\wgh\\Desktop\\oni-world-report.pdf",
      title: "坐标地图报告",
    },
  ]);
});

test("exportWorldReport stops quietly when the user cancels the save dialog", async () => {
  let called = false;

  const exported = await exportWorldReport(
    {
      match: MATCH,
      geysers: [],
      catalog: FALLBACK_SEARCH_CATALOG,
      themeMode: "light",
    },
    {
      inTauriRuntime: () => true,
      chooseSavePath: async () => null,
      getWorldReport: async () => {
        called = true;
        return REPORT_EVENT;
      },
      renderPreviewReportImage: async () => {
        called = true;
        return "data:image/png;base64,AAA=";
      },
      buildWorldReportViewModel: () => {
        called = true;
        throw new Error("should not build view model after cancellation");
      },
      buildWorldReportHtml: () => {
        called = true;
        throw new Error("should not build html after cancellation");
      },
      exportReportPdf: async () => {
        called = true;
      },
    }
  );

  assert.equal(exported, false);
  assert.equal(called, false);
});
