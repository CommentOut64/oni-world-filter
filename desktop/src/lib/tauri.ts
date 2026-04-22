import { invoke } from "@tauri-apps/api/core";
import { listen, type UnlistenFn } from "@tauri-apps/api/event";

import type {
  GeyserOption,
  PreviewEvent,
  PreviewRequest,
  SearchAnalysisPayload,
  SearchAnalyzeRequest,
  SearchCatalog,
  SearchRequest,
  SidecarEvent,
  SidecarStderrEvent,
  WorldOption,
} from "./contracts";
import { FALLBACK_SEARCH_CATALOG, normalizeSearchCatalog } from "./searchCatalog.ts";

const SIDECAR_EVENT_CHANNEL = "sidecar://event";
const SIDECAR_STDERR_CHANNEL = "sidecar://stderr";

function inTauriRuntime(): boolean {
  return typeof window !== "undefined" && "__TAURI_INTERNALS__" in window;
}

function normalizeError(error: unknown): string {
  if (typeof error === "string") {
    return error;
  }
  if (error instanceof Error) {
    return error.message;
  }
  return "未知错误";
}

export async function startSearch(request: SearchRequest): Promise<void> {
  if (!inTauriRuntime()) {
    throw new Error("当前不在 Tauri 运行时，无法启动搜索。");
  }
  await invoke("start_search", { request });
}

export async function cancelSearch(jobId: string): Promise<void> {
  if (!inTauriRuntime()) {
    throw new Error("当前不在 Tauri 运行时，无法取消搜索。");
  }
  await invoke("cancel_search", { jobId });
}

export async function loadPreview(request: PreviewRequest): Promise<PreviewEvent> {
  if (!inTauriRuntime()) {
    throw new Error("当前不在 Tauri 运行时，无法加载预览。");
  }
  return invoke<PreviewEvent>("load_preview", { request });
}

export async function listWorlds(): Promise<WorldOption[]> {
  if (!inTauriRuntime()) {
    return FALLBACK_SEARCH_CATALOG.worlds;
  }
  return invoke<WorldOption[]>("list_worlds");
}

export async function listGeysers(): Promise<GeyserOption[]> {
  if (!inTauriRuntime()) {
    return FALLBACK_SEARCH_CATALOG.geysers;
  }
  return invoke<GeyserOption[]>("list_geysers");
}

export async function getSearchCatalog(): Promise<SearchCatalog> {
  if (!inTauriRuntime()) {
    return FALLBACK_SEARCH_CATALOG;
  }
  const catalog = await invoke<SearchCatalog>("get_search_catalog");
  return normalizeSearchCatalog(catalog);
}

export async function analyzeSearchRequest(
  request: SearchAnalyzeRequest
): Promise<SearchAnalysisPayload> {
  if (!inTauriRuntime()) {
    throw new Error("当前不在 Tauri 运行时，无法调用 analyze_search_request。");
  }
  return invoke<SearchAnalysisPayload>("analyze_search_request", { request });
}

export async function subscribeSidecar(
  onEvent: (event: SidecarEvent) => void,
  onStdErr: (event: SidecarStderrEvent) => void
): Promise<UnlistenFn> {
  if (!inTauriRuntime()) {
    return () => undefined;
  }

  const unlistenEvent = await listen<SidecarEvent>(SIDECAR_EVENT_CHANNEL, (payload) => {
    onEvent(payload.payload);
  });
  const unlistenStdErr = await listen<SidecarStderrEvent>(SIDECAR_STDERR_CHANNEL, (payload) => {
    onStdErr(payload.payload);
  });

  return () => {
    unlistenEvent();
    unlistenStdErr();
  };
}

export function formatTauriError(error: unknown): string {
  return normalizeError(error);
}
