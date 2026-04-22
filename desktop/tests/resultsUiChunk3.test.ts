import test, { afterEach } from "node:test";
import assert from "node:assert/strict";
import { createElement } from "react";
import { renderToStaticMarkup } from "react-dom/server";

import ResultSummaryCards from "../src/features/results/ResultSummaryCards.tsx";
import ResultsTable from "../src/features/results/ResultsTable.tsx";
import ResultToolbar from "../src/features/results/ResultToolbar.tsx";
import { useSearchStore } from "../src/state/searchStore.ts";

const BASE_STATE = {
  stats: {
    startedAtMs: 1713772800000,
    processedSeeds: 123456,
    totalSeeds: 200000,
    totalMatches: 42,
    activeWorkers: 8,
    currentSeedsPerSecond: 321.5,
    peakSeedsPerSecond: 512.3,
  },
  isSearching: true,
  selectedSeed: 100001,
  lastSubmittedRequest: null,
  lastHostDebugMessages: [],
  geysers: [
    { id: 0, key: "steam" },
    { id: 6, key: "salt_water" },
  ],
  results: [
    {
      seed: 100001,
      worldType: 13,
      mixing: 625,
      coord: "SNDST-A-100001-0-D3-HD",
      traits: [1, 2],
      start: { x: 10, y: 20 },
      worldSize: { w: 256, h: 384 },
      geysers: [
        { type: 0, x: 11, y: 21 },
        { type: 6, x: 15, y: 25 },
      ],
      nearestDistance: 12.5,
    },
  ],
};

afterEach(() => {
  useSearchStore.setState({
    stats: {
      startedAtMs: null,
      processedSeeds: 0,
      totalSeeds: 0,
      totalMatches: 0,
      activeWorkers: 0,
      currentSeedsPerSecond: 0,
      peakSeedsPerSecond: 0,
    },
    isSearching: false,
    selectedSeed: null,
    lastSubmittedRequest: null,
    lastHostDebugMessages: [],
    geysers: [],
    results: [],
  });
});

test("ResultSummaryCards renders antd statistics", () => {
  useSearchStore.setState({
    stats: BASE_STATE.stats,
  });

  const markup = renderToStaticMarkup(createElement(ResultSummaryCards));

  assert.match(markup, /ant-statistic/);
});

test("ResultToolbar renders antd status shell", () => {
  useSearchStore.setState({
    isSearching: BASE_STATE.isSearching,
    selectedSeed: BASE_STATE.selectedSeed,
    results: BASE_STATE.results,
    lastSubmittedRequest: BASE_STATE.lastSubmittedRequest,
    lastHostDebugMessages: BASE_STATE.lastHostDebugMessages,
  });

  const markup = renderToStaticMarkup(createElement(ResultToolbar));

  assert.match(markup, /ant-tag|ant-badge/);
  assert.match(markup, /ant-btn/);
});

test("ResultsTable renders antd table shell", () => {
  useSearchStore.setState({
    geysers: BASE_STATE.geysers,
    results: BASE_STATE.results,
    selectedSeed: BASE_STATE.selectedSeed,
  });

  const markup = renderToStaticMarkup(createElement(ResultsTable));

  assert.match(markup, /ant-table/);
  assert.match(markup, /ant-table-body/);
  assert.match(markup, /max-height:\d+px/);
});
