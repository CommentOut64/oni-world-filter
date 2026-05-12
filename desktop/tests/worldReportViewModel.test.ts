import test from "node:test";
import assert from "node:assert/strict";

import type { WorldReportData } from "../src/lib/contracts.ts";
import { FALLBACK_SEARCH_CATALOG } from "../src/lib/searchCatalog.ts";
import { encodeMixingFromLevels } from "../src/features/search/searchSchema.ts";
import { buildWorldReportViewModel } from "../src/features/report/buildWorldReportViewModel.ts";

const MIXING_LEVELS = [2, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1];

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
  mixing: encodeMixingFromLevels(MIXING_LEVELS),
  coord: "V-SNDST-C-100123-0-D3-HD",
};

test("buildWorldReportViewModel maps world category, world name and coord summary", () => {
  const viewModel = buildWorldReportViewModel(REPORT, FALLBACK_SEARCH_CATALOG);

  assert.equal(viewModel.worldCategoryLabel, "经典");
  assert.equal(viewModel.worldName, "类地星群");
  assert.equal(viewModel.coord, "V-SNDST-C-100123-0-D3-HD");
  assert.equal(viewModel.seedLabel, "100123");
  assert.equal(viewModel.worldSizeLabel, "256 × 384");
  assert.equal(viewModel.startLabel, "(12, 34)");
});

test("buildWorldReportViewModel decodes mixing summary with formal slot names", () => {
  const viewModel = buildWorldReportViewModel(REPORT, FALLBACK_SEARCH_CATALOG);

  assert.match(viewModel.mixingSummary, /寒霜行星包/);
  assert.match(viewModel.mixingSummary, /冰窟生态/);
  assert.match(viewModel.mixingSummary, /史前行星包/);
  assert.match(viewModel.mixingSummary, /古迹星碎片/);
});

test("buildWorldReportViewModel formats geyser rows with formal name and detail strings", () => {
  const viewModel = buildWorldReportViewModel(REPORT, FALLBACK_SEARCH_CATALOG);

  assert.equal(viewModel.geyserRows.length, 1);
  assert.equal(viewModel.geyserRows[0].name, "低温蒸汽喷孔");
  assert.equal(viewModel.geyserRows[0].coord, "(70, 90)");
  assert.equal(viewModel.geyserRows[0].temperature, "110.0 °C");
  assert.equal(viewModel.geyserRows[0].eruptionRate, "3.9 kg/s");
  assert.equal(viewModel.geyserRows[0].averageYield, "1561.0 g/s");
  assert.match(viewModel.geyserRows[0].eruptionWindow, /每 646\.8 秒喷发 403\.6 秒/);
  assert.match(viewModel.geyserRows[0].activeWindow, /每 110\.3 周期活跃 71\.6 周期/);
});

test("buildWorldReportViewModel renders non-parameter geyser values as dash placeholders", () => {
  const viewModel = buildWorldReportViewModel(
    {
      ...REPORT,
      geyserDetails: [
        {
          ...REPORT.geyserDetails[0],
          summary: { type: 26, x: 88, y: 99, id: "printing_pod" },
          hasParameters: false,
          parameterKind: "facility",
        },
      ],
    },
    FALLBACK_SEARCH_CATALOG
  );

  assert.equal(viewModel.geyserRows[0].name, "打印舱");
  assert.equal(viewModel.geyserRows[0].coord, "(88, 99)");
  assert.equal(viewModel.geyserRows[0].temperature, "-");
  assert.equal(viewModel.geyserRows[0].eruptionRate, "-");
  assert.equal(viewModel.geyserRows[0].averageYield, "-");
  assert.equal(viewModel.geyserRows[0].eruptionWindow, "-");
  assert.equal(viewModel.geyserRows[0].activeWindow, "-");
});

test("buildWorldReportViewModel sorts geyser rows with the same ordering as preview geyser list", () => {
  const viewModel = buildWorldReportViewModel(
    {
      ...REPORT,
      geyserDetails: [
        {
          ...REPORT.geyserDetails[0],
          index: 0,
          summary: { type: 9, x: 10, y: 20, id: "small_volcano" },
        },
        {
          ...REPORT.geyserDetails[0],
          index: 1,
          summary: { type: 27, x: 30, y: 40, id: "steam" },
        },
        {
          ...REPORT.geyserDetails[0],
          index: 2,
          summary: { type: 1, x: 50, y: 60, id: "hot_water" },
        },
        {
          ...REPORT.geyserDetails[0],
          index: 3,
          summary: { type: 18, x: 70, y: 80, id: "molten_gold" },
        },
      ],
    },
    FALLBACK_SEARCH_CATALOG
  );

  assert.deepEqual(
    viewModel.geyserRows.map((row) => row.name),
    ["清水泉", "低温蒸汽喷孔", "小型火山", "金火山"]
  );
});
