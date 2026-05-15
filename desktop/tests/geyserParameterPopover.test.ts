import test, { afterEach } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createElement } from "react";
import { renderToStaticMarkup } from "react-dom/server";

import GeyserParameterPopover, {
  resolveGeyserPopoverWidth,
} from "../src/features/preview/GeyserParameterPopover.tsx";
import { useSearchStore } from "../src/state/searchStore.ts";

const GEYSER_POPOVER_SOURCE = readFileSync(
  new URL("../src/features/preview/GeyserParameterPopover.tsx", import.meta.url),
  "utf8"
);

const GEYSER = {
  type: 6,
  x: 15,
  y: 25,
  id: "salt_water",
};

const GEYSER_DETAIL = {
  index: 0,
  summary: GEYSER,
  hasParameters: true,
  parameterKind: "geyser",
  native: {
    averageActiveYieldKgPerCycle: 10,
    eruptionPeriodSeconds: 805,
    eruptionRatio: 0.4,
    activePeriodSeconds: 49080,
    activeRatio: 0.69,
  },
  derived: {
    eruptionRateKgPerSecond: 12.4,
    averageOverallYieldGPerSecond: 3437.3,
    eruptionSeconds: 324,
    activeSeconds: 33865.2,
    activeCycles: 56.4,
    totalCycles: 81.8,
    temperatureCelsius: 95,
  },
};

afterEach(() => {
  useSearchStore.setState({
    geysers: [],
    selectedSeed: null,
  });
});

test("GeyserParameterPopover renders positioned floating panel for selected geyser", () => {
  const markup = renderToStaticMarkup(
    createElement(GeyserParameterPopover, {
      anchor: { left: 120, top: 160 },
      popupContainer: null,
      geyser: GEYSER,
      geyserDetail: null,
      geyserDetailsStatus: "loading",
      geyserDetailsError: null,
      onClose: () => undefined,
      onRetry: () => undefined,
    })
  );

  assert.match(markup, /geyser-parameter-popover-overlay geyser-parameter-popover-floating ant-popover ant-popover-placement-rightTop/);
  assert.match(markup, /left:120px;top:160px;width:320px;max-width:320px/);
});

test("GeyserParameterPopover constrains overlay width to popup container width", () => {
  assert.equal(resolveGeyserPopoverWidth(null), 320);
  assert.equal(resolveGeyserPopoverWidth({ clientWidth: 640 } as HTMLElement), 320);
  assert.equal(resolveGeyserPopoverWidth({ clientWidth: 280 } as HTMLElement), 256);
  assert.equal(resolveGeyserPopoverWidth({ clientWidth: 180 } as HTMLElement), 156);
});

test("GeyserParameterPopover source keeps loading and ready parameter content", () => {
  void GEYSER_DETAIL;
  assert.match(GEYSER_POPOVER_SOURCE, /参数计算中\.\.\./);
  assert.match(GEYSER_POPOVER_SOURCE, /<Descriptions/);
  assert.match(GEYSER_POPOVER_SOURCE, /formatGeyserDetailTemperature\(detail\)/);
  assert.match(GEYSER_POPOVER_SOURCE, /formatGeyserDetailEruptionRate\(detail\)/);
  assert.match(GEYSER_POPOVER_SOURCE, /formatGeyserDetailAverageYield\(detail\)/);
  assert.match(GEYSER_POPOVER_SOURCE, /formatGeyserDetailEruptionWindow\(detail\)/);
  assert.match(GEYSER_POPOVER_SOURCE, /formatGeyserDetailActiveWindow\(detail\)/);
});

test("GeyserParameterPopover source keeps non-parameter fallback message branch", () => {
  assert.match(GEYSER_POPOVER_SOURCE, /formatGeyserDetailMissingMessage\(detail\.parameterKind\)/);
  assert.match(GEYSER_POPOVER_SOURCE, /当前喷口详情暂不可用。/);
});

test("GeyserParameterPopover source keeps failed state and retry controls", () => {
  assert.match(GEYSER_POPOVER_SOURCE, /参数加载失败:/);
  assert.match(GEYSER_POPOVER_SOURCE, />\s*重试\s*</);
  assert.match(GEYSER_POPOVER_SOURCE, />\s*关闭\s*</);
});

test("GeyserParameterPopover source keeps floating panel positioning without rc-trigger realign", () => {
  assert.match(GEYSER_POPOVER_SOURCE, /className="geyser-parameter-popover-overlay geyser-parameter-popover-floating ant-popover ant-popover-placement-rightTop"/);
  assert.match(GEYSER_POPOVER_SOURCE, /left:\s*anchor\.left/);
  assert.match(GEYSER_POPOVER_SOURCE, /top:\s*anchor\.top/);
  assert.match(GEYSER_POPOVER_SOURCE, /<div className="ant-popover-content">/);
  assert.match(GEYSER_POPOVER_SOURCE, /<div className="ant-popover-inner" role="tooltip">/);
  assert.match(GEYSER_POPOVER_SOURCE, /<div className="ant-popover-title">\{panelTitle\}<\/div>/);
  assert.doesNotMatch(GEYSER_POPOVER_SOURCE, /forceAlign/);
  assert.doesNotMatch(GEYSER_POPOVER_SOURCE, /<Popover/);
});
