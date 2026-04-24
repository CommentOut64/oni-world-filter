import { save } from "@tauri-apps/plugin-dialog";
import { writeFile } from "@tauri-apps/plugin-fs";

import { inTauriRuntime } from "../../lib/tauri.ts";

export interface ExportPreviewPngParams {
  dataUrl: string;
  seed: number;
}

export interface ExportPreviewPngDeps {
  inTauriRuntime: () => boolean;
  chooseSavePath: (defaultPath: string) => Promise<string | null>;
  writeBinaryFile: (path: string, bytes: Uint8Array) => Promise<void>;
  downloadDataUrl: (dataUrl: string, fileName: string) => void;
}

const DEFAULT_DEPS: ExportPreviewPngDeps = {
  inTauriRuntime,
  chooseSavePath: async (defaultPath) =>
    save({
      title: "导出地图预览 PNG",
      defaultPath,
      filters: [
        {
          name: "PNG 图片",
          extensions: ["png"],
        },
      ],
    }),
  writeBinaryFile: async (path, bytes) => {
    await writeFile(path, bytes);
  },
  downloadDataUrl: (dataUrl, fileName) => {
    const link = document.createElement("a");
    link.href = dataUrl;
    link.download = fileName;
    link.style.display = "none";
    document.body.append(link);
    link.click();
    link.remove();
  },
};

export function buildPreviewPngFileName(seed: number): string {
  return `oni-preview-${seed}.png`;
}

export function decodePngDataUrl(dataUrl: string): Uint8Array {
  const prefix = "data:image/png;base64,";
  if (!dataUrl.startsWith(prefix)) {
    throw new Error("导出的 PNG 数据无效。");
  }
  const decoded = atob(dataUrl.slice(prefix.length));
  return Uint8Array.from(decoded, (char) => char.charCodeAt(0));
}

export async function exportPreviewPng(
  params: ExportPreviewPngParams,
  deps: ExportPreviewPngDeps = DEFAULT_DEPS
): Promise<void> {
  const fileName = buildPreviewPngFileName(params.seed);
  if (!deps.inTauriRuntime()) {
    deps.downloadDataUrl(params.dataUrl, fileName);
    return;
  }

  const path = await deps.chooseSavePath(fileName);
  if (!path) {
    return;
  }
  await deps.writeBinaryFile(path, decodePngDataUrl(params.dataUrl));
}
