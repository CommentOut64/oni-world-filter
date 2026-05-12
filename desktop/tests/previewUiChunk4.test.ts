import test, { afterEach } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createElement } from "react";
import { renderToStaticMarkup } from "react-dom/server";

import HostDebugWindow from "../src/features/debug/HostDebugWindow.tsx";
import GeyserListOverlay from "../src/features/preview/GeyserListOverlay.tsx";
import PreviewDetails from "../src/features/preview/PreviewDetails.tsx";
import PreviewLegend from "../src/features/preview/PreviewLegend.tsx";
import PreviewToolbar from "../src/features/preview/PreviewToolbar.tsx";
import { toPreviewViewModel } from "../src/features/preview/previewModel.ts";
import { useSearchStore } from "../src/state/searchStore.ts";

const APP_CSS = readFileSync(new URL("../src/app/app.css", import.meta.url), "utf8");
const PREVIEW_PANE_SOURCE = readFileSync(
  new URL("../src/features/preview/PreviewPane.tsx", import.meta.url),
  "utf8"
);
const CONTRACTS_SOURCE = readFileSync(new URL("../src/lib/contracts.ts", import.meta.url), "utf8");
const PREVIEW_CANVAS_SOURCE = readFileSync(
  new URL("../src/features/preview/PreviewCanvas.tsx", import.meta.url),
  "utf8"
);
const GEYSER_POPOVER_SOURCE = readFileSync(
  new URL("../src/features/preview/GeyserParameterPopover.tsx", import.meta.url),
  "utf8"
);

const PREVIEW = {
  summary: {
    seed: 100001,
    worldType: 13,
    start: { x: 10, y: 20 },
    worldSize: { w: 256, h: 384 },
    traits: [1, 2],
    geysers: [
      { type: 0, x: 11, y: 21 },
      { type: 6, x: 15, y: 25 },
    ],
  },
  polygons: [],
};

afterEach(() => {
  useSearchStore.setState({
    geysers: [],
    selectedSeed: null,
  });
});

test("PreviewToolbar renders antd controls", () => {
  const markup = renderToStaticMarkup(
    createElement(PreviewToolbar, {
      showBoundaries: true,
      showLabels: true,
      showGeysers: true,
      geyserCount: 2,
      onToggleBoundaries: () => undefined,
      onToggleLabels: () => undefined,
      onToggleGeysers: () => undefined,
      onResetView: () => undefined,
      onExportPng: () => undefined,
      onOpenGeyserList: () => undefined,
    })
  );

  assert.match(markup, /ant-switch|ant-btn/);
});

test("PreviewLegend renders antd tags", () => {
  const markup = renderToStaticMarkup(createElement(PreviewLegend));

  assert.match(markup, /ant-tag|ant-card/);
});

test("PreviewLegend is mounted inside preview canvas container", () => {
  assert.match(
    PREVIEW_PANE_SOURCE,
    /<div className="preview-canvas-container"[\s\S]*<PreviewLegend\s*\/>[\s\S]*<\/div>\s*<PreviewDetails/
  );
});

test("PreviewPane forwards themeMode to PreviewCanvas", () => {
  assert.match(
    PREVIEW_PANE_SOURCE,
    /<PreviewCanvas[\s\S]*themeMode=\{themeMode\}/
  );
});

test("PreviewPane wires selected geyser popover inside preview canvas container", () => {
  assert.match(
    PREVIEW_PANE_SOURCE,
    /<PreviewCanvas[\s\S]*selectedGeyserIndex=\{selectedGeyserIndex\}[\s\S]*onSelectedGeyserAnchorChange=\{setSelectedGeyserAnchor\}/
  );
  assert.match(
    PREVIEW_PANE_SOURCE,
    /<div className="preview-canvas-container"[\s\S]*ref=\{previewCanvasContainerRef\}[\s\S]*<PreviewLegend\s*\/>[\s\S]*<GeyserParameterPopover[\s\S]*popupContainer=\{previewCanvasContainer\}[\s\S]*geyserDetailsStatus=\{activeGeyserDetailsStatus\}[\s\S]*<\/div>\s*<PreviewDetails/
  );
});

test("PreviewPane forwards active geyser detail state into GeyserListOverlay", () => {
  assert.match(
    PREVIEW_PANE_SOURCE,
    /<GeyserListOverlay[\s\S]*geyserDetails=\{activeGeyserDetails\}[\s\S]*geyserDetailsStatus=\{activeGeyserDetailsStatus\}[\s\S]*popupContainer=\{previewCanvasContainer\}/
  );
});

test("PreviewLegend uses floating vertical layout styling", () => {
  assert.match(
    APP_CSS,
    /\.preview-legend-card\s*\{\s*position:\s*absolute;\s*top:\s*12px;\s*right:\s*12px;[\s\S]*z-index:\s*9;[\s\S]*\}/
  );
  assert.match(
    APP_CSS,
    /\.preview-legend\s*\{\s*margin-top:\s*0;[\s\S]*flex-direction:\s*column;[\s\S]*align-items:\s*stretch;[\s\S]*\}/
  );
});

test("Geyser parameter popover uses controlled Popover anchor styling above other preview overlays", () => {
  assert.match(GEYSER_POPOVER_SOURCE, /import\s*\{\s*Button,\s*Descriptions,\s*Popover,\s*Space,\s*Typography\s*\}\s*from "antd"/);
  assert.match(GEYSER_POPOVER_SOURCE, /export function resolveGeyserPopoverWidth/);
  assert.match(
    GEYSER_POPOVER_SOURCE,
    /popupContainer\.clientWidth\s*-\s*GEYSER_POPOVER_CONTAINER_MARGIN/
  );
  assert.match(GEYSER_POPOVER_SOURCE, /<Popover[\s\S]*open=\{Boolean\(anchor && geyser\)\}/);
  assert.match(GEYSER_POPOVER_SOURCE, /overlayClassName="geyser-parameter-popover-overlay"/);
  assert.match(
    GEYSER_POPOVER_SOURCE,
    /overlayStyle=\{\{\s*width:\s*overlayWidth,\s*maxWidth:\s*overlayWidth\s*\}\}/
  );
  assert.match(GEYSER_POPOVER_SOURCE, /<span className="geyser-parameter-anchor" style=\{\{ left: anchor\.left, top: anchor\.top \}\} \/>/);
  assert.match(
    APP_CSS,
    /\.geyser-parameter-anchor\s*\{\s*position:\s*absolute;[\s\S]*pointer-events:\s*none;[\s\S]*z-index:\s*11;[\s\S]*\}/
  );
  assert.match(
    APP_CSS,
    /\.geyser-parameter-popover-overlay \.ant-popover-inner\s*\{[\s\S]*width:\s*100%;[\s\S]*max-width:\s*100%;[\s\S]*box-sizing:\s*border-box;[\s\S]*\}/
  );
});

test("Contracts reserve WorldReportData and world_report event shape", () => {
  assert.match(CONTRACTS_SOURCE, /export interface WorldReportData \{\s*preview: PreviewPayload;\s*geyserDetails: GeyserDetail\[\];\s*mixing: number;\s*coord: string;\s*\}/);
  assert.match(CONTRACTS_SOURCE, /export interface WorldReportRequest \{\s*jobId: string;\s*worldType: number;\s*seed: number;\s*mixing: number;\s*\}/);
  assert.match(CONTRACTS_SOURCE, /export interface WorldReportEvent \{\s*event: "world_report";\s*jobId: string;\s*report: WorldReportData;\s*\}/);
});

test("PreviewDetails renders antd empty state without preview", () => {
  const markup = renderToStaticMarkup(
    createElement(PreviewDetails, {
      preview: null,
      hoveredRegion: null,
      selectedRegion: null,
      hoverGeyserIndex: null,
      selectedGeyserIndex: null,
    })
  );

  assert.match(markup, /ant-empty|ant-card/);
});

test("PreviewDetails renders antd details shell with preview", () => {
  useSearchStore.setState({
    geysers: [
      { id: 0, key: "steam" },
      { id: 6, key: "salt_water" },
    ],
    selectedSeed: 100001,
  });

  const markup = renderToStaticMarkup(
    createElement(PreviewDetails, {
      preview: PREVIEW,
      hoveredRegion: { id: "region-1", zoneType: 3 },
      selectedRegion: null,
      hoverGeyserIndex: 0,
      selectedGeyserIndex: null,
    })
  );

  assert.match(markup, /ant-descriptions|ant-card|ant-list/);
  assert.equal((markup.match(/ant-descriptions-row/g) ?? []).length, 2);
  assert.match(markup, /preview-detail-focus-value/);
  assert.equal((markup.match(/preview-detail-focus-value/g) ?? []).length, 2);
});

test("PreviewDetails uses player biome names for desktop-visible region labels", () => {
  useSearchStore.setState({
    geysers: [],
    selectedSeed: 100001,
  });

  const barrenMarkup = renderToStaticMarkup(
    createElement(PreviewDetails, {
      preview: PREVIEW,
      hoveredRegion: { id: "region-16", zoneType: 16 },
      selectedRegion: null,
      hoverGeyserIndex: null,
      selectedGeyserIndex: null,
    })
  );
  assert.match(barrenMarkup, /浮土生态/);
  assert.doesNotMatch(barrenMarkup, /岩漠生态/);

  const hiddenMarkup = renderToStaticMarkup(
    createElement(PreviewDetails, {
      preview: PREVIEW,
      hoveredRegion: { id: "region-1", zoneType: 1 },
      selectedRegion: null,
      hoverGeyserIndex: null,
      selectedGeyserIndex: null,
    })
  );
  assert.doesNotMatch(hiddenMarkup, /水晶洞穴|Crystal Caverns/);
  assert.match(hiddenMarkup, />-</);
});

test("Preview model uses player biome names for visible region labels", () => {
  const model = toPreviewViewModel(
    {
      ...PREVIEW,
      polygons: [
        { zoneType: 16, vertices: [[0, 0], [4, 0], [4, 4], [0, 4]] },
        { zoneType: 1, vertices: [[10, 10], [14, 10], [14, 14], [10, 14]] },
      ],
    },
    []
  );

  const texts = model.labelCandidates.filter((item) => item.kind === "region").map((item) => item.text);
  assert.match(texts.join(","), /浮土生态/);
  assert.doesNotMatch(texts.join(","), /水晶洞穴|Crystal Caverns/);
});

test("PreviewDetails focus values use unified detail text styling", () => {
  assert.match(
    APP_CSS,
    /\.preview-detail-focus-value\s*\{\s*display:\s*inline;\s*color:\s*var\(--text-main\);\s*font-size:\s*12px;\s*line-height:\s*1\.5;\s*background:\s*transparent;\s*padding:\s*0;\s*border:\s*0;\s*\}/
  );
});

test("GeyserListOverlay renders antd overlay shell", () => {
  useSearchStore.setState({
    geysers: [
      { id: 0, key: "steam" },
      { id: 6, key: "salt_water" },
    ],
  });

  const markup = renderToStaticMarkup(
    createElement(GeyserListOverlay, {
      geysersData: PREVIEW.summary.geysers,
      onClose: () => undefined,
    })
  );

  assert.match(markup, /ant-card|ant-list/);
  assert.match(markup, /ant-btn/);
});

test("PreviewCanvas computes selected geyser anchor and clears selection on blank click", () => {
  assert.match(
    PREVIEW_CANVAS_SOURCE,
    /onSelectedGeyserAnchorChange:\s*\(anchor:\s*GeyserParameterAnchor\s*\|\s*null\)\s*=>\s*void/
  );
  assert.match(
    PREVIEW_CANVAS_SOURCE,
    /resolveSelectedGeyserAnchor\(marker,\s*viewport,\s*stageSize\)/
  );
  assert.match(
    PREVIEW_CANVAS_SOURCE,
    /if \(event\.target !== event\.target\.getStage\(\)\)\s*\{\s*return;\s*\}[\s\S]*onSelectedGeyserChange\(null\);[\s\S]*onSelectedGeyserAnchorChange\(null\);/
  );
});

test("HostDebugWindow renders antd container shell", () => {
  const markup = renderToStaticMarkup(createElement(HostDebugWindow));

  assert.match(markup, /ant-card|ant-collapse/);
});
