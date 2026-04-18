import type { PreviewPayload } from "../lib/contracts";

export interface PreviewStoreSnapshot {
  activeKey: string | null;
  activePreview: PreviewPayload | null;
  cache: Record<string, PreviewPayload>;
  isLoading: boolean;
  lastError: string | null;
}

export function beginPreviewLoad(
  state: PreviewStoreSnapshot,
  key: string
): PreviewStoreSnapshot {
  const cached = state.cache[key];
  if (cached) {
    return {
      ...state,
      activeKey: key,
      activePreview: cached,
      isLoading: false,
      lastError: null,
    };
  }
  return {
    ...state,
    activeKey: key,
    activePreview: null,
    isLoading: true,
    lastError: null,
  };
}

export function completePreviewLoad(
  state: PreviewStoreSnapshot,
  key: string,
  preview: PreviewPayload
): PreviewStoreSnapshot {
  const nextCache = {
    ...state.cache,
    [key]: preview,
  };
  if (state.activeKey !== key) {
    return {
      ...state,
      cache: nextCache,
    };
  }
  return {
    ...state,
    activePreview: preview,
    cache: nextCache,
    isLoading: false,
    lastError: null,
  };
}

export function failPreviewLoad(
  state: PreviewStoreSnapshot,
  key: string,
  error: string
): PreviewStoreSnapshot {
  if (state.activeKey !== key) {
    return state;
  }
  return {
    ...state,
    isLoading: false,
    lastError: error,
  };
}
