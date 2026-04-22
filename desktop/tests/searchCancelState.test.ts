import test, { afterEach, beforeEach } from "node:test";
import assert from "node:assert/strict";
import { webcrypto } from "node:crypto";

import { clearMocks, mockIPC } from "@tauri-apps/api/mocks";

import type {
  SearchCancelledEvent,
  SearchCompletedEvent,
  SearchFailedEvent,
  SearchMatchEvent,
  SearchProgressEvent,
} from "../src/lib/contracts.ts";

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

function createBaseState(
  module: SearchStoreModule,
  jobId = "job-search-001"
): Partial<ReturnType<SearchStoreModule["useSearchStore"]["getState"]>> {
  return {
    worlds: [{ id: 13, code: "V-SNDST-C-" }],
    geysers: [],
    isSearching: true,
    isCancelling: false,
    activeJobId: jobId,
    activeWorldType: 13,
    activeMixing: module.DEFAULT_SEARCH_DRAFT.mixing,
    results: [],
    selectedSeed: null,
    draft: module.DEFAULT_SEARCH_DRAFT,
    lastSubmittedRequest: null,
    lastHostDebugMessages: [],
    stats: {
      startedAtMs: 123,
      processedSeeds: 0,
      totalSeeds: 0,
      totalMatches: 0,
      activeWorkers: 0,
      currentSeedsPerSecond: 0,
      peakSeedsPerSecond: 0,
    },
    lastError: null,
  };
}

function createMatchEvent(jobId: string): SearchMatchEvent {
  return {
    event: "match",
    jobId,
    seed: 100123,
    processedSeeds: 9,
    totalSeeds: 200,
    totalMatches: 1,
    summary: {
      start: { x: 12, y: 34 },
      worldSize: { w: 256, h: 384 },
      traits: [],
      geysers: [],
    },
  };
}

function createCancelledEvent(jobId: string): SearchCancelledEvent {
  return {
    event: "cancelled",
    jobId,
    processedSeeds: 9,
    totalSeeds: 200,
    totalMatches: 1,
    finalActiveWorkers: 0,
  };
}

function createProgressEvent(jobId: string): SearchProgressEvent {
  return {
    event: "progress",
    jobId,
    processedSeeds: 12,
    totalSeeds: 200,
    totalMatches: 2,
    activeWorkers: 3,
    hasWindowSample: true,
    windowSeedsPerSecond: 321,
    activeWorkersReduced: false,
    peakSeedsPerSecond: 456,
  };
}

beforeEach(() => {
  sessionStorage.clear();
  localStorage.clear();
  ensureWindow();
});

afterEach(() => {
  clearMocks();
});

test("cancelSearchJob keeps cancelling state until cancelled event and drops late progress and match", async () => {
  mockIPC((cmd) => {
    if (cmd === "cancel_search") {
      return null;
    }
    throw new Error(`unexpected command: ${cmd}`);
  });

  const { useSearchStore } = await loadSearchStoreModule();
  const baseState = createBaseState(await loadSearchStoreModule());
  useSearchStore.setState(baseState);

  await useSearchStore.getState().cancelSearchJob();

  const cancellingState = useSearchStore.getState();
  assert.equal(cancellingState.isSearching, true);
  assert.equal(cancellingState.isCancelling, true);
  assert.equal(cancellingState.activeJobId, "job-search-001");

  useSearchStore.getState().ingestSidecarEvent(createProgressEvent("job-search-001"));
  useSearchStore.getState().ingestSidecarEvent(createMatchEvent("job-search-001"));
  const frozenState = useSearchStore.getState();
  assert.equal(frozenState.results.length, 0);
  assert.equal(frozenState.stats.processedSeeds, 0);
  assert.equal(frozenState.stats.totalMatches, 0);

  useSearchStore.getState().ingestSidecarEvent(createCancelledEvent("job-search-001"));
  const finalState = useSearchStore.getState();
  assert.equal(finalState.isSearching, false);
  assert.equal(finalState.isCancelling, false);
  assert.equal(finalState.activeJobId, null);
  assert.equal(finalState.results.length, 0);
  assert.equal(finalState.stats.totalMatches, 1);
});

test("terminal events should clear cancelling state and active job", async () => {
  mockIPC(() => {
    throw new Error("unexpected IPC call");
  });

  const module = await loadSearchStoreModule();
  const { useSearchStore } = module;

  const completedEvent: SearchCompletedEvent = {
    event: "completed",
    jobId: "job-terminal",
    processedSeeds: 20,
    totalSeeds: 20,
    totalMatches: 2,
    finalActiveWorkers: 1,
    autoFallbackCount: 0,
    stoppedByBudget: false,
    throughput: {
      averageSeedsPerSecond: 123,
      stddevSeedsPerSecond: 0,
      processedSeeds: 20,
      valid: true,
    },
  };

  const failedEvent: SearchFailedEvent = {
    event: "failed",
    jobId: "job-terminal",
    message: "search failed",
  };

  useSearchStore.setState({
    ...createBaseState(module, "job-terminal"),
    isCancelling: true,
    stats: {
      startedAtMs: 123,
      processedSeeds: 12,
      totalSeeds: 20,
      totalMatches: 2,
      activeWorkers: 3,
      currentSeedsPerSecond: 321,
      peakSeedsPerSecond: 456,
    },
  });
  useSearchStore.getState().ingestSidecarEvent(completedEvent);
  assert.equal(useSearchStore.getState().isCancelling, false);
  assert.equal(useSearchStore.getState().activeJobId, null);
  assert.equal(useSearchStore.getState().stats.currentSeedsPerSecond, 0);
  assert.equal(useSearchStore.getState().stats.peakSeedsPerSecond, 456);

  useSearchStore.setState({
    ...createBaseState(module, "job-terminal"),
    isCancelling: true,
    stats: {
      startedAtMs: 123,
      processedSeeds: 12,
      totalSeeds: 20,
      totalMatches: 2,
      activeWorkers: 3,
      currentSeedsPerSecond: 321,
      peakSeedsPerSecond: 456,
    },
  });
  useSearchStore.getState().ingestSidecarEvent(createCancelledEvent("job-terminal"));
  assert.equal(useSearchStore.getState().isSearching, false);
  assert.equal(useSearchStore.getState().isCancelling, false);
  assert.equal(useSearchStore.getState().activeJobId, null);
  assert.equal(useSearchStore.getState().stats.currentSeedsPerSecond, 0);
  assert.equal(useSearchStore.getState().stats.peakSeedsPerSecond, 456);

  useSearchStore.setState({
    ...createBaseState(module, "job-terminal"),
    isCancelling: true,
    stats: {
      startedAtMs: 123,
      processedSeeds: 12,
      totalSeeds: 20,
      totalMatches: 2,
      activeWorkers: 3,
      currentSeedsPerSecond: 321,
      peakSeedsPerSecond: 456,
    },
  });
  useSearchStore.getState().ingestSidecarEvent(failedEvent);
  assert.equal(useSearchStore.getState().isSearching, false);
  assert.equal(useSearchStore.getState().isCancelling, false);
  assert.equal(useSearchStore.getState().activeJobId, null);
  assert.equal(useSearchStore.getState().stats.currentSeedsPerSecond, 0);
  assert.equal(useSearchStore.getState().stats.peakSeedsPerSecond, 456);
});

test("cancelled event should not roll processed seeds back to zero", async () => {
  mockIPC(() => {
    throw new Error("unexpected IPC call");
  });

  const module = await loadSearchStoreModule();
  const { useSearchStore } = module;

  useSearchStore.setState({
    ...createBaseState(module, "job-cancel-zero"),
    isCancelling: true,
    stats: {
      startedAtMs: 123,
      processedSeeds: 12,
      totalSeeds: 20,
      totalMatches: 2,
      activeWorkers: 3,
      currentSeedsPerSecond: 321,
      peakSeedsPerSecond: 456,
    },
  });

  useSearchStore.getState().ingestSidecarEvent({
    event: "cancelled",
    jobId: "job-cancel-zero",
    processedSeeds: 0,
    totalSeeds: 20,
    totalMatches: 2,
    finalActiveWorkers: 0,
  });

  const finalState = useSearchStore.getState();
  assert.equal(finalState.isSearching, false);
  assert.equal(finalState.isCancelling, false);
  assert.equal(finalState.activeJobId, null);
  assert.equal(finalState.stats.processedSeeds, 12);
  assert.equal(finalState.stats.totalSeeds, 20);
  assert.equal(finalState.stats.totalMatches, 2);
  assert.equal(finalState.stats.currentSeedsPerSecond, 0);
});
