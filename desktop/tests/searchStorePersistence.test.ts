import test from "node:test";
import assert from "node:assert/strict";

import type { SearchMatchSummary, SearchRequest } from "../src/lib/contracts";
import {
  SEARCH_SESSION_STORAGE_KEY,
  persistSearchSessionSnapshot,
  restoreSearchSessionSnapshot,
  type SearchSessionStorage,
} from "../src/state/searchStorePersistence.ts";
import { DEFAULT_SEARCH_DRAFT } from "../src/state/searchStore.ts";
import { decodeMixingToLevels } from "../src/features/search/searchSchema.ts";

const SAMPLE_DRAFT = {
  worldType: 13,
  seedStart: 100000,
  seedEnd: 120000,
  mixing: 625,
  cpu: {
    mode: "balanced" as const,
    allowSmt: true,
    allowLowPerf: false,
    placement: "strict" as const,
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
    coord: `V-SNDST-C-${seed}-0-D3-DH`,
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

test("restoreSearchSessionSnapshot drops legacy v1 session snapshots", () => {
  const storage = createMemoryStorage({
    "oni-search-session/v1": JSON.stringify({
      version: 1,
      draft: SAMPLE_DRAFT,
      results: [createMatch(100123)],
      selectedSeed: 100123,
      lastSubmittedRequest: createRequest(),
    }),
  });

  const restored = restoreSearchSessionSnapshot(storage);
  assert.equal(restored, null);
  assert.equal(storage.getItem(SEARCH_SESSION_STORAGE_KEY), null);
});

test("restoreSearchSessionSnapshot removes v2 snapshots with non-canonical coord", () => {
  const storage = createMemoryStorage({
    [SEARCH_SESSION_STORAGE_KEY]: JSON.stringify({
      version: 2,
      draft: SAMPLE_DRAFT,
      results: [
        {
          ...createMatch(100123),
          coord: "V-SNDST-C-100123",
        },
      ],
      selectedSeed: 100123,
      lastSubmittedRequest: createRequest(),
    }),
  });

  const restored = restoreSearchSessionSnapshot(storage);
  assert.equal(restored, null);
  assert.equal(storage.getItem(SEARCH_SESSION_STORAGE_KEY), null);
});

test("restoreSearchSessionSnapshot normalizes legacy cpu fields to unified surface", () => {
  const storage = createMemoryStorage();
  const request = createRequest();

  persistSearchSessionSnapshot(storage, {
    draft: {
      ...SAMPLE_DRAFT,
      cpu: {
        ...SAMPLE_DRAFT.cpu,
        mode: "custom" as never,
        workers: 6,
        enableWarmup: true,
        enableAdaptiveDown: true,
      },
    },
    results: [createMatch(100123)],
    selectedSeed: 100123,
    lastSubmittedRequest: {
      ...request,
      cpu: {
        ...request.cpu,
        mode: "custom" as never,
        workers: 6,
        enableWarmup: true,
        enableAdaptiveDown: true,
      },
    },
  });

  const restored = restoreSearchSessionSnapshot(storage);
  assert.ok(restored);
  assert.equal("threads" in restored.draft, false);
  assert.equal(
    restored.lastSubmittedRequest ? "threads" in restored.lastSubmittedRequest : false,
    false
  );
  assert.deepEqual(restored.draft.cpu, {
    ...SAMPLE_DRAFT.cpu,
    mode: "turbo",
  });
  assert.deepEqual(restored.lastSubmittedRequest?.cpu, {
    ...SAMPLE_DRAFT.cpu,
    mode: "turbo",
  });
});

test("restoreSearchSessionSnapshot migrates legacy preferred placement to strict", () => {
  const storage = createMemoryStorage();
  const request = createRequest();

  persistSearchSessionSnapshot(storage, {
    draft: {
      ...SAMPLE_DRAFT,
      cpu: {
        ...SAMPLE_DRAFT.cpu,
        placement: "preferred" as never,
      },
    },
    results: [createMatch(100123)],
    selectedSeed: 100123,
    lastSubmittedRequest: {
      ...request,
      cpu: {
        ...request.cpu,
        placement: "preferred" as never,
      },
    },
  });

  const restored = restoreSearchSessionSnapshot(storage);
  assert.ok(restored);
  assert.equal(restored.draft.cpu.placement, "strict");
  assert.equal(restored.lastSubmittedRequest?.cpu?.placement, "strict");
});

test("desktop default draft starts from seed 0 to 10000", () => {
  assert.equal(DEFAULT_SEARCH_DRAFT.seedStart, 0);
  assert.equal(DEFAULT_SEARCH_DRAFT.seedEnd, 10000);
});

test("desktop default draft keeps prehistoric package unchecked", () => {
  const levels = decodeMixingToLevels(DEFAULT_SEARCH_DRAFT.mixing, 11);

  assert.equal(levels[6], 0);
});
