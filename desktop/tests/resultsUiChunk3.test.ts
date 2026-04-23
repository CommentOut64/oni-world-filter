import test, { afterEach } from "node:test";
import assert from "node:assert/strict";
import { createElement } from "react";
import { renderToStaticMarkup } from "react-dom/server";

import ResultSummaryCards from "../src/features/results/ResultSummaryCards.tsx";
import ResultsTable from "../src/features/results/ResultsTable.tsx";
import ResultToolbar from "../src/features/results/ResultToolbar.tsx";
import { copyCoordCode, createResultColumns } from "../src/features/results/resultColumns.tsx";
import { formatGeyserCountSummary } from "../src/features/results/resultSummary.ts";
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

  assert.match(markup, /ant-btn/);
  assert.match(markup, /取消搜索/);
  assert.match(markup, /清空结果/);
  assert.match(markup, /result-toolbar-actions/);
  assert.doesNotMatch(markup, /ant-space-compact/);
  assert.doesNotMatch(markup, /打开调试窗口/);
  assert.doesNotMatch(markup, /结果总数/);
  assert.doesNotMatch(markup, /状态:/);
});

test("ResultsTable renders antd table shell", () => {
  useSearchStore.setState({
    geysers: BASE_STATE.geysers,
    results: BASE_STATE.results,
    selectedSeed: BASE_STATE.selectedSeed,
  });

  const markup = renderToStaticMarkup(createElement(ResultsTable));

  assert.match(markup, /ant-table/);
  assert.match(markup, /ant-table-virtual/);
  assert.match(markup, /ant-table-body/);
  assert.match(markup, /max-height:\d+px/);
});

test("ResultsTable does not render antd selection control column", () => {
  useSearchStore.setState({
    geysers: BASE_STATE.geysers,
    results: BASE_STATE.results,
    selectedSeed: BASE_STATE.selectedSeed,
  });

  const markup = renderToStaticMarkup(createElement(ResultsTable));

  assert.doesNotMatch(markup, /ant-table-selection-column/);
});

test("geyser summary prioritizes constrained geysers before other overview items", () => {
  const summary = formatGeyserCountSummary(
    [
      { type: 0, x: 11, y: 21 },
      { type: 6, x: 15, y: 25 },
      { type: 0, x: 17, y: 27 },
    ],
    BASE_STATE.geysers,
    4,
    ["salt_water"]
  );

  assert.equal(summary, "盐水泉, 低温蒸汽喷孔 x2");
});

test("result columns rename nearest distance to geyser minimum distance", () => {
  const columns = createResultColumns(BASE_STATE.geysers, []);
  const nearestDistanceColumn = columns.find((column) => column.key === "nearestDistance");

  assert.ok(nearestDistanceColumn);
  assert.equal(nearestDistanceColumn.title, "喷口最小距离");
});

test("result columns add copy action and remove trait summary", () => {
  const columns = createResultColumns(BASE_STATE.geysers, []);

  const copyColumn = columns.find((column) => column.key === "copyCoord");
  const traitColumn = columns.find((column) => column.key === "traitSummary");

  assert.ok(copyColumn);
  assert.equal(copyColumn.title, "");
  assert.equal(traitColumn, undefined);
});

test("copyCoordCode writes coord text and reports success", async () => {
  const calls: string[] = [];

  await copyCoordCode("SNDST-A-100001-0-D3-HD", {
    writeText: async (value) => {
      calls.push(`write:${value}`);
    },
    notifySuccess: (value) => {
      calls.push(`success:${value}`);
    },
  });

  assert.deepEqual(calls, ["write:SNDST-A-100001-0-D3-HD", "success:复制成功"]);
});
