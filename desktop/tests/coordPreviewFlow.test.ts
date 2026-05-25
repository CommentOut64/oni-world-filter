import test from "node:test";
import assert from "node:assert/strict";

import type { PreviewPayload, SearchMatchSummary } from "../src/lib/contracts.ts";
import { runCoordPreviewFlow } from "../src/features/search/coordPreviewFlow.ts";
import { formatNativeDisplayMessage, loadPreviewByCoord } from "../src/lib/tauri.ts";

const PREVIEW: PreviewPayload = {
  summary: {
    seed: 123456,
    worldType: 13,
    start: { x: 10, y: 20 },
    worldSize: { w: 256, h: 384 },
    traits: [1, 2],
    geysers: [
      { type: 0, x: 11, y: 21, worldX: 11, worldY: 363 },
      { type: 6, x: 15, y: 25, worldX: 15, worldY: 359 },
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
  const longCoord = "V-SNDST-C-231293028-0-39-MPJ2Q7Y1";
  const longMixing = 152841815626;

  await runCoordPreviewFlow(
    {
      loadPreviewByCoord: async (coord) => {
        calls.push(`load:${coord}`);
        return {
          coord,
          worldType: 13,
          seed: 123456,
          mixing: longMixing,
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
    longCoord
  );

  assert.equal(capturedMatches.length, 1);
  assert.deepEqual(calls, [
    `load:${longCoord}`,
    `open:${longCoord}`,
    `prime:${longCoord}:123456`,
    "view-results",
  ]);
  assert.equal(capturedMatches[0]?.seed, 123456);
  assert.equal(capturedMatches[0]?.worldType, 13);
  assert.equal(capturedMatches[0]?.mixing, longMixing);
  assert.equal(capturedMatches[0]?.coord, longCoord);
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

test("formatNativeDisplayMessage maps relaxed native coord validation error", () => {
  assert.equal(
    formatNativeDisplayMessage("invalid native coord; trailing mixing code must be non-empty uppercase base36 within mixing range"),
    "坐标格式无效，尾部混搭编码需为非空大写 base36，且不能超出 mixing 有效范围。"
  );
});
