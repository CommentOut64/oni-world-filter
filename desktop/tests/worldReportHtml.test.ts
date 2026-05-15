import test from "node:test";
import assert from "node:assert/strict";

import { FALLBACK_SEARCH_CATALOG } from "../src/lib/searchCatalog.ts";
import { buildWorldReportViewModel } from "../src/features/report/buildWorldReportViewModel.ts";
import { buildWorldReportHtml } from "../src/features/report/buildWorldReportHtml.ts";
import { REPORT_PRINT_CSS } from "../src/features/report/reportCss.ts";
import type { WorldReportData } from "../src/lib/contracts.ts";
import { encodeMixingFromLevels } from "../src/features/search/searchSchema.ts";

const REPORT: WorldReportData = {
  preview: {
    summary: {
      seed: 100123,
      worldType: 13,
      start: { x: 12, y: 34 },
      worldSize: { w: 256, h: 384 },
      traits: [],
      geysers: [
        { type: 0, x: 70, y: 90, id: "steam" },
      ],
    },
    polygons: [],
  },
  geyserDetails: [
    {
      index: 0,
      summary: { type: 0, x: 70, y: 90, id: "steam" },
      hasParameters: true,
      parameterKind: "geyser",
      native: {
        averageActiveYieldKgPerCycle: 1443.45,
        eruptionPeriodSeconds: 646.84,
        eruptionRatio: 0.62,
        activePeriodSeconds: 66194.12,
        activeRatio: 0.65,
      },
      derived: {
        eruptionRateKgPerSecond: 3.85,
        averageOverallYieldGPerSecond: 1560.99,
        eruptionSeconds: 403.64,
        activeSeconds: 42950.63,
        activeCycles: 71.58,
        totalCycles: 110.32,
        temperatureCelsius: 110.0,
      },
    },
  ],
  mixing: encodeMixingFromLevels([2, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1]),
  coord: "V-SNDST-C-100123-0-D3-HD",
};

test("buildWorldReportHtml renders summary page, map image and geyser table", () => {
  const viewModel = buildWorldReportViewModel(REPORT, FALLBACK_SEARCH_CATALOG);
  const html = buildWorldReportHtml(viewModel, "data:image/png;base64,AAA=");

  assert.match(html, /坐标地图报告/);
  assert.match(html, /世界分类/);
  assert.match(html, /具体世界/);
  assert.match(html, /完整坐标/);
  assert.match(html, /喷口详细参数/);
  assert.match(html, /低温蒸汽喷孔/);
  assert.match(html, /data:image\/png;base64,AAA=/);
});

test("buildWorldReportHtml embeds print CSS with page breaks and color preservation", () => {
  const viewModel = buildWorldReportViewModel(REPORT, FALLBACK_SEARCH_CATALOG);
  const html = buildWorldReportHtml(viewModel, "data:image/png;base64,AAA=");

  assert.match(html, /@page\s*\{\s*size:\s*A4 portrait;/);
  assert.match(html, /break-after:\s*page/);
  assert.match(html, /print-color-adjust:\s*exact/);
  assert.match(html, /-webkit-print-color-adjust:\s*exact/);
});

test("report print css keeps non-table sections background-free and prevents coord wrapping", () => {
  assert.doesNotMatch(REPORT_PRINT_CSS, /body\s*\{[^}]*background:/);
  assert.doesNotMatch(REPORT_PRINT_CSS, /\.hero\s*\{[^}]*background:/);
  assert.doesNotMatch(REPORT_PRINT_CSS, /\.card\s*\{[^}]*background:/);
  assert.doesNotMatch(REPORT_PRINT_CSS, /\.map-image\s*\{[^}]*background:/);
  assert.match(REPORT_PRINT_CSS, /\.geyser-coord\s*\{[\s\S]*white-space:\s*nowrap/);
  assert.match(REPORT_PRINT_CSS, /\.map-image\s*\{[\s\S]*max-height:\s*172mm/);
});
