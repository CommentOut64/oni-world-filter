import test, { afterEach } from "node:test";
import assert from "node:assert/strict";
import { createElement } from "react";
import { renderToStaticMarkup } from "react-dom/server";

import HostDebugWindow from "../src/features/debug/HostDebugWindow.tsx";
import GeyserListOverlay from "../src/features/preview/GeyserListOverlay.tsx";
import PreviewDetails from "../src/features/preview/PreviewDetails.tsx";
import PreviewLegend from "../src/features/preview/PreviewLegend.tsx";
import PreviewToolbar from "../src/features/preview/PreviewToolbar.tsx";
import { useSearchStore } from "../src/state/searchStore.ts";

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

test("HostDebugWindow renders antd container shell", () => {
  const markup = renderToStaticMarkup(createElement(HostDebugWindow));

  assert.match(markup, /ant-card|ant-collapse/);
});
