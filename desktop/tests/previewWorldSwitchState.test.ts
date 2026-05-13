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

interface DeferredValue<T> {
  promise: Promise<T>;
  resolve: (value: T) => void;
  reject: (reason?: unknown) => void;
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

function createDeferredValue<T>(): DeferredValue<T> {
  let resolve!: (value: T) => void;
  let reject!: (reason?: unknown) => void;
  const promise = new Promise<T>((nextResolve, nextReject) => {
    resolve = nextResolve;
    reject = nextReject;
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

function createMatch(seed = 123456): SearchMatchSummary {
  return {
    seed,
    worldType: 13,
    mixing: 625,
    coord: `M-SWMP-C-${seed}-0-D3-0`,
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

function createPreview(
  seed: number,
  target: "primary" | "secondary"
): PreviewPayload {
  return {
    summary: {
      seed,
      worldType: 13,
      worldPlacementIndex: target === "primary" ? 0 : 4,
      isPrimary: target === "primary",
      hasSecondaryPreview: true,
      start: target === "primary" ? { x: 10, y: 20 } : { x: 110, y: 220 },
      worldSize: target === "primary" ? { w: 256, h: 384 } : { w: 160, h: 192 },
      traits: target === "primary" ? [1, 2] : [8, 9],
      geysers:
        target === "primary"
          ? [
              { type: 0, x: 11, y: 21, id: "steam" },
              { type: 6, x: 15, y: 25, id: "salt_water" },
            ]
          : [{ type: 27, x: 101, y: 121, id: "chlorine_gas" }],
    },
    polygons: [],
  };
}

function createDetailsEvent(seed: number, target: "primary" | "secondary") {
  return {
    event: "preview_geyser_details" as const,
    jobId: `${target}-details-${seed}`,
    worldType: 13,
    seed,
    mixing: 625,
    geyserDetails: [
      {
        index: 0,
        summary:
          target === "primary"
            ? { type: 0, x: 11, y: 21, id: "steam" }
            : { type: 27, x: 101, y: 121, id: "chlorine_gas" },
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
          temperatureCelsius: target === "primary" ? 110 : 95,
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
    activeTarget: "primary",
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

test("loadByMatch caches primary and secondary previews independently and forwards target to both requests", async () => {
  const previewRequests: Array<Record<string, unknown>> = [];
  const detailRequests: Array<Record<string, unknown>> = [];

  mockIPC(async (cmd, payload) => {
    const request = payload.request as {
      seed: number;
      target?: "primary" | "secondary";
    };
    if (cmd === "load_preview") {
      previewRequests.push(payload.request as Record<string, unknown>);
      return {
        preview: createPreview(request.seed, request.target ?? "primary"),
      };
    }
    if (cmd === "load_preview_geyser_details") {
      detailRequests.push(payload.request as Record<string, unknown>);
      return createDetailsEvent(request.seed, request.target ?? "primary");
    }
    throw new Error(`unexpected command: ${cmd}`);
  });

  const module = await loadPreviewStoreModule();
  const match = createMatch();

  await module.usePreviewStore.getState().loadByMatch(match);
  await flushMicrotasks();
  await module.usePreviewStore.getState().loadByMatch(match, "secondary");
  await flushMicrotasks();

  const state = module.usePreviewStore.getState();
  assert.equal(state.activeTarget, "secondary");
  assert.equal(state.activeKey, "13:123456:625:secondary");
  assert.equal(state.activePreview?.summary.isPrimary, false);
  assert.equal(state.cache["13:123456:625:primary"]?.preview.summary.isPrimary, true);
  assert.equal(state.cache["13:123456:625:secondary"]?.preview.summary.isPrimary, false);
  assert.deepEqual(
    previewRequests.map((item) => item.target),
    ["primary", "secondary"],
    "preview request target should track selected world"
  );
  assert.deepEqual(
    detailRequests.map((item) => item.target),
    ["primary", "secondary"],
    "geyser detail request target should track selected world"
  );
});

test("loading a new seed after secondary becomes active resets the active target back to primary", async () => {
  mockIPC(async (cmd, payload) => {
    const request = payload.request as {
      seed: number;
      target?: "primary" | "secondary";
    };
    if (cmd === "load_preview") {
      return {
        preview: createPreview(request.seed, request.target ?? "primary"),
      };
    }
    if (cmd === "load_preview_geyser_details") {
      return createDetailsEvent(request.seed, request.target ?? "primary");
    }
    throw new Error(`unexpected command: ${cmd}`);
  });

  const module = await loadPreviewStoreModule();
  await module.usePreviewStore.getState().loadByMatch(createMatch(111111), "secondary");
  await flushMicrotasks();
  await module.usePreviewStore.getState().loadByMatch(createMatch(222222));
  await flushMicrotasks();

  const state = module.usePreviewStore.getState();
  assert.equal(state.activeTarget, "primary");
  assert.equal(state.activeKey, "13:222222:625:primary");
  assert.equal(state.activePreview?.summary.seed, 222222);
  assert.equal(state.activePreview?.summary.isPrimary, true);
});

test("secondary preview failure keeps the primary preview active and reports a targeted error", async () => {
  mockIPC(async (cmd, payload) => {
    const request = payload.request as {
      seed: number;
      target?: "primary" | "secondary";
    };
    if (cmd === "load_preview") {
      if (request.target === "secondary") {
        throw new Error("secondary sidecar failed");
      }
      return {
        preview: createPreview(request.seed, "primary"),
      };
    }
    if (cmd === "load_preview_geyser_details") {
      return createDetailsEvent(request.seed, request.target ?? "primary");
    }
    throw new Error(`unexpected command: ${cmd}`);
  });

  const module = await loadPreviewStoreModule();
  const match = createMatch(444444);

  await module.usePreviewStore.getState().loadByMatch(match);
  await flushMicrotasks();
  await module.usePreviewStore.getState().loadByMatch(match, "secondary");
  await flushMicrotasks();

  const state = module.usePreviewStore.getState();
  assert.equal(state.activeTarget, "primary");
  assert.equal(state.activeKey, "13:444444:625:primary");
  assert.equal(state.activePreview?.summary.isPrimary, true);
  assert.equal(state.lastError, "副星预览加载失败: secondary sidecar failed");
});

test("loading an uncached secondary preview switches to secondary state immediately but keeps the current primary preview as fallback", async () => {
  const secondaryPreview = createDeferredValue<{ preview: PreviewPayload }>();

  mockIPC(async (cmd, payload) => {
    const request = payload.request as {
      seed: number;
      target?: "primary" | "secondary";
    };
    if (cmd === "load_preview") {
      if (request.target === "secondary") {
        return await secondaryPreview.promise;
      }
      return {
        preview: createPreview(request.seed, "primary"),
      };
    }
    if (cmd === "load_preview_geyser_details") {
      return createDetailsEvent(request.seed, request.target ?? "primary");
    }
    throw new Error(`unexpected command: ${cmd}`);
  });

  const module = await loadPreviewStoreModule();
  const match = createMatch(555555);

  await module.usePreviewStore.getState().loadByMatch(match);
  await flushMicrotasks();

  const loadingSecondaryPromise = module.usePreviewStore.getState().loadByMatch(match, "secondary");
  await Promise.resolve();

  const loadingState = module.usePreviewStore.getState();
  assert.equal(loadingState.activeTarget, "secondary");
  assert.equal(loadingState.activeKey, "13:555555:625:secondary");
  assert.equal(loadingState.isLoading, true);
  assert.equal(loadingState.activePreview?.summary.seed, 555555);
  assert.equal(loadingState.activePreview?.summary.isPrimary, true);
  assert.equal(loadingState.activeGeyserDetailsStatus, "idle");

  secondaryPreview.resolve({
    preview: createPreview(match.seed, "secondary"),
  });
  await loadingSecondaryPromise;
  await flushMicrotasks();

  const readyState = module.usePreviewStore.getState();
  assert.equal(readyState.activeTarget, "secondary");
  assert.equal(readyState.activePreview?.summary.isPrimary, false);
});

test("a late secondary preview response only warms the secondary cache after the user switches back to primary", async () => {
  const secondaryPreview = createDeferredValue<{ preview: PreviewPayload }>();

  mockIPC(async (cmd, payload) => {
    const request = payload.request as {
      seed: number;
      target?: "primary" | "secondary";
    };
    if (cmd === "load_preview") {
      if (request.target === "secondary") {
        return await secondaryPreview.promise;
      }
      return {
        preview: createPreview(request.seed, "primary"),
      };
    }
    if (cmd === "load_preview_geyser_details") {
      return createDetailsEvent(request.seed, request.target ?? "primary");
    }
    throw new Error(`unexpected command: ${cmd}`);
  });

  const module = await loadPreviewStoreModule();
  const match = createMatch(666666);

  await module.usePreviewStore.getState().loadByMatch(match);
  await flushMicrotasks();

  const loadingSecondaryPromise = module.usePreviewStore.getState().loadByMatch(match, "secondary");
  await Promise.resolve();
  module.usePreviewStore.getState().setActiveTarget("primary");

  secondaryPreview.resolve({
    preview: createPreview(match.seed, "secondary"),
  });
  await loadingSecondaryPromise;
  await flushMicrotasks();

  const state = module.usePreviewStore.getState();
  assert.equal(state.activeTarget, "primary");
  assert.equal(state.activeKey, "13:666666:625:primary");
  assert.equal(state.activePreview?.summary.isPrimary, true);
  assert.equal(state.cache["13:666666:625:secondary"]?.preview.summary.isPrimary, false);
});
