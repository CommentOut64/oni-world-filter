import test, { afterEach, beforeEach } from "node:test";
import assert from "node:assert/strict";
import { webcrypto } from "node:crypto";

import { clearMocks, mockIPC } from "@tauri-apps/api/mocks";

import type { PreviewPayload, SearchMatchSummary } from "../src/lib/contracts.ts";

type PreviewStoreModule = typeof import("../src/state/previewStore.ts");

interface MemoryStorage {
  getItem(key: string): string | null;
  setItem(key: string, value: string): void;
  removeItem(key: string): void;
  clear(): void;
}

interface Deferred<T> {
  promise: Promise<T>;
  resolve: (value: T) => void;
  reject: (error: unknown) => void;
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

function createDeferred<T>(): Deferred<T> {
  let resolve!: (value: T) => void;
  let reject!: (error: unknown) => void;
  const promise = new Promise<T>((res, rej) => {
    resolve = res;
    reject = rej;
  });
  return { promise, resolve, reject };
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

async function loadPreviewStoreModule(): Promise<PreviewStoreModule> {
  ensureWindow();
  return import("../src/state/previewStore.ts");
}

function createMatch(seed = 123456, mixing = 625): SearchMatchSummary {
  return {
    seed,
    worldType: 13,
    mixing,
    coord: `V-SNDST-C-${seed}-0-D3-0`,
    traits: [1, 2],
    start: { x: 10, y: 20 },
    worldSize: { w: 256, h: 384 },
    geysers: [
      { type: 0, x: 11, y: 21, id: "steam" },
      { type: 6, x: 15, y: 25, id: "salt_water" },
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
        { type: 0, x: 11, y: 21, id: "steam" },
        { type: 6, x: 15, y: 25, id: "salt_water" },
      ],
    },
    polygons: [],
  };
}

function createDetailsEvent(seed: number) {
  return {
    event: "preview_geyser_details" as const,
    jobId: `details-${seed}`,
    worldType: 13,
    seed,
    mixing: 625,
    geyserDetails: [
      {
        index: 0,
        summary: { type: 0, x: 11, y: 21, id: "steam" },
        hasParameters: true,
        parameterKind: "geyser",
        native: {
          averageActiveYieldKgPerCycle: 1400,
          eruptionPeriodSeconds: 600,
          eruptionRatio: 0.5,
          activePeriodSeconds: 60000,
          activeRatio: 0.6,
        },
        derived: {
          eruptionRateKgPerSecond: 4.2,
          averageOverallYieldGPerSecond: 2100,
          eruptionSeconds: 300,
          activeSeconds: 36000,
          activeCycles: 60,
          totalCycles: 100,
          temperatureCelsius: 110,
        },
      },
    ],
  };
}

async function flushMicrotasks(): Promise<void> {
  await Promise.resolve();
  await Promise.resolve();
  await new Promise((resolve) => setTimeout(resolve, 0));
}

beforeEach(() => {
  sessionStorage.clear();
  localStorage.clear();
  ensureWindow();
});

afterEach(async () => {
  const module = await loadPreviewStoreModule();
  module.usePreviewStore.setState({
    activeKey: null,
    activePreview: null,
    activeGeyserDetailsStatus: "idle",
    activeGeyserDetails: [],
    activeGeyserDetailsError: null,
    cache: {},
    inflightPreviewKeys: {},
    inflightDetailKeys: {},
    requestSerial: 0,
    isLoading: false,
    lastError: null,
  });
  clearMocks();
});

test("loadByMatch loads preview first and then hydrates geyser details for the active key", async () => {
  const previewDeferred = createDeferred<{ preview: PreviewPayload }>();
  const detailDeferred = createDeferred<ReturnType<typeof createDetailsEvent>>();
  const detailRequests: Array<Record<string, unknown>> = [];

  mockIPC(async (cmd, payload) => {
    if (cmd === "load_preview") {
      return previewDeferred.promise;
    }
    if (cmd === "load_preview_geyser_details") {
      detailRequests.push(payload.request as Record<string, unknown>);
      return detailDeferred.promise;
    }
    throw new Error(`unexpected command: ${cmd}`);
  });

  const module = await loadPreviewStoreModule();
  const match = createMatch();
  const loadPromise = module.usePreviewStore.getState().loadByMatch(match);

  previewDeferred.resolve({
    preview: createPreview(match.seed),
  });
  await loadPromise;

  let state = module.usePreviewStore.getState();
  assert.equal(state.activePreview?.summary.seed, match.seed);
  assert.equal(state.activeGeyserDetailsStatus, "loading");
  assert.deepEqual(state.activeGeyserDetails, []);
  assert.equal(state.cache["13:123456:625:primary"]?.preview.summary.seed, match.seed);
  assert.deepEqual(
    Object.keys(detailRequests[0] ?? {}).sort(),
    ["jobId", "mixing", "seed", "target", "worldType"],
    "geyser detail request should only carry canonical key fields plus target"
  );
  assert.equal(detailRequests[0]?.target, "primary");
  assert.equal(detailRequests[0]?.worldHeight, undefined);
  assert.equal(detailRequests[0]?.geysers, undefined);

  detailDeferred.resolve(createDetailsEvent(match.seed));
  await flushMicrotasks();

  state = module.usePreviewStore.getState();
  assert.equal(state.activeGeyserDetailsStatus, "ready");
  assert.equal(state.activeGeyserDetails.length, 1);
  assert.equal(state.activeGeyserDetails[0]?.summary.id, "steam");
  assert.equal(
    state.cache["13:123456:625:primary"]?.geyserDetailsStatus,
    "ready",
    "detail cache should become ready after hydration"
  );
});

test("loadByMatch dedupes repeated same-key preview and geyser detail requests", async () => {
  const previewDeferred = createDeferred<{ preview: PreviewPayload }>();
  const detailDeferred = createDeferred<ReturnType<typeof createDetailsEvent>>();
  let previewCalls = 0;
  let detailCalls = 0;

  mockIPC(async (cmd) => {
    if (cmd === "load_preview") {
      previewCalls += 1;
      return previewDeferred.promise;
    }
    if (cmd === "load_preview_geyser_details") {
      detailCalls += 1;
      return detailDeferred.promise;
    }
    throw new Error(`unexpected command: ${cmd}`);
  });

  const module = await loadPreviewStoreModule();
  const match = createMatch();

  const first = module.usePreviewStore.getState().loadByMatch(match);
  const second = module.usePreviewStore.getState().loadByMatch(match);

  previewDeferred.resolve({
    preview: createPreview(match.seed),
  });
  await Promise.all([first, second]);

  detailDeferred.resolve(createDetailsEvent(match.seed));
  await flushMicrotasks();

  assert.equal(previewCalls, 1, "same key should reuse one inflight preview request");
  assert.equal(detailCalls, 1, "same key should reuse one inflight detail request");
});

test("stale preview and detail results can warm cache but must not overwrite the latest active key", async () => {
  const previewDeferredBySeed = new Map<number, Deferred<{ preview: PreviewPayload }>>();
  const detailDeferredBySeed = new Map<number, Deferred<ReturnType<typeof createDetailsEvent>>>();

  mockIPC(async (cmd, payload) => {
    const request = payload.request as { seed: number };
    if (cmd === "load_preview") {
      const deferred = createDeferred<{ preview: PreviewPayload }>();
      previewDeferredBySeed.set(request.seed, deferred);
      return deferred.promise;
    }
    if (cmd === "load_preview_geyser_details") {
      const deferred = createDeferred<ReturnType<typeof createDetailsEvent>>();
      detailDeferredBySeed.set(request.seed, deferred);
      return deferred.promise;
    }
    throw new Error(`unexpected command: ${cmd}`);
  });

  const module = await loadPreviewStoreModule();
  const matchA = createMatch(111111);
  const matchB = createMatch(222222);
  const matchC = createMatch(333333);

  void module.usePreviewStore.getState().loadByMatch(matchA);
  void module.usePreviewStore.getState().loadByMatch(matchB);
  void module.usePreviewStore.getState().loadByMatch(matchC);

  previewDeferredBySeed.get(111111)?.resolve({ preview: createPreview(111111) });
  await flushMicrotasks();
  previewDeferredBySeed.get(222222)?.resolve({ preview: createPreview(222222) });
  await flushMicrotasks();
  previewDeferredBySeed.get(333333)?.resolve({ preview: createPreview(333333) });
  await flushMicrotasks();

  detailDeferredBySeed.get(111111)?.resolve(createDetailsEvent(111111));
  await flushMicrotasks();
  detailDeferredBySeed.get(222222)?.resolve(createDetailsEvent(222222));
  await flushMicrotasks();

  let state = module.usePreviewStore.getState();
  assert.equal(state.activeKey, "13:333333:625:primary");
  assert.equal(state.activePreview?.summary.seed, 333333);
  assert.equal(state.activeGeyserDetailsStatus, "loading");
  assert.equal(state.cache["13:111111:625:primary"]?.preview.summary.seed, 111111);
  assert.equal(state.cache["13:222222:625:primary"]?.geyserDetailsStatus, "ready");

  detailDeferredBySeed.get(333333)?.resolve(createDetailsEvent(333333));
  await flushMicrotasks();

  state = module.usePreviewStore.getState();
  assert.equal(state.activeKey, "13:333333:625:primary");
  assert.equal(state.activePreview?.summary.seed, 333333);
  assert.equal(state.activeGeyserDetailsStatus, "ready");
  assert.equal(state.activeGeyserDetails[0]?.summary.id, "steam");
});

test("geyser detail failure should not clear the active preview", async () => {
  const previewDeferred = createDeferred<{ preview: PreviewPayload }>();
  const detailDeferred = createDeferred<ReturnType<typeof createDetailsEvent>>();

  mockIPC(async (cmd) => {
    if (cmd === "load_preview") {
      return previewDeferred.promise;
    }
    if (cmd === "load_preview_geyser_details") {
      return detailDeferred.promise;
    }
    throw new Error(`unexpected command: ${cmd}`);
  });

  const module = await loadPreviewStoreModule();
  const match = createMatch();
  const loadPromise = module.usePreviewStore.getState().loadByMatch(match);

  previewDeferred.resolve({
    preview: createPreview(match.seed),
  });
  await loadPromise;

  detailDeferred.reject(new Error("detail sidecar failed"));
  await flushMicrotasks();

  const state = module.usePreviewStore.getState();
  assert.equal(state.activePreview?.summary.seed, match.seed);
  assert.equal(state.activeGeyserDetailsStatus, "failed");
  assert.equal(state.activeGeyserDetailsError, "detail sidecar failed");
  assert.equal(state.lastError, null, "detail failure should not replace preview error channel");
});
