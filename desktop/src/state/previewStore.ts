import { create } from "zustand";

import type { PreviewPayload, SearchMatchSummary } from "../lib/contracts.ts";
import { formatTauriError, loadPreview, loadPreviewGeyserDetails } from "../lib/tauri.ts";
import {
  beginGeyserDetailsLoad,
  beginPreviewLoad,
  completeGeyserDetailsLoad,
  completePreviewLoad,
  failGeyserDetailsLoad,
  failPreviewLoad,
  previewKey,
  primeResolvedPreviewState,
  type PreviewStoreSnapshot,
} from "./previewStoreState.ts";

interface PreviewState extends PreviewStoreSnapshot {
  loadByMatch: (match: SearchMatchSummary) => Promise<void>;
  primeResolvedPreview: (match: SearchMatchSummary, preview: PreviewPayload) => void;
  clear: () => void;
  clearError: () => void;
}

export const usePreviewStore = create<PreviewState>((set, get) => ({
  activeKey: null,
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
  loadByMatch: async (match) => {
    const requestGeyserDetails = async (
      key: string,
      requestSerial: number
    ): Promise<void> => {
      const current = get();
      const cachedPreview = current.cache[key]?.preview;
      if (!cachedPreview) {
        return;
      }
      if (current.inflightDetailKeys[key] !== undefined) {
        return;
      }

      set((state) => beginGeyserDetailsLoad(state, key, requestSerial));
      try {
        const event = await loadPreviewGeyserDetails({
          jobId: `preview-geyser-details-${requestSerial}-${match.seed}`,
          worldType: match.worldType,
          seed: match.seed,
          mixing: match.mixing,
          worldHeight: cachedPreview.summary.worldSize.h,
          geysers: cachedPreview.summary.geysers,
        });
        set((state) => completeGeyserDetailsLoad(state, key, event.geyserDetails, requestSerial));
      } catch (error) {
        set((state) => failGeyserDetailsLoad(state, key, formatTauriError(error), requestSerial));
      }
    };

    const key = previewKey(match);
    const current = get();
    const inflightPreviewSerial = current.inflightPreviewKeys[key];
    const inflightDetailSerial = current.inflightDetailKeys[key];
    const cached = current.cache[key];
    const requestSerial = inflightPreviewSerial ?? inflightDetailSerial ?? current.requestSerial + 1;

    set((state) => beginPreviewLoad(state, key, requestSerial));
    if (inflightPreviewSerial !== undefined) {
      return;
    }

    if (cached) {
      if (cached.geyserDetailsStatus !== "ready" && inflightDetailSerial === undefined) {
        void requestGeyserDetails(key, requestSerial);
      }
      return;
    }

    set((state) => ({
      ...state,
      inflightPreviewKeys: {
        ...state.inflightPreviewKeys,
        [key]: requestSerial,
      },
    }));

    try {
      const event = await loadPreview({
        jobId: `preview-${Date.now()}-${match.seed}`,
        worldType: match.worldType,
        seed: match.seed,
        mixing: match.mixing,
      });
      set((state) => completePreviewLoad(state, key, event.preview, requestSerial));
      void requestGeyserDetails(key, requestSerial);
    } catch (error) {
      set((state) => failPreviewLoad(state, key, formatTauriError(error), requestSerial));
    }
  },
  primeResolvedPreview: (match, preview) => {
    set((state) => primeResolvedPreviewState(state, match, preview));
  },
  clear: () => {
    set({
      activeKey: null,
      activePreview: null,
      activeGeyserDetailsStatus: "idle",
      activeGeyserDetails: [],
      activeGeyserDetailsError: null,
      isLoading: false,
      lastError: null,
      inflightPreviewKeys: {},
      inflightDetailKeys: {},
      requestSerial: 0,
    });
  },
  clearError: () => {
    set({ lastError: null });
  },
}));
