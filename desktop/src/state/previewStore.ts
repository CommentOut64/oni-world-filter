import { create } from "zustand";

import type { PreviewPayload, SearchMatchSummary } from "../lib/contracts";
import { formatTauriError, loadPreview } from "../lib/tauri.ts";
import {
  beginPreviewLoad,
  completePreviewLoad,
  failPreviewLoad,
  previewKey,
  primeResolvedPreviewState,
} from "./previewStoreState";

interface PreviewState {
  activeKey: string | null;
  activePreview: PreviewPayload | null;
  cache: Record<string, PreviewPayload>;
  isLoading: boolean;
  lastError: string | null;
  loadByMatch: (match: SearchMatchSummary) => Promise<void>;
  primeResolvedPreview: (match: SearchMatchSummary, preview: PreviewPayload) => void;
  clear: () => void;
  clearError: () => void;
}

export const usePreviewStore = create<PreviewState>((set, get) => ({
  activeKey: null,
  activePreview: null,
  cache: {},
  isLoading: false,
  lastError: null,
  loadByMatch: async (match) => {
    const key = previewKey(match);
    if (get().cache[key]) {
      set((state) => beginPreviewLoad(state, key));
      return;
    }

    set((state) => beginPreviewLoad(state, key));
    try {
      const event = await loadPreview({
        jobId: `preview-${Date.now()}-${match.seed}`,
        worldType: match.worldType,
        seed: match.seed,
        mixing: match.mixing,
      });
      set((state) => completePreviewLoad(state, key, event.preview));
    } catch (error) {
      set((state) => failPreviewLoad(state, key, formatTauriError(error)));
    }
  },
  primeResolvedPreview: (match, preview) => {
    set((state) => primeResolvedPreviewState(state, match, preview));
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
