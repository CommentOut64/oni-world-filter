import type {
  GeyserDetail,
  GeyserDetailsStatus,
  PreviewPayload,
  SearchMatchSummary,
} from "../lib/contracts.ts";

export interface PreviewCacheEntry {
  preview: PreviewPayload;
  geyserDetailsStatus: GeyserDetailsStatus;
  geyserDetails: GeyserDetail[];
  geyserDetailsError: string | null;
}

export interface PreviewStoreSnapshot {
  activeKey: string | null;
  activePreview: PreviewPayload | null;
  activeGeyserDetailsStatus: GeyserDetailsStatus;
  activeGeyserDetails: GeyserDetail[];
  activeGeyserDetailsError: string | null;
  cache: Record<string, PreviewCacheEntry>;
  inflightPreviewKeys: Record<string, number>;
  inflightDetailKeys: Record<string, number>;
  requestSerial: number;
  isLoading: boolean;
  lastError: string | null;
}

function createCacheEntry(
  preview: PreviewPayload,
  existing?: PreviewCacheEntry
): PreviewCacheEntry {
  return {
    preview,
    geyserDetailsStatus: existing?.geyserDetailsStatus ?? "idle",
    geyserDetails: existing?.geyserDetails ?? [],
    geyserDetailsError: existing?.geyserDetailsError ?? null,
  };
}

function activeDetailsFromCache(
  cached: PreviewCacheEntry | undefined,
  forceLoading: boolean
): {
  status: GeyserDetailsStatus;
  details: GeyserDetail[];
  error: string | null;
} {
  if (!cached) {
    return {
      status: forceLoading ? "loading" : "idle",
      details: [],
      error: null,
    };
  }

  if (forceLoading && cached.geyserDetailsStatus !== "ready") {
    return {
      status: "loading",
      details: [],
      error: null,
    };
  }

  if (cached.geyserDetailsStatus === "loading") {
    return {
      status: "loading",
      details: [],
      error: null,
    };
  }

  return {
    status: cached.geyserDetailsStatus,
    details: cached.geyserDetails,
    error: cached.geyserDetailsError,
  };
}

function removeInflightKey(
  inflight: Record<string, number>,
  key: string,
  serial: number
): Record<string, number> {
  if (inflight[key] !== serial) {
    return inflight;
  }
  const next = { ...inflight };
  delete next[key];
  return next;
}

function upsertCacheEntry(
  state: PreviewStoreSnapshot,
  key: string,
  preview: PreviewPayload
): Record<string, PreviewCacheEntry> {
  const nextCache = {
    ...state.cache,
    [key]: createCacheEntry(preview, state.cache[key]),
  };
  return nextCache;
}

export function previewKey(match: SearchMatchSummary): string {
  return `${match.worldType}:${match.seed}:${match.mixing}`;
}

export function beginPreviewLoad(
  state: PreviewStoreSnapshot,
  key: string,
  requestSerial: number
): PreviewStoreSnapshot {
  const cached = state.cache[key];
  const forceLoading = state.inflightDetailKeys[key] !== undefined && cached?.geyserDetailsStatus !== "ready";
  const activeDetails = activeDetailsFromCache(cached, forceLoading);

  if (cached) {
    return {
      ...state,
      activeKey: key,
      activePreview: cached.preview,
      activeGeyserDetailsStatus: activeDetails.status,
      activeGeyserDetails: activeDetails.details,
      activeGeyserDetailsError: activeDetails.error,
      isLoading: false,
      lastError: null,
      requestSerial,
    };
  }

  return {
    ...state,
    activeKey: key,
    activePreview: null,
    activeGeyserDetailsStatus: "idle",
    activeGeyserDetails: [],
    activeGeyserDetailsError: null,
    isLoading: true,
    lastError: null,
    requestSerial,
  };
}

export function beginGeyserDetailsLoad(
  state: PreviewStoreSnapshot,
  key: string,
  requestSerial: number
): PreviewStoreSnapshot {
  const nextCache = state.cache[key]
    ? {
        ...state.cache,
        [key]: {
          ...state.cache[key],
          geyserDetailsStatus: "loading" as const,
          geyserDetailsError: null,
        } as PreviewCacheEntry,
      }
    : state.cache;

  if (state.activeKey !== key) {
    return {
      ...state,
      cache: nextCache,
      inflightDetailKeys: {
        ...state.inflightDetailKeys,
        [key]: requestSerial,
      },
    };
  }

  return {
    ...state,
    cache: nextCache,
    activeGeyserDetailsStatus: "loading",
    activeGeyserDetails: [],
    activeGeyserDetailsError: null,
    inflightDetailKeys: {
      ...state.inflightDetailKeys,
      [key]: requestSerial,
    },
    requestSerial,
  };
}

export function completePreviewLoad(
  state: PreviewStoreSnapshot,
  key: string,
  preview: PreviewPayload,
  requestSerial: number
): PreviewStoreSnapshot {
  const nextCache = upsertCacheEntry(state, key, preview);
  const nextState = {
    ...state,
    cache: nextCache,
    inflightPreviewKeys: removeInflightKey(state.inflightPreviewKeys, key, requestSerial),
  };

  if (state.activeKey !== key || state.requestSerial !== requestSerial) {
    return nextState;
  }

  return {
    ...nextState,
    activePreview: preview,
    isLoading: false,
    lastError: null,
  };
}

export function completeGeyserDetailsLoad(
  state: PreviewStoreSnapshot,
  key: string,
  geyserDetails: GeyserDetail[],
  requestSerial: number
): PreviewStoreSnapshot {
  const cached = state.cache[key];
  const nextCache = cached
    ? {
        ...state.cache,
        [key]: {
          ...cached,
          geyserDetailsStatus: "ready" as const,
          geyserDetails,
          geyserDetailsError: null,
        } as PreviewCacheEntry,
      }
    : state.cache;
  const nextState = {
    ...state,
    cache: nextCache,
    inflightDetailKeys: removeInflightKey(state.inflightDetailKeys, key, requestSerial),
  };

  if (state.activeKey !== key || state.requestSerial !== requestSerial) {
    return nextState;
  }

  return {
    ...nextState,
    activeGeyserDetailsStatus: "ready",
    activeGeyserDetails: geyserDetails,
    activeGeyserDetailsError: null,
  };
}

export function failPreviewLoad(
  state: PreviewStoreSnapshot,
  key: string,
  error: string,
  requestSerial: number
): PreviewStoreSnapshot {
  const nextState = {
    ...state,
    inflightPreviewKeys: removeInflightKey(state.inflightPreviewKeys, key, requestSerial),
  };
  if (state.activeKey !== key || state.requestSerial !== requestSerial) {
    return nextState;
  }
  return {
    ...nextState,
    isLoading: false,
    lastError: error,
  };
}

export function failGeyserDetailsLoad(
  state: PreviewStoreSnapshot,
  key: string,
  error: string,
  requestSerial: number
): PreviewStoreSnapshot {
  const cached = state.cache[key];
  const nextCache = cached
    ? {
        ...state.cache,
        [key]: {
          ...cached,
          geyserDetailsStatus: "failed" as const,
          geyserDetailsError: error,
        } as PreviewCacheEntry,
      }
    : state.cache;
  const nextState = {
    ...state,
    cache: nextCache,
    inflightDetailKeys: removeInflightKey(state.inflightDetailKeys, key, requestSerial),
  };

  if (state.activeKey !== key || state.requestSerial !== requestSerial) {
    return nextState;
  }

  return {
    ...nextState,
    activeGeyserDetailsStatus: "failed",
    activeGeyserDetailsError: error,
  };
}

export function primeResolvedPreviewState(
  state: PreviewStoreSnapshot,
  match: SearchMatchSummary,
  preview: PreviewPayload
): PreviewStoreSnapshot {
  const key = previewKey(match);
  const cacheEntry = createCacheEntry(preview, state.cache[key]);
  const activeDetails = activeDetailsFromCache(cacheEntry, cacheEntry.geyserDetailsStatus === "loading");

  return {
    ...state,
    activeKey: key,
    activePreview: preview,
    activeGeyserDetailsStatus: activeDetails.status,
    activeGeyserDetails: activeDetails.details,
    activeGeyserDetailsError: activeDetails.error,
    cache: {
      ...state.cache,
      [key]: cacheEntry,
    },
    isLoading: false,
    lastError: null,
  };
}
