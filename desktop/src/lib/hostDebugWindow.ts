import type { SearchRequest } from "./contracts";

const HOST_DEBUG_WINDOW_LABEL = "host-debug-window";
const HOST_DEBUG_CHANNEL = "oni-host-debug-channel";
const HOST_DEBUG_STORAGE_KEY = "oni-host-debug-snapshot";
const HOST_DEBUG_WINDOW_QUERY = "host-debug-window";

export interface HostDebugSnapshot {
  request: SearchRequest | null;
  messages: string[];
}

function inBrowserWindow(): boolean {
  return typeof window !== "undefined";
}

function getWindowUrl(): string {
  if (!inBrowserWindow()) {
    return `/?${HOST_DEBUG_WINDOW_QUERY}=1`;
  }
  const url = new URL(window.location.href);
  url.searchParams.set(HOST_DEBUG_WINDOW_QUERY, "1");
  return url.toString();
}

export function isHostDebugWindow(): boolean {
  if (!inBrowserWindow()) {
    return false;
  }
  const params = new URLSearchParams(window.location.search);
  return params.get(HOST_DEBUG_WINDOW_QUERY) === "1";
}

export function buildHostDebugText(snapshot: HostDebugSnapshot): string {
  const sections = [
    "Host 实际发送给 sidecar 的调试信息",
    "",
    "前端提交请求:",
    JSON.stringify(snapshot.request, null, 2),
    "",
    "Host 调试消息:",
    snapshot.messages.length > 0 ? snapshot.messages.join("\n") : "(暂无)",
  ];
  return sections.join("\n");
}

function persistSnapshot(snapshot: HostDebugSnapshot): void {
  if (!inBrowserWindow()) {
    return;
  }
  window.localStorage.setItem(HOST_DEBUG_STORAGE_KEY, JSON.stringify(snapshot));
}

export function readPersistedHostDebugSnapshot(): HostDebugSnapshot {
  if (!inBrowserWindow()) {
    return { request: null, messages: [] };
  }
  const raw = window.localStorage.getItem(HOST_DEBUG_STORAGE_KEY);
  if (!raw) {
    return { request: null, messages: [] };
  }
  try {
    const parsed = JSON.parse(raw) as HostDebugSnapshot;
    return {
      request: parsed.request ?? null,
      messages: Array.isArray(parsed.messages) ? parsed.messages : [],
    };
  } catch {
    return { request: null, messages: [] };
  }
}

export function publishHostDebugSnapshot(snapshot: HostDebugSnapshot): void {
  persistSnapshot(snapshot);
  if (!inBrowserWindow() || typeof BroadcastChannel === "undefined") {
    return;
  }
  const channel = new BroadcastChannel(HOST_DEBUG_CHANNEL);
  channel.postMessage(snapshot);
  channel.close();
}

export function subscribeHostDebugSnapshot(
  onSnapshot: (snapshot: HostDebugSnapshot) => void
): () => void {
  if (!inBrowserWindow() || typeof BroadcastChannel === "undefined") {
    return () => undefined;
  }
  const channel = new BroadcastChannel(HOST_DEBUG_CHANNEL);
  const handleMessage = (event: MessageEvent<HostDebugSnapshot>) => {
    onSnapshot(event.data);
  };
  channel.addEventListener("message", handleMessage);
  return () => {
    channel.removeEventListener("message", handleMessage);
    channel.close();
  };
}

export async function openHostDebugWindow(snapshot: HostDebugSnapshot): Promise<void> {
  publishHostDebugSnapshot(snapshot);

  if (!inBrowserWindow()) {
    return;
  }

  if ("__TAURI_INTERNALS__" in window) {
    const { WebviewWindow } = await import("@tauri-apps/api/webviewWindow");
    const existing = await WebviewWindow.getByLabel(HOST_DEBUG_WINDOW_LABEL);
    if (existing) {
      await existing.show();
      await existing.setFocus();
      return;
    }
    const debugWindow = new WebviewWindow(HOST_DEBUG_WINDOW_LABEL, {
      url: getWindowUrl(),
      title: "Host 调试窗口",
      width: 920,
      height: 760,
      resizable: true,
      center: true,
      focus: true,
    });
    await new Promise<void>((resolve, reject) => {
      const unlisten = debugWindow.once("tauri://created", () => {
        unlisten.then((off) => off());
        resolve();
      });
      const unlistenError = debugWindow.once("tauri://error", (event) => {
        unlistenError.then((off) => off());
        reject(event.payload);
      });
    });
    return;
  }

  const popup = window.open(getWindowUrl(), HOST_DEBUG_WINDOW_LABEL, "popup,width=920,height=760");
  popup?.focus();
}
