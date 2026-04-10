import { create } from "zustand";

import type { PreviewPayload, SearchMatchSummary } from "../lib/contracts";
import { formatTauriError, loadPreview } from "../lib/tauri";

interface PreviewState {
  activeKey: string | null;
  activePreview: PreviewPayload | null;
  cache: Record<string, PreviewPayload>;
  isLoading: boolean;
  lastError: string | null;
  loadByMatch: (match: SearchMatchSummary) => Promise<void>;
  clear: () => void;
  clearError: () => void;
}

function previewKey(match: SearchMatchSummary): string {
  return `${match.worldType}:${match.seed}:${match.mixing}`;
}

export const usePreviewStore = create<PreviewState>((set, get) => ({
  activeKey: null,
  activePreview: null,
  cache: {},
  isLoading: false,
  lastError: null,
  loadByMatch: async (match) => {
    const key = previewKey(match);
    const cached = get().cache[key];
    if (cached) {
      set({
        activeKey: key,
        activePreview: cached,
        lastError: null,
      });
      return;
    }

    set({
      activeKey: key,
      isLoading: true,
      lastError: null,
    });
    try {
      const event = await loadPreview({
        jobId: `preview-${Date.now()}-${match.seed}`,
        worldType: match.worldType,
        seed: match.seed,
        mixing: match.mixing,
      });
      set((state) => ({
        isLoading: false,
        activePreview: event.preview,
        cache: {
          ...state.cache,
          [key]: event.preview,
        },
      }));
    } catch (error) {
      set({
        isLoading: false,
        lastError: formatTauriError(error),
      });
    }
  },
  clear: () => {
    set({
      activeKey: null,
      activePreview: null,
      isLoading: false,
      lastError: null,
    });
  },
  clearError: () => {
    set({ lastError: null });
  },
}));
