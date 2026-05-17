import { invoke } from "@tauri-apps/api/core";
import { listen, type UnlistenFn } from "@tauri-apps/api/event";

import type {
  CoordPreviewEvent,
  CoordPreviewRequest,
  GeyserOption,
  PreviewEvent,
  PreviewGeyserDetailsEvent,
  PreviewGeyserDetailsRequest,
  PreviewRequest,
  SearchAnalysisPayload,
  SearchAnalyzeRequest,
  SearchCatalog,
  SearchRequest,
  SidecarEvent,
  SidecarStderrEvent,
  ExportReportPdfRequest,
  WorldReportEvent,
  WorldReportRequest,
  WorldOption,
} from "./contracts";
import { FALLBACK_SEARCH_CATALOG, normalizeSearchCatalog } from "./searchCatalog.ts";

const SIDECAR_EVENT_CHANNEL = "sidecar://event";
const SIDECAR_STDERR_CHANNEL = "sidecar://stderr";

export function shouldIgnoreSidecarStderr(message: string): boolean {
  const normalized = message.trim();
  return (
    normalized.startsWith("[sidecar-diagnostic]") ||
    (normalized.includes("RelaxGeneratedChildren") &&
      normalized.includes("fallback to compute node.")) ||
    normalized.includes("pdfailed, fallback to compute node.") ||
    normalized.includes("compute child node pd failed, fallback to compute node.") ||
    normalized.includes("compute node pd failed, fallback to compute node.") ||
    normalized.includes("compute node pd failed after convert unknown cells") ||
    normalized.includes("start location should not overlap bounds.") ||
    normalized.includes("override placement is wrong, rule:") ||
    normalized.includes("the site is already used.") ||
    normalized.includes("can not place all templates") ||
    (normalized.includes("Intersect:") && normalized.includes("intersection result is empty.")) ||
    (normalized.includes("Intersect:") && normalized.includes("subj:") && normalized.includes("clip:"))
  );
}

export function inTauriRuntime(): boolean {
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

export function formatNativeDisplayMessage(message: string): string {
  const normalized = message.trim();
  if (!normalized) {
    return "未知错误";
  }
  if (normalized.includes("secondary preview is not available for current seed")) {
    return "当前种子没有可用的副星预览。";
  }
  if (normalized.includes("invalid native coord")) {
    return "坐标格式无效，尾部混搭编码必须是 0 或 5 位 base36。";
  }
  if (normalized.includes("another search job is still running")) {
    return "已有搜索任务仍在运行，请稍后再试。";
  }
  if (normalized.includes("job is not running")) {
    return "当前任务未在运行。";
  }
  if (normalized.includes("preview payload is empty")) {
    return "预览结果为空。";
  }
  if (normalized.includes("world report payload is empty")) {
    return "地图报告结果为空。";
  }
  if (normalized.includes("failed to load shared settings cache")) {
    return "加载设置缓存失败。";
  }
  if (normalized.includes("failed to open settings asset blob")) {
    return "打开设置资源失败。";
  }
  if (normalized.includes("failed to read settings asset blob")) {
    return "读取设置资源失败。";
  }
  if (normalized.includes("settings asset blob is empty")) {
    return "设置资源为空。";
  }
  if (normalized.includes("sidecar command crashed")) {
    return "后端命令执行异常中断。";
  }
  if (normalized.includes("search thread crashed")) {
    return "搜索线程异常中断。";
  }
  return normalized;
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

export async function loadPreviewGeyserDetails(
  request: PreviewGeyserDetailsRequest
): Promise<PreviewGeyserDetailsEvent> {
  if (!inTauriRuntime()) {
    throw new Error("当前不在 Tauri 运行时，无法加载喷口参数详情。");
  }
  return invoke<PreviewGeyserDetailsEvent>("load_preview_geyser_details", { request });
}

export async function loadPreviewByCoord(
  request: CoordPreviewRequest
): Promise<CoordPreviewEvent> {
  if (!inTauriRuntime()) {
    throw new Error("当前不在 Tauri 运行时，无法加载坐标预览。");
  }
  return invoke<CoordPreviewEvent>("load_preview_by_coord", { request });
}

export async function getWorldReport(
  request: WorldReportRequest
): Promise<WorldReportEvent> {
  if (!inTauriRuntime()) {
    throw new Error("当前不在 Tauri 运行时，无法加载地图报告。");
  }
  return invoke<WorldReportEvent>("get_world_report", { request });
}

export async function exportReportPdf(
  request: ExportReportPdfRequest
): Promise<void> {
  if (!inTauriRuntime()) {
    throw new Error("当前不在 Tauri 运行时，无法导出 PDF 报告。");
  }
  await invoke("export_report_pdf", { request });
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
    if (shouldIgnoreSidecarStderr(payload.payload.message)) {
      return;
    }
    onStdErr(payload.payload);
  });

  return () => {
    unlistenEvent();
    unlistenStdErr();
  };
}

export function formatTauriError(error: unknown): string {
  return formatNativeDisplayMessage(normalizeError(error));
}
