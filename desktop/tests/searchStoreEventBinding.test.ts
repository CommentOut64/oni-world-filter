import test, { afterEach, beforeEach } from "node:test";
import assert from "node:assert/strict";
import { webcrypto } from "node:crypto";

import { emit } from "@tauri-apps/api/event";
import { clearMocks, mockIPC } from "@tauri-apps/api/mocks";

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

function createCompletedEvent(jobId: string) {
  return {
    event: "completed" as const,
    jobId,
    processedSeeds: 1,
    totalSeeds: 1,
    totalMatches: 1,
    finalActiveWorkers: 0,
    autoFallbackCount: 0,
    stoppedByBudget: false,
    throughput: {
      averageSeedsPerSecond: 1,
      stddevSeedsPerSecond: 0,
      processedSeeds: 1,
      valid: true,
    },
  };
}

beforeEach(() => {
  sessionStorage.clear();
  localStorage.clear();
  ensureWindow();
});

afterEach(async () => {
  const module = await loadSearchStoreModule();
  module.disposeSidecarListener();
  module.useSearchStore.setState({
    listening: false,
    bindingSidecar: false,
    isSearching: false,
    isCancelling: false,
    activeJobId: null,
    results: [],
    selectedSeed: null,
    lastSubmittedRequest: null,
    lastHostDebugMessages: [],
    lastError: null,
  });
  clearMocks();
});

test("startSearchJob binds sidecar listener before invoking start_search", async () => {
  mockIPC(
    async (cmd, payload) => {
      if (cmd !== "start_search") {
        throw new Error(`unexpected command: ${cmd}`);
      }

      const request = payload.request as { jobId: string };
      await emit("sidecar://event", {
        event: "match",
        jobId: request.jobId,
        seed: 100123,
        processedSeeds: 1,
        totalSeeds: 1,
        totalMatches: 1,
        summary: {
          start: { x: 12, y: 34 },
          worldSize: { w: 256, h: 384 },
          traits: [],
          geysers: [],
        },
      });
      await emit("sidecar://event", createCompletedEvent(request.jobId));
      return null;
    },
    { shouldMockEvents: true }
  );

  const module = await loadSearchStoreModule();
  const { useSearchStore, DEFAULT_SEARCH_DRAFT } = module;
  useSearchStore.setState({
    worlds: [{ id: 13, code: "V-SNDST-C-" }],
    geysers: [],
    listening: false,
    bindingSidecar: false,
    isSearching: false,
    isCancelling: false,
    activeJobId: null,
    results: [],
    selectedSeed: null,
    draft: DEFAULT_SEARCH_DRAFT,
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

  const started = await useSearchStore.getState().startSearchJob(DEFAULT_SEARCH_DRAFT);

  assert.equal(started, true);
  assert.equal(useSearchStore.getState().listening, true);
  assert.equal(useSearchStore.getState().results.length, 1);
  assert.equal(useSearchStore.getState().results[0]?.seed, 100123);
  assert.equal(useSearchStore.getState().isSearching, false);
  assert.equal(useSearchStore.getState().stats.totalMatches, 1);
});

test("progress sidecar event updates visible search stats while search is running", async () => {
  mockIPC(
    async (cmd) => {
      if (cmd !== "start_search") {
        throw new Error(`unexpected command: ${cmd}`);
      }
      return null;
    },
    { shouldMockEvents: true }
  );

  const module = await loadSearchStoreModule();
  const { useSearchStore, DEFAULT_SEARCH_DRAFT } = module;
  useSearchStore.setState({
    worlds: [{ id: 13, code: "V-SNDST-C-" }],
    geysers: [],
    listening: false,
    bindingSidecar: false,
    isSearching: false,
    isCancelling: false,
    activeJobId: null,
    results: [],
    selectedSeed: null,
    draft: DEFAULT_SEARCH_DRAFT,
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

  const started = await useSearchStore.getState().startSearchJob(DEFAULT_SEARCH_DRAFT);
  const jobId = useSearchStore.getState().activeJobId;
  assert.equal(started, true);
  assert.ok(jobId, "search should keep an active job id while running");

  await emit("sidecar://event", {
    event: "started",
    jobId,
    seedStart: 0,
    seedEnd: 10,
    totalSeeds: 11,
    workerCount: 4,
  });
  await emit("sidecar://event", {
    event: "progress",
    jobId,
    processedSeeds: 5,
    totalSeeds: 11,
    totalMatches: 0,
    activeWorkers: 4,
    hasWindowSample: true,
    windowSeedsPerSecond: 12.5,
    activeWorkersReduced: false,
    peakSeedsPerSecond: 12.5,
  });

  const state = useSearchStore.getState();
  assert.equal(state.isSearching, true);
  assert.equal(state.stats.processedSeeds, 5);
  assert.equal(state.stats.totalSeeds, 11);
  assert.equal(state.stats.totalMatches, 0);
  assert.equal(state.stats.activeWorkers, 4);
  assert.equal(state.stats.currentSeedsPerSecond, 12.5);
  assert.equal(state.stats.peakSeedsPerSecond, 12.5);
});

test("sidecar stderr diagnostics do not become visible backend errors", async () => {
  mockIPC(async () => null, { shouldMockEvents: true });

  const module = await loadSearchStoreModule();
  const { useSearchStore } = module;
  await useSearchStore.getState().bindSidecarEvents();

  await emit("sidecar://stderr", {
    jobId: "search-1777015977549-mdntey",
    message: "[sidecar-diagnostic] search worker started jobId=search-1777015977549-mdntey",
  });

  assert.equal(useSearchStore.getState().lastError, null);
});

test("real sidecar stderr errors still become visible backend errors", async () => {
  mockIPC(async () => null, { shouldMockEvents: true });

  const module = await loadSearchStoreModule();
  const { useSearchStore } = module;
  await useSearchStore.getState().bindSidecarEvents();

  await emit("sidecar://stderr", {
    jobId: "search-1777015977549-mdntey",
    message: "sidecar 进程异常退出(code=Some(-1073741819))",
  });

  assert.equal(
    useSearchStore.getState().lastError,
    "[search-1777015977549-mdntey] sidecar 进程异常退出(code=Some(-1073741819))"
  );
});
