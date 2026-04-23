import test from "node:test";
import assert from "node:assert/strict";

import type { PreviewPayload, SearchMatchSummary } from "../src/lib/contracts.ts";
import { runCoordPreviewFlow } from "../src/features/search/coordPreviewFlow.ts";
import { loadPreviewByCoord } from "../src/lib/tauri.ts";

const PREVIEW: PreviewPayload = {
  summary: {
    seed: 123456,
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

test("tauri exports loadPreviewByCoord for direct coord preview flow", () => {
  assert.equal(typeof loadPreviewByCoord, "function");
});

test("runCoordPreviewFlow loads preview by coord and opens single direct result", async () => {
  const calls: string[] = [];
  const capturedMatches: SearchMatchSummary[] = [];

  await runCoordPreviewFlow(
    {
      loadPreviewByCoord: async (coord) => {
        calls.push(`load:${coord}`);
        return {
          coord,
          worldType: 13,
          seed: 123456,
          mixing: 625,
          preview: PREVIEW,
        };
      },
      openDirectCoordResult: (match) => {
        calls.push(`open:${match.coord}`);
        capturedMatches.push(match);
      },
      primeResolvedPreview: (match, preview) => {
        calls.push(`prime:${match.coord}:${preview.summary.seed}`);
      },
      setError: (message) => {
        calls.push(`error:${message}`);
      },
      openResults: () => {
        calls.push("view-results");
      },
    },
    "V-SNDST-C-123456-0-D3-HD"
  );

  assert.equal(capturedMatches.length, 1);
  assert.deepEqual(calls, [
    "load:V-SNDST-C-123456-0-D3-HD",
    "open:V-SNDST-C-123456-0-D3-HD",
    "prime:V-SNDST-C-123456-0-D3-HD:123456",
    "view-results",
  ]);
  assert.equal(capturedMatches[0]?.seed, 123456);
  assert.equal(capturedMatches[0]?.worldType, 13);
  assert.equal(capturedMatches[0]?.mixing, 625);
  assert.equal(capturedMatches[0]?.coord, "V-SNDST-C-123456-0-D3-HD");
  assert.deepEqual(capturedMatches[0]?.traits, [1, 2]);
  assert.deepEqual(capturedMatches[0]?.start, { x: 10, y: 20 });
  assert.deepEqual(capturedMatches[0]?.worldSize, { w: 256, h: 384 });
  assert.deepEqual(capturedMatches[0]?.geysers, PREVIEW.summary.geysers);
  assert.equal(capturedMatches[0]?.nearestDistance, 1.4);
});

test("runCoordPreviewFlow only sets error when coord preview fails", async () => {
  const calls: string[] = [];

  await runCoordPreviewFlow(
    {
      loadPreviewByCoord: async () => {
        throw new Error("坐标无效");
      },
      openDirectCoordResult: () => {
        calls.push("open");
      },
      primeResolvedPreview: () => {
        calls.push("prime");
      },
      setError: (message) => {
        calls.push(`error:${message}`);
      },
      openResults: () => {
        calls.push("view-results");
      },
    },
    "broken-coord"
  );

  assert.deepEqual(calls, ["error:坐标无效"]);
});
