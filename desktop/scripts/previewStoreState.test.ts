import test from "node:test";
import assert from "node:assert/strict";

import type { PreviewPayload } from "../src/lib/contracts";
import {
  beginPreviewLoad,
  completePreviewLoad,
  failPreviewLoad,
  type PreviewStoreSnapshot,
} from "../src/state/previewStoreState.ts";

function makePreview(seed: number): PreviewPayload {
  return {
    polygons: [],
    summary: {
      seed,
      worldType: 0,
      start: { x: 0, y: 0 },
      worldSize: { w: 256, h: 384 },
      traits: [],
      geysers: [],
    },
  };
}

function makeState(): PreviewStoreSnapshot {
  return {
    activeKey: null,
    activePreview: null,
    cache: {},
    isLoading: false,
    lastError: null,
  };
}

test("beginPreviewLoad clears stale preview for uncached key", () => {
  const initial = {
    ...makeState(),
    activeKey: "0:100067:625",
    activePreview: makePreview(100067),
  };

  const next = beginPreviewLoad(initial, "0:100082:625");

  assert.equal(next.activeKey, "0:100082:625");
  assert.equal(next.activePreview, null);
  assert.equal(next.isLoading, true);
  assert.equal(next.lastError, null);
});

test("completePreviewLoad ignores stale response for inactive key", () => {
  const initial = {
    ...makeState(),
    activeKey: "0:100082:625",
    activePreview: makePreview(100082),
    isLoading: true,
  };

  const next = completePreviewLoad(initial, "0:100067:625", makePreview(100067));

  assert.equal(next.activeKey, "0:100082:625");
  assert.equal(next.activePreview?.summary.seed, 100082);
  assert.equal(next.isLoading, true);
  assert.equal(next.cache["0:100067:625"]?.summary.seed, 100067);
});

test("failPreviewLoad ignores stale error for inactive key", () => {
  const initial = {
    ...makeState(),
    activeKey: "0:100082:625",
    activePreview: makePreview(100082),
    isLoading: true,
  };

  const next = failPreviewLoad(initial, "0:100067:625", "stale failure");

  assert.deepEqual(next, initial);
});
