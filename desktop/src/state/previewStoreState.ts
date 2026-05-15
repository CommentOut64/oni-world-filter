import type {
  GeyserDetail,
  GeyserDetailsStatus,
  PreviewPayload,
  PreviewTarget,
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
  activeTarget: PreviewTarget;
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
  return previewKeyForTarget(match.worldType, match.seed, match.mixing, "primary");
}

export function previewKeyForTarget(
  worldType: number,
  seed: number,
  mixing: number,
  target: PreviewTarget
): string {
  return `${worldType}:${seed}:${mixing}:${target}`;
}

export function previewKeyForMatch(
  match: SearchMatchSummary,
  target: PreviewTarget
): string {
  return previewKeyForTarget(match.worldType, match.seed, match.mixing, target);
}

function activeTargetFromKey(key: string, fallback: PreviewTarget): PreviewTarget {
  const segments = key.split(":");
  const rawTarget = segments[3];
  if (rawTarget === "secondary") {
    return "secondary";
  }
  if (rawTarget === "primary") {
    return "primary";
  }
  return fallback;
}

export function previewBaseKey(key: string | null): string | null {
  if (!key) {
    return null;
  }
  const segments = key.split(":");
  if (segments.length < 3) {
    return null;
  }
  return segments.slice(0, 3).join(":");
}

export function beginPreviewLoad(
  state: PreviewStoreSnapshot,
  key: string,
  requestSerial: number
): PreviewStoreSnapshot {
  const cached = state.cache[key];
  const forceLoading = state.inflightDetailKeys[key] !== undefined && cached?.geyserDetailsStatus !== "ready";
  const activeDetails = activeDetailsFromCache(cached, forceLoading);
  const activeTarget = activeTargetFromKey(key, state.activeTarget);
  const shouldRetainActivePreview = previewBaseKey(state.activeKey) === previewBaseKey(key);

  if (cached) {
    return {
      ...state,
      activeKey: key,
      activeTarget,
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
    activeTarget,
    activePreview: shouldRetainActivePreview ? state.activePreview : null,
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
  const failedTarget = activeTargetFromKey(key, state.activeTarget);
  const baseKey = previewBaseKey(key);
  const primaryKey = baseKey ? `${baseKey}:primary` : null;
  const primaryCached = primaryKey ? state.cache[primaryKey] : undefined;
  if (failedTarget === "secondary" && state.cache[key] === undefined && primaryCached && primaryKey) {
    const forceLoading =
      state.inflightDetailKeys[primaryKey] !== undefined && primaryCached.geyserDetailsStatus !== "ready";
    const activeDetails = activeDetailsFromCache(primaryCached, forceLoading);
    return {
      ...nextState,
      activeKey: primaryKey,
      activeTarget: "primary",
      activePreview: primaryCached.preview,
      activeGeyserDetailsStatus: activeDetails.status,
      activeGeyserDetails: activeDetails.details,
      activeGeyserDetailsError: activeDetails.error,
      isLoading: false,
      lastError: error,
    };
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
  preview: PreviewPayload,
  target: PreviewTarget = "primary"
): PreviewStoreSnapshot {
  const key = previewKeyForMatch(match, target);
  const cacheEntry = createCacheEntry(preview, state.cache[key]);
  const activeDetails = activeDetailsFromCache(cacheEntry, cacheEntry.geyserDetailsStatus === "loading");

  return {
    ...state,
    activeKey: key,
    activeTarget: target,
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

export function setActiveTargetState(
  state: PreviewStoreSnapshot,
  target: PreviewTarget
): PreviewStoreSnapshot {
  const baseKey = previewBaseKey(state.activeKey);
  if (!baseKey) {
    return {
      ...state,
      activeTarget: target,
    };
  }

  const key = `${baseKey}:${target}`;
  const cached = state.cache[key];
  if (!cached) {
    return state;
  }

  const forceLoading = state.inflightDetailKeys[key] !== undefined && cached.geyserDetailsStatus !== "ready";
  const activeDetails = activeDetailsFromCache(cached, forceLoading);

  return {
    ...state,
    activeKey: key,
    activeTarget: target,
    activePreview: cached.preview,
    activeGeyserDetailsStatus: activeDetails.status,
    activeGeyserDetails: activeDetails.details,
    activeGeyserDetailsError: activeDetails.error,
    isLoading: false,
    lastError: null,
  };
}
