import { create } from "zustand";

import type { PreviewPayload, SearchMatchSummary } from "../lib/contracts";
import { formatTauriError, loadPreview } from "../lib/tauri";
import {
  beginPreviewLoad,
  completePreviewLoad,
  failPreviewLoad,
} from "./previewStoreState";

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
