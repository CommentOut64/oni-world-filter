import test, { afterEach, beforeEach } from "node:test";
import assert from "node:assert/strict";
import { webcrypto } from "node:crypto";

import type { PreviewPayload, SearchMatchSummary } from "../src/lib/contracts.ts";
import type { PreviewStoreSnapshot } from "../src/state/previewStoreState.ts";
import { primeResolvedPreviewState } from "../src/state/previewStoreState.ts";

type SearchStoreModule = typeof import("../src/state/searchStore.ts");

interface MemoryStorage {
  getItem(key: string): string | null;
  setItem(key: string, value: string): void;
  removeItem(key: string): void;
  clear(): void;
}

function createMemoryStorage(): MemoryStorage {
  const data = new Map<string, string>();
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
    clear() {
      data.clear();
    },
  };
}

const sessionStorage = createMemoryStorage();
const localStorage = createMemoryStorage();

function ensureWindow(): void {
  const currentWindow = (globalThis as { window?: Record<string, unknown> }).window ?? {};
  currentWindow.crypto = webcrypto;
  currentWindow.sessionStorage = sessionStorage;
  currentWindow.localStorage = localStorage;
  (globalThis as { window?: unknown }).window = currentWindow;
}

async function loadSearchStoreModule(): Promise<SearchStoreModule> {
  ensureWindow();
  return import("../src/state/searchStore.ts");
}

function createMatch(seed = 123456): SearchMatchSummary {
  return {
    seed,
    worldType: 13,
    mixing: 625,
    coord: `V-SNDST-C-${seed}-0-D3-HD`,
    traits: [1, 2],
    start: { x: 10, y: 20 },
    worldSize: { w: 256, h: 384 },
    geysers: [
      { type: 0, x: 11, y: 21 },
      { type: 6, x: 15, y: 25 },
    ],
    nearestDistance: 1.4,
  };
}

function createPreview(seed = 123456): PreviewPayload {
  return {
    summary: {
      seed,
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
}

beforeEach(() => {
  sessionStorage.clear();
  localStorage.clear();
  ensureWindow();
});

afterEach(async () => {
  const searchModule = await loadSearchStoreModule();

  searchModule.useSearchStore.setState({
    isSearching: false,
    isCancelling: false,
    activeJobId: null,
    activeWorldType: searchModule.DEFAULT_SEARCH_DRAFT.worldType,
    activeMixing: searchModule.DEFAULT_SEARCH_DRAFT.mixing,
    results: [],
    selectedSeed: null,
    draft: searchModule.DEFAULT_SEARCH_DRAFT,
    lastSubmittedRequest: null,
    lastHostDebugMessages: [],
    stats: {
      startedAtMs: null,
      processedSeeds: 0,
      totalSeeds: 0,
      totalMatches: 0,
      activeWorkers: 0,
      currentSeedsPerSecond: 0,
      peakSeedsPerSecond: 0,
    },
    lastError: null,
  });
});

test("openDirectCoordResult clears search runtime state but keeps the existing draft", async () => {
  const module = await loadSearchStoreModule();
  const match = createMatch();
  const preservedDraft = {
    ...module.DEFAULT_SEARCH_DRAFT,
    worldType: 26,
    seedStart: 300000,
    seedEnd: 350000,
    mixing: 1250,
  };

  module.useSearchStore.setState({
    isSearching: true,
    isCancelling: true,
    activeJobId: "search-job-001",
    activeWorldType: 5,
    activeMixing: 5,
    results: [createMatch(111111)],
    selectedSeed: 111111,
    draft: preservedDraft,
    lastSubmittedRequest: {
      jobId: "search-job-001",
      worldType: 5,
      seedStart: 1,
      seedEnd: 10,
      mixing: 5,
      constraints: {
        required: [],
        forbidden: [],
        distance: [],
        count: [],
      },
      cpu: { ...preservedDraft.cpu },
    },
    stats: {
      startedAtMs: 123,
      processedSeeds: 7,
      totalSeeds: 9,
      totalMatches: 2,
      activeWorkers: 3,
      currentSeedsPerSecond: 99,
      peakSeedsPerSecond: 101,
    },
    lastError: "old error",
  });

  module.useSearchStore.getState().openDirectCoordResult(match);

  const state = module.useSearchStore.getState();
  assert.equal(state.isSearching, false);
  assert.equal(state.isCancelling, false);
  assert.equal(state.activeJobId, null);
  assert.equal(state.activeWorldType, 13);
  assert.equal(state.activeMixing, 625);
  assert.deepEqual(state.results, [match]);
  assert.equal(state.selectedSeed, 123456);
  assert.equal(state.lastSubmittedRequest, null);
  assert.equal(state.lastError, null);
  assert.deepEqual(state.draft, preservedDraft);
  assert.deepEqual(state.stats, {
    startedAtMs: null,
    processedSeeds: 0,
    totalSeeds: 0,
    totalMatches: 1,
    activeWorkers: 0,
    currentSeedsPerSecond: 0,
    peakSeedsPerSecond: 0,
  });
});

test("primeResolvedPreviewState stores preview in cache and makes it active", () => {
  const match = createMatch();
  const preview = createPreview();
  const initialState: PreviewStoreSnapshot = {
    activeKey: null,
    activePreview: null,
    cache: {},
    isLoading: false,
    lastError: "old error",
  };

  const state = primeResolvedPreviewState(initialState, match, preview);
  assert.equal(state.activeKey, "13:123456:625");
  assert.deepEqual(state.activePreview, preview);
  assert.deepEqual(state.cache, {
    "13:123456:625": preview,
  });
  assert.equal(state.isLoading, false);
  assert.equal(state.lastError, null);
});
