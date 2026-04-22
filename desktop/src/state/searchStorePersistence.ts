import type { SearchCpuConfig, SearchMatchSummary, SearchRequest } from "../lib/contracts";
import type { SearchDraft } from "./searchStore";

export const SEARCH_SESSION_STORAGE_KEY = "oni-search-session/v1";

const SEARCH_SESSION_VERSION = 1;

export interface SearchSessionStorage {
  getItem(key: string): string | null;
  setItem(key: string, value: string): void;
  removeItem(key: string): void;
}

export interface SearchSessionStateSnapshot {
  draft: SearchDraft;
  results: SearchMatchSummary[];
  selectedSeed: number | null;
  lastSubmittedRequest: SearchRequest | null;
}

interface SerializedSearchSessionSnapshot extends SearchSessionStateSnapshot {
  version: typeof SEARCH_SESSION_VERSION;
}

export interface RestoredSearchSessionState extends SearchSessionStateSnapshot {
  activeWorldType: number;
  activeMixing: number;
  totalMatches: number;
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null;
}

function cloneValue<T>(value: T): T {
  return JSON.parse(JSON.stringify(value)) as T;
}

function sanitizeSelectedSeed(results: SearchMatchSummary[], selectedSeed: number | null): number | null {
  if (selectedSeed === null) {
    return null;
  }
  return results.some((item) => item.seed === selectedSeed) ? selectedSeed : null;
}

function normalizeCpuMode(value: unknown): SearchCpuConfig["mode"] {
  if (value === "turbo" || value === "custom") {
    return "turbo";
  }
  return "balanced";
}

function normalizePlacement(value: unknown): SearchCpuConfig["placement"] {
  if (value === "none") {
    return value;
  }
  return "strict";
}

function normalizeSearchCpuConfig(cpu: unknown): SearchCpuConfig {
  const source = isRecord(cpu) ? cpu : {};
  const placementValue =
    typeof source.placement === "string"
      ? source.placement
      : typeof source.binding === "string"
        ? source.binding
        : undefined;
  return {
    mode: normalizeCpuMode(source.mode),
    allowSmt: typeof source.allowSmt === "boolean" ? source.allowSmt : true,
    allowLowPerf: typeof source.allowLowPerf === "boolean" ? source.allowLowPerf : false,
    placement: normalizePlacement(placementValue),
  };
}

function normalizeSearchDraft(draft: SearchDraft): SearchDraft {
  const { threads: _legacyThreads, ...rest } = draft as SearchDraft & { threads?: unknown };
  return {
    ...rest,
    cpu: normalizeSearchCpuConfig(rest.cpu),
  };
}

function normalizeSearchRequest(request: SearchRequest): SearchRequest {
  const { threads: _legacyThreads, ...rest } = request as SearchRequest & { threads?: unknown };
  if (!request.cpu) {
    return {
      ...rest,
    };
  }
  return {
    ...rest,
    cpu: normalizeSearchCpuConfig(rest.cpu),
  };
}

function toSerializableSnapshot(
  state: SearchSessionStateSnapshot
): SerializedSearchSessionSnapshot {
  return {
    version: SEARCH_SESSION_VERSION,
    draft: cloneValue(state.draft),
    results: cloneValue(state.results),
    selectedSeed: state.selectedSeed,
    lastSubmittedRequest: state.lastSubmittedRequest ? cloneValue(state.lastSubmittedRequest) : null,
  };
}

function parseSerializedSnapshot(raw: string): SerializedSearchSessionSnapshot | null {
  const parsed: unknown = JSON.parse(raw);
  if (!isRecord(parsed) || parsed.version !== SEARCH_SESSION_VERSION) {
    return null;
  }
  if (!isRecord(parsed.draft) || !Array.isArray(parsed.results)) {
    return null;
  }
  if (!(parsed.selectedSeed === null || typeof parsed.selectedSeed === "number")) {
    return null;
  }
  if (!(parsed.lastSubmittedRequest === null || isRecord(parsed.lastSubmittedRequest))) {
    return null;
  }
  return {
    version: SEARCH_SESSION_VERSION,
    draft: cloneValue(parsed.draft as unknown as SearchDraft),
    results: cloneValue(parsed.results as unknown as SearchMatchSummary[]),
    selectedSeed: parsed.selectedSeed,
    lastSubmittedRequest:
      parsed.lastSubmittedRequest === null
        ? null
        : cloneValue(parsed.lastSubmittedRequest as unknown as SearchRequest),
  };
}

export function persistSearchSessionSnapshot(
  storage: SearchSessionStorage | null | undefined,
  state: SearchSessionStateSnapshot
): void {
  if (!storage) {
    return;
  }
  storage.setItem(
    SEARCH_SESSION_STORAGE_KEY,
    JSON.stringify(toSerializableSnapshot(state))
  );
}

export function restoreSearchSessionSnapshot(
  storage: SearchSessionStorage | null | undefined
): RestoredSearchSessionState | null {
  if (!storage) {
    return null;
  }
  const raw = storage.getItem(SEARCH_SESSION_STORAGE_KEY);
  if (!raw) {
    return null;
  }
  try {
    const snapshot = parseSerializedSnapshot(raw);
    if (!snapshot) {
      storage.removeItem(SEARCH_SESSION_STORAGE_KEY);
      return null;
    }
    const results = cloneValue(snapshot.results);
    const draft = normalizeSearchDraft(cloneValue(snapshot.draft));
    return {
      draft,
      results,
      selectedSeed: sanitizeSelectedSeed(results, snapshot.selectedSeed),
      lastSubmittedRequest: snapshot.lastSubmittedRequest
        ? normalizeSearchRequest(cloneValue(snapshot.lastSubmittedRequest))
        : null,
      activeWorldType: draft.worldType,
      activeMixing: draft.mixing,
      totalMatches: results.length,
    };
  } catch {
    storage.removeItem(SEARCH_SESSION_STORAGE_KEY);
    return null;
  }
}

export function clearSearchSessionSnapshot(
  storage: SearchSessionStorage | null | undefined
): void {
  if (!storage) {
    return;
  }
  storage.removeItem(SEARCH_SESSION_STORAGE_KEY);
}
