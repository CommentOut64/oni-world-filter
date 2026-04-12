import { create } from "zustand";

import type {
  SearchCpuConfig,
  GeyserOption,
  SearchCatalog,
  SearchConstraints,
  SearchMatchEvent,
  SearchMatchSummary,
  SearchProgressEvent,
  SearchRequest,
  SidecarEvent,
  WorldOption,
} from "../lib/contracts";
import {
  cancelSearch,
  formatTauriError,
  getSearchCatalog,
  startSearch,
  subscribeSidecar,
} from "../lib/tauri";

const DEFAULT_CONSTRAINTS: SearchConstraints = {
  required: [],
  forbidden: [],
  distance: [],
};

const DEFAULT_CPU_CONFIG: SearchCpuConfig = {
  mode: "balanced",
  workers: 0,
  allowSmt: true,
  allowLowPerf: false,
  placement: "preferred",
  enableWarmup: true,
  enableAdaptiveDown: true,
  chunkSize: 64,
  progressInterval: 1000,
  sampleWindowMs: 2000,
  adaptiveMinWorkers: 1,
  adaptiveDropThreshold: 0.12,
  adaptiveDropWindows: 3,
  adaptiveCooldownMs: 8000,
};

export interface SearchDraft {
  worldType: number;
  seedStart: number;
  seedEnd: number;
  mixing: number;
  threads: number;
  cpu: SearchCpuConfig;
  constraints: SearchConstraints;
}

export const DEFAULT_SEARCH_DRAFT: SearchDraft = {
  worldType: 13,
  seedStart: 100000,
  seedEnd: 120000,
  mixing: 625,
  threads: 0,
  cpu: DEFAULT_CPU_CONFIG,
  constraints: DEFAULT_CONSTRAINTS,
};

interface SearchStats {
  startedAtMs: number | null;
  processedSeeds: number;
  totalSeeds: number;
  totalMatches: number;
  activeWorkers: number;
  currentSeedsPerSecond: number;
  peakSeedsPerSecond: number;
}

interface SearchState {
  catalog: SearchCatalog | null;
  worlds: WorldOption[];
  geysers: GeyserOption[];
  bootstrapped: boolean;
  listening: boolean;
  isSearching: boolean;
  isCancelling: boolean;
  activeJobId: string | null;
  activeWorldType: number;
  activeMixing: number;
  results: SearchMatchSummary[];
  selectedSeed: number | null;
  draft: SearchDraft;
  stats: SearchStats;
  lastError: string | null;
  bootstrap: () => Promise<void>;
  bindSidecarEvents: () => Promise<void>;
  startSearchJob: (draft: SearchDraft) => Promise<void>;
  cancelSearchJob: () => Promise<void>;
  clearResults: () => void;
  selectSeed: (seed: number | null) => void;
  setDraft: (draft: SearchDraft) => void;
  clearError: () => void;
  copyDraftAsJson: () => string;
  ingestSidecarEvent: (event: SidecarEvent) => void;
}

let releaseListener: (() => void) | null = null;

function makeJobId(prefix: string): string {
  const suffix = Math.random().toString(36).slice(2, 8);
  return `${prefix}-${Date.now()}-${suffix}`;
}

function toBase36(value: number): string {
  const normalized = Math.max(0, Math.trunc(value));
  return normalized.toString(36).toUpperCase();
}

function toCoordCode(
  worlds: WorldOption[],
  worldType: number,
  seed: number,
  mixing: number
): string {
  const prefix = worlds.find((item) => item.id === worldType)?.code ?? `WORLD-${worldType}-`;
  return `${prefix}${seed}-0-D3-${toBase36(mixing)}`;
}

function nearestDistance(event: SearchMatchEvent): number | null {
  const start = event.summary.start;
  if (!event.summary.geysers.length) {
    return null;
  }
  let minDistance = Number.POSITIVE_INFINITY;
  for (const geyser of event.summary.geysers) {
    const dx = geyser.x - start.x;
    const dy = geyser.y - start.y;
    const value = Math.sqrt(dx * dx + dy * dy);
    if (value < minDistance) {
      minDistance = value;
    }
  }
  return Number.isFinite(minDistance) ? Number(minDistance.toFixed(1)) : null;
}

function appendMatch(state: SearchState, event: SearchMatchEvent): SearchState {
  const match: SearchMatchSummary = {
    seed: event.seed,
    worldType: state.activeWorldType,
    mixing: state.activeMixing,
    coord: toCoordCode(state.worlds, state.activeWorldType, event.seed, state.activeMixing),
    traits: event.summary.traits,
    start: event.summary.start,
    worldSize: event.summary.worldSize,
    geysers: event.summary.geysers,
    nearestDistance: nearestDistance(event),
  };

  return {
    ...state,
    results: [...state.results, match],
    stats: {
      ...state.stats,
      processedSeeds: event.processedSeeds,
      totalSeeds: event.totalSeeds,
      totalMatches: event.totalMatches,
    },
  };
}

function updateProgress(state: SearchState, event: SearchProgressEvent): SearchState {
  return {
    ...state,
    stats: {
      ...state.stats,
      processedSeeds: event.processedSeeds,
      totalSeeds: event.totalSeeds,
      totalMatches: event.totalMatches,
      activeWorkers: event.activeWorkers,
      currentSeedsPerSecond: event.windowSeedsPerSecond ?? state.stats.currentSeedsPerSecond,
      peakSeedsPerSecond: Math.max(state.stats.peakSeedsPerSecond, event.peakSeedsPerSecond),
    },
  };
}

export const useSearchStore = create<SearchState>((set, get) => ({
  catalog: null,
  worlds: [],
  geysers: [],
  bootstrapped: false,
  listening: false,
  isSearching: false,
  isCancelling: false,
  activeJobId: null,
  activeWorldType: DEFAULT_SEARCH_DRAFT.worldType,
  activeMixing: DEFAULT_SEARCH_DRAFT.mixing,
  results: [],
  selectedSeed: null,
  draft: DEFAULT_SEARCH_DRAFT,
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
  bootstrap: async () => {
    if (get().bootstrapped) {
      return;
    }
    try {
      const catalog = await getSearchCatalog();
      set({
        catalog,
        worlds: catalog.worlds,
        geysers: catalog.geysers,
        bootstrapped: true,
      });
    } catch (error) {
      set({ lastError: formatTauriError(error) });
    }
  },
  bindSidecarEvents: async () => {
    if (get().listening) {
      return;
    }
    try {
      releaseListener = await subscribeSidecar(
        (event) => {
          useSearchStore.getState().ingestSidecarEvent(event);
        },
        (stderrEvent) => {
          useSearchStore.setState({
            lastError: `[${stderrEvent.jobId}] ${stderrEvent.message}`,
          });
        }
      );
      set({ listening: true });
    } catch (error) {
      set({ lastError: formatTauriError(error) });
    }
  },
  startSearchJob: async (draft) => {
    const jobId = makeJobId("search");
    const request: SearchRequest = {
      jobId,
      worldType: draft.worldType,
      seedStart: draft.seedStart,
      seedEnd: draft.seedEnd,
      mixing: draft.mixing,
      threads: draft.threads,
      constraints: {
        required: [...draft.constraints.required],
        forbidden: [...draft.constraints.forbidden],
        distance: [...draft.constraints.distance],
      },
      cpu: { ...draft.cpu },
    };
    set({
      isSearching: true,
      isCancelling: false,
      activeJobId: jobId,
      activeWorldType: draft.worldType,
      activeMixing: draft.mixing,
      results: [],
      selectedSeed: null,
      draft,
      stats: {
        startedAtMs: Date.now(),
        processedSeeds: 0,
        totalSeeds: 0,
        totalMatches: 0,
        activeWorkers: 0,
        currentSeedsPerSecond: 0,
        peakSeedsPerSecond: 0,
      },
      lastError: null,
    });
    try {
      await startSearch(request);
    } catch (error) {
      set({
        isSearching: false,
        isCancelling: false,
        lastError: formatTauriError(error),
      });
    }
  },
  cancelSearchJob: async () => {
    const jobId = get().activeJobId;
    if (!jobId) {
      return;
    }
    set({ isCancelling: true });
    try {
      await cancelSearch(jobId);
    } catch (error) {
      set({ lastError: formatTauriError(error) });
    } finally {
      set({ isCancelling: false });
    }
  },
  clearResults: () => {
    set({
      results: [],
      selectedSeed: null,
      stats: {
        startedAtMs: null,
        processedSeeds: 0,
        totalSeeds: 0,
        totalMatches: 0,
        activeWorkers: 0,
        currentSeedsPerSecond: 0,
        peakSeedsPerSecond: 0,
      },
    });
  },
  selectSeed: (seed) => {
    set({ selectedSeed: seed });
  },
  setDraft: (draft) => {
    set({ draft });
  },
  clearError: () => {
    set({ lastError: null });
  },
  copyDraftAsJson: () => {
    const draft = get().draft;
    return JSON.stringify(
      {
        worldType: draft.worldType,
        seedStart: draft.seedStart,
        seedEnd: draft.seedEnd,
        mixing: draft.mixing,
        threads: draft.threads,
        cpu: draft.cpu,
        required: draft.constraints.required,
        forbidden: draft.constraints.forbidden,
        distance: draft.constraints.distance,
      },
      null,
      2
    );
  },
  ingestSidecarEvent: (event) => {
    const state = get();
    if (!state.activeJobId || event.jobId !== state.activeJobId) {
      return;
    }

    if (event.event === "started") {
      set({
        stats: {
          ...state.stats,
          startedAtMs: Date.now(),
          totalSeeds: event.totalSeeds,
          activeWorkers: event.workerCount,
        },
      });
      return;
    }

    if (event.event === "progress") {
      set((current) => updateProgress(current, event));
      return;
    }

    if (event.event === "match") {
      set((current) => appendMatch(current, event));
      return;
    }

    if (event.event === "failed") {
      set({
        isSearching: false,
        activeJobId: null,
        lastError: event.message,
      });
      return;
    }

    if (event.event === "completed") {
      set({
        isSearching: false,
        activeJobId: null,
        stats: {
          ...state.stats,
          processedSeeds: event.processedSeeds,
          totalSeeds: event.totalSeeds,
          totalMatches: event.totalMatches,
          activeWorkers: event.finalActiveWorkers,
          currentSeedsPerSecond: event.throughput.averageSeedsPerSecond,
        },
      });
      return;
    }

    if (event.event === "cancelled") {
      set({
        isSearching: false,
        activeJobId: null,
        stats: {
          ...state.stats,
          processedSeeds: event.processedSeeds,
          totalSeeds: event.totalSeeds,
          totalMatches: event.totalMatches,
          activeWorkers: event.finalActiveWorkers,
        },
      });
    }
  },
}));

export function disposeSidecarListener(): void {
  if (releaseListener) {
    releaseListener();
    releaseListener = null;
    useSearchStore.setState({ listening: false });
  }
}
