import test from "node:test";
import assert from "node:assert/strict";

import type { SearchMatchSummary, SearchRequest } from "../src/lib/contracts";
import {
  SEARCH_SESSION_STORAGE_KEY,
  persistSearchSessionSnapshot,
  restoreSearchSessionSnapshot,
  type SearchSessionStorage,
} from "../src/state/searchStorePersistence.ts";

const SAMPLE_DRAFT = {
  worldType: 13,
  seedStart: 100000,
  seedEnd: 120000,
  mixing: 625,
  threads: 0,
  cpu: {
    mode: "balanced" as const,
    workers: 0,
    allowSmt: true,
    allowLowPerf: false,
    placement: "preferred" as const,
    enableWarmup: false,
    enableAdaptiveDown: true,
    chunkSize: 64,
    progressInterval: 1000,
    sampleWindowMs: 2000,
    adaptiveMinWorkers: 1,
    adaptiveDropThreshold: 0.12,
    adaptiveDropWindows: 3,
    adaptiveCooldownMs: 8000,
  },
  constraints: {
    required: [],
    forbidden: [],
    distance: [],
    count: [],
  },
};

function createMemoryStorage(seed?: Record<string, string>): SearchSessionStorage {
  const data = new Map(Object.entries(seed ?? {}));
  return {
    getItem(key) {
      return data.get(key) ?? null;
    },
    setItem(key, value) {
      data.set(key, value);
    },
    removeItem(key) {
      data.delete(key);
    },
  };
}

function createMatch(seed: number): SearchMatchSummary {
  return {
    seed,
    worldType: 13,
    mixing: 625,
    coord: `V-SNDST-C-${seed}`,
    traits: [],
    start: { x: 10, y: 20 },
    worldSize: { w: 256, h: 384 },
    geysers: [],
    nearestDistance: 12.5,
  };
}

function createRequest(): SearchRequest {
  return {
    jobId: "search-001",
    worldType: 13,
    seedStart: 100000,
    seedEnd: 120000,
    mixing: 625,
    threads: 0,
    cpu: { ...SAMPLE_DRAFT.cpu },
    constraints: {
      required: [],
      forbidden: [],
      distance: [],
      count: [],
    },
  };
}

test("restoreSearchSessionSnapshot restores results and draft from persisted snapshot", () => {
  const storage = createMemoryStorage();
  const results = [createMatch(100123), createMatch(100456)];
  const request = createRequest();

  persistSearchSessionSnapshot(storage, {
    draft: SAMPLE_DRAFT,
    results,
    selectedSeed: 100456,
    lastSubmittedRequest: request,
  });

  const restored = restoreSearchSessionSnapshot(storage);
  assert.ok(restored);
  assert.equal(restored.activeWorldType, SAMPLE_DRAFT.worldType);
  assert.equal(restored.activeMixing, SAMPLE_DRAFT.mixing);
  assert.equal(restored.totalMatches, 2);
  assert.equal(restored.selectedSeed, 100456);
  assert.deepEqual(restored.results, results);
  assert.deepEqual(restored.lastSubmittedRequest, request);
});

test("restoreSearchSessionSnapshot clears invalid selected seed when result set changed", () => {
  const storage = createMemoryStorage();
  persistSearchSessionSnapshot(storage, {
    draft: SAMPLE_DRAFT,
    results: [createMatch(100123)],
    selectedSeed: 999999,
    lastSubmittedRequest: null,
  });

  const restored = restoreSearchSessionSnapshot(storage);
  assert.ok(restored);
  assert.equal(restored.selectedSeed, null);
});

test("restoreSearchSessionSnapshot removes corrupted session payload", () => {
  const storage = createMemoryStorage({
    [SEARCH_SESSION_STORAGE_KEY]: "{not-json",
  });

  const restored = restoreSearchSessionSnapshot(storage);
  assert.equal(restored, null);
  assert.equal(storage.getItem(SEARCH_SESSION_STORAGE_KEY), null);
});

test("restoreSearchSessionSnapshot normalizes legacy warmup flag to desktop default", () => {
  const storage = createMemoryStorage();
  const request = createRequest();

  persistSearchSessionSnapshot(storage, {
    draft: {
      ...SAMPLE_DRAFT,
      cpu: {
        ...SAMPLE_DRAFT.cpu,
        enableWarmup: true,
      },
    },
    results: [createMatch(100123)],
    selectedSeed: 100123,
    lastSubmittedRequest: {
      ...request,
      cpu: {
        ...request.cpu,
        enableWarmup: true,
      },
    },
  });

  const restored = restoreSearchSessionSnapshot(storage);
  assert.ok(restored);
  assert.equal(restored.draft.cpu.enableWarmup, false);
  assert.equal(restored.lastSubmittedRequest?.cpu?.enableWarmup, false);
  assert.equal(restored.draft.cpu.enableAdaptiveDown, SAMPLE_DRAFT.cpu.enableAdaptiveDown);
});
