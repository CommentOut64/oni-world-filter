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
  listGeysers,
  listWorlds,
  startSearch,
  subscribeSidecar,
} from "../lib/tauri.ts";
import { publishHostDebugSnapshot } from "../lib/hostDebugWindow.ts";
import {
  beginSidecarBinding,
  completeSidecarBinding,
  disposeSidecarBinding,
  failSidecarBinding,
} from "./searchStoreState.ts";
import { appendUniqueSearchResult } from "./searchResultsState.ts";
import { createSidecarListenerRegistry } from "./sidecarListenerRegistry.ts";
import {
  persistSearchSessionSnapshot,
  restoreSearchSessionSnapshot,
} from "./searchStorePersistence.ts";
import { buildSearchMatchSummaryFromPreview, computeNearestDistanceFromSummary } from "../lib/searchMatchSummary.ts";

const DEFAULT_CONSTRAINTS: SearchConstraints = {
  required: [],
  forbidden: [],
  distance: [],
  count: [],
};

const DEFAULT_CPU_CONFIG: SearchCpuConfig = {
  mode: "balanced",
  allowSmt: true,
  allowLowPerf: false,
  placement: "strict",
};

export interface SearchDraft {
  worldType: number;
  seedStart: number;
  seedEnd: number;
  mixing: number;
  cpu: SearchCpuConfig;
  constraints: SearchConstraints;
}

export const DEFAULT_SEARCH_DRAFT: SearchDraft = {
  worldType: 13,
  seedStart: 0,
  seedEnd: 10000,
  mixing: 0,
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
  bindingSidecar: boolean;
  isSearching: boolean;
  isCancelling: boolean;
  activeJobId: string | null;
  activeWorldType: number;
  activeMixing: number;
  results: SearchMatchSummary[];
  selectedSeed: number | null;
  draft: SearchDraft;
  lastSubmittedRequest: SearchRequest | null;
  lastHostDebugMessages: string[];
  stats: SearchStats;
  lastError: string | null;
  bootstrap: () => Promise<void>;
  bindSidecarEvents: () => Promise<void>;
  startSearchJob: (draft: SearchDraft) => Promise<boolean>;
  cancelSearchJob: () => Promise<void>;
  openDirectCoordResult: (match: SearchMatchSummary) => void;
  clearResults: () => void;
  selectSeed: (seed: number | null) => void;
  setDraft: (draft: SearchDraft) => void;
  clearError: () => void;
  copyDraftAsJson: () => string;
  ingestSidecarEvent: (event: SidecarEvent) => void;
}

const HOST_DEBUG_PREFIX = "[host-debug]";
const sidecarListenerRegistry = createSidecarListenerRegistry();
const restoredSession = restoreSearchSessionSnapshot(getSessionStorage());

function getSessionStorage(): Storage | null {
  if (typeof window === "undefined") {
    return null;
  }
  try {
    return window.sessionStorage;
  } catch {
    return null;
  }
}

function createDefaultStats(totalMatches = 0): SearchStats {
  return {
    startedAtMs: null,
    processedSeeds: 0,
    totalSeeds: 0,
    totalMatches,
    activeWorkers: 0,
    currentSeedsPerSecond: 0,
    peakSeedsPerSecond: 0,
  };
}

function isRecoverableSidecarDiagnostic(message: string): boolean {
  const normalized = message.trim();
  return (
    normalized.includes("compute child node pd failed, fallback to compute node.") ||
    normalized.includes("compute node pd failed, fallback to compute node.") ||
    normalized.includes("compute node pd failed after convert unknown cells") ||
    (normalized.includes("Intersect:") && normalized.includes("intersection result is empty.")) ||
    (normalized.includes("Intersect:") && normalized.includes("subj:") && normalized.includes("clip:"))
  );
}

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

function appendMatch(state: SearchState, event: SearchMatchEvent): SearchState {
  const match: SearchMatchSummary = {
    ...buildSearchMatchSummaryFromPreview({
      coord: toCoordCode(state.worlds, state.activeWorldType, event.seed, state.activeMixing),
      worldType: state.activeWorldType,
      seed: event.seed,
      mixing: state.activeMixing,
      summary: {
        seed: event.seed,
        worldType: state.activeWorldType,
        start: event.summary.start,
        worldSize: event.summary.worldSize,
        traits: event.summary.traits,
        geysers: event.summary.geysers,
      },
    }),
    nearestDistance: computeNearestDistanceFromSummary(event.summary.start, event.summary.geysers),
  };

  return {
    ...state,
    results: appendUniqueSearchResult(state.results, match),
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

function appendHostDebugMessage(message: string): void {
  useSearchStore.setState((current) => {
    const nextMessages = [...current.lastHostDebugMessages, message];
    publishHostDebugSnapshot({
      request: current.lastSubmittedRequest,
      messages: nextMessages,
    });
    return {
      lastHostDebugMessages: nextMessages,
    };
  });
}

function waitForSidecarBindingToSettle(): Promise<void> {
  return new Promise((resolve) => {
    const unsubscribe = useSearchStore.subscribe((state) => {
      if (state.listening || !state.bindingSidecar) {
        unsubscribe();
        resolve();
      }
    });
  });
}

async function ensureSidecarListening(): Promise<void> {
  const current = useSearchStore.getState();
  if (current.listening) {
    return;
  }

  if (!current.bindingSidecar) {
    await current.bindSidecarEvents();
  }

  let settled = useSearchStore.getState();
  if (!settled.listening && settled.bindingSidecar) {
    await waitForSidecarBindingToSettle();
    settled = useSearchStore.getState();
  }

  if (!settled.listening) {
    throw new Error(settled.lastError ?? "sidecar 事件监听绑定失败");
  }
}

export const useSearchStore = create<SearchState>((set, get) => ({
  catalog: null,
  worlds: [],
  geysers: [],
  bootstrapped: false,
  listening: false,
  bindingSidecar: false,
  isSearching: false,
  isCancelling: false,
  activeJobId: null,
  activeWorldType: restoredSession?.activeWorldType ?? DEFAULT_SEARCH_DRAFT.worldType,
  activeMixing: restoredSession?.activeMixing ?? DEFAULT_SEARCH_DRAFT.mixing,
  results: restoredSession?.results ?? [],
  selectedSeed: restoredSession?.selectedSeed ?? null,
  draft: restoredSession?.draft ?? DEFAULT_SEARCH_DRAFT,
  lastSubmittedRequest: restoredSession?.lastSubmittedRequest ?? null,
  lastHostDebugMessages: [],
  stats: createDefaultStats(restoredSession?.totalMatches ?? 0),
  lastError: null,
  bootstrap: async () => {
    if (get().bootstrapped) {
      return;
    }
    const [catalogResult, worldsResult, geysersResult] = await Promise.allSettled([
      getSearchCatalog(),
      listWorlds(),
      listGeysers(),
    ]);

    const catalog = catalogResult.status === "fulfilled" ? catalogResult.value : null;
    const worlds =
      worldsResult.status === "fulfilled" && worldsResult.value.length > 0
        ? worldsResult.value
        : catalog?.worlds ?? [];
    const geysers =
      geysersResult.status === "fulfilled" && geysersResult.value.length > 0
        ? geysersResult.value
        : catalog?.geysers ?? [];

    if (!catalog && worlds.length === 0 && geysers.length === 0) {
      const firstError =
        catalogResult.status === "rejected"
          ? catalogResult.reason
          : worldsResult.status === "rejected"
            ? worldsResult.reason
            : geysersResult.status === "rejected"
              ? geysersResult.reason
              : "搜索目录加载失败";
      set({ lastError: formatTauriError(firstError) });
      return;
    }

    set({
      catalog,
      worlds,
      geysers,
      bootstrapped: true,
      lastError:
        catalogResult.status === "rejected" ? `catalog 加载失败，已退回静态目录: ${formatTauriError(catalogResult.reason)}` : null,
    });
  },
  bindSidecarEvents: async () => {
    const binding = beginSidecarBinding({
      listening: get().listening,
      bindingSidecar: get().bindingSidecar,
      lastError: get().lastError,
    });
    if (!binding.shouldSubscribe) {
      return;
    }
    set(binding.nextState);
    const bindingId = sidecarListenerRegistry.beginBinding();
    try {
      const releaseListener = await subscribeSidecar(
        (event) => {
          useSearchStore.getState().ingestSidecarEvent(event);
        },
        (stderrEvent) => {
          if (stderrEvent.message.startsWith(HOST_DEBUG_PREFIX)) {
            appendHostDebugMessage(stderrEvent.message);
            return;
          }
          if (isRecoverableSidecarDiagnostic(stderrEvent.message)) {
            return;
          }
          useSearchStore.setState({
            lastError: `[${stderrEvent.jobId}] ${stderrEvent.message}`,
          });
        }
      );
      // 严格模式下首次挂载可能先卸载再等待订阅返回，过期监听必须立刻释放。
      if (!sidecarListenerRegistry.resolveBinding(bindingId, releaseListener)) {
        return;
      }
      set((current) => completeSidecarBinding(current));
    } catch (error) {
      if (!sidecarListenerRegistry.isActiveBinding(bindingId)) {
        return;
      }
      set((current) => failSidecarBinding(current, formatTauriError(error)));
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
      constraints: {
        required: [...draft.constraints.required],
        forbidden: [...draft.constraints.forbidden],
        distance: [...draft.constraints.distance],
        count: [...draft.constraints.count],
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
      lastSubmittedRequest: request,
      lastHostDebugMessages: [],
      stats: {
        ...createDefaultStats(),
        startedAtMs: Date.now(),
      },
      lastError: null,
    });
    publishHostDebugSnapshot({
      request,
      messages: [],
    });
    try {
      await ensureSidecarListening();
      await startSearch(request);
      return true;
    } catch (error) {
      set({
        isSearching: false,
        isCancelling: false,
        lastError: formatTauriError(error),
      });
      return false;
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
      set({
        isCancelling: false,
        lastError: formatTauriError(error),
      });
    }
  },
  openDirectCoordResult: (match) => {
    set((state) => ({
      isSearching: false,
      isCancelling: false,
      activeJobId: null,
      activeWorldType: match.worldType,
      activeMixing: match.mixing,
      results: [match],
      selectedSeed: match.seed,
      draft: state.draft,
      lastSubmittedRequest: null,
      lastHostDebugMessages: [],
      stats: createDefaultStats(1),
      lastError: null,
    }));
  },
  clearResults: () => {
    set({
      results: [],
      selectedSeed: null,
      stats: createDefaultStats(),
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
        cpu: draft.cpu,
        required: draft.constraints.required,
        forbidden: draft.constraints.forbidden,
        distance: draft.constraints.distance,
        count: draft.constraints.count,
      },
      null,
      2
    );
  },
  ingestSidecarEvent: (event) => {
    const state = get();
    if (!state.activeJobId || event.jobId !== state.activeJobId) {
      appendHostDebugMessage(
        `${HOST_DEBUG_PREFIX} frontend dropped sidecar event: event=${event.event}, eventJobId=${event.jobId}, activeJobId=${state.activeJobId ?? "null"}`
      );
      return;
    }

    if (state.isCancelling && (event.event === "progress" || event.event === "match")) {
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
        isCancelling: false,
        activeJobId: null,
        stats: {
          ...state.stats,
          currentSeedsPerSecond: 0,
        },
        lastError: event.message,
      });
      return;
    }

    if (event.event === "completed") {
      set({
        isSearching: false,
        isCancelling: false,
        activeJobId: null,
        stats: {
          ...state.stats,
          processedSeeds: event.processedSeeds,
          totalSeeds: event.totalSeeds,
          totalMatches: event.totalMatches,
          activeWorkers: event.finalActiveWorkers,
          currentSeedsPerSecond: 0,
        },
      });
      return;
    }

    if (event.event === "cancelled") {
      set({
        isSearching: false,
        isCancelling: false,
        activeJobId: null,
        stats: {
          ...state.stats,
          processedSeeds: Math.max(state.stats.processedSeeds, event.processedSeeds),
          totalSeeds: Math.max(state.stats.totalSeeds, event.totalSeeds),
          totalMatches: Math.max(state.stats.totalMatches, event.totalMatches),
          activeWorkers: event.finalActiveWorkers,
          currentSeedsPerSecond: 0,
        },
      });
    }
  },
}));

useSearchStore.subscribe((state) => {
  persistSearchSessionSnapshot(getSessionStorage(), {
    draft: state.draft,
    results: state.results,
    selectedSeed: state.selectedSeed,
    lastSubmittedRequest: state.lastSubmittedRequest,
  });
});

export function disposeSidecarListener(): void {
  sidecarListenerRegistry.dispose();
  useSearchStore.setState((current) => disposeSidecarBinding(current));
}
