import { create } from "zustand";

import type {
  PreviewPayload,
  PreviewTarget,
  SearchMatchSummary,
} from "../lib/contracts.ts";
import { formatTauriError, loadPreview, loadPreviewGeyserDetails } from "../lib/tauri.ts";
import {
  beginGeyserDetailsLoad,
  beginPreviewLoad,
  completeGeyserDetailsLoad,
  completePreviewLoad,
  failGeyserDetailsLoad,
  failPreviewLoad,
  previewKeyForMatch,
  primeResolvedPreviewState,
  setActiveTargetState,
  type PreviewStoreSnapshot,
} from "./previewStoreState.ts";

interface PreviewState extends PreviewStoreSnapshot {
  loadByMatch: (match: SearchMatchSummary, target?: PreviewTarget) => Promise<void>;
  primeResolvedPreview: (match: SearchMatchSummary, preview: PreviewPayload) => void;
  setActiveTarget: (target: PreviewTarget) => void;
  clear: () => void;
  clearError: () => void;
}

export const usePreviewStore = create<PreviewState>((set, get) => ({
  activeKey: null,
  activeTarget: "primary",
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
  loadByMatch: async (match, target = "primary") => {
    const requestGeyserDetails = async (
      key: string,
      requestSerial: number,
      detailTarget: PreviewTarget
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
          target: detailTarget,
        });
        set((state) => completeGeyserDetailsLoad(state, key, event.geyserDetails, requestSerial));
      } catch (error) {
        set((state) => failGeyserDetailsLoad(state, key, formatTauriError(error), requestSerial));
      }
    };

    const key = previewKeyForMatch(match, target);
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
        void requestGeyserDetails(key, requestSerial, target);
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
        target,
      });
      set((state) => completePreviewLoad(state, key, event.preview, requestSerial));
      void requestGeyserDetails(key, requestSerial, target);
    } catch (error) {
      const message = formatTauriError(error);
      set((state) =>
        failPreviewLoad(
          state,
          key,
          target === "secondary" ? `副星预览加载失败: ${message}` : message,
          requestSerial
        )
      );
      if (target === "secondary") {
        set((state) => ({
          ...state,
          isLoading: false,
          lastError: `副星预览加载失败: ${message}`,
        }));
      }
    }
  },
  primeResolvedPreview: (match, preview) => {
    set((state) => primeResolvedPreviewState(state, match, preview));
  },
  setActiveTarget: (target) => {
    set((state) => setActiveTargetState(state, target));
  },
  clear: () => {
    set({
      activeKey: null,
      activeTarget: "primary",
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
