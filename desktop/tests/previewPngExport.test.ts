import test from "node:test";
import assert from "node:assert/strict";

import { exportPreviewPng } from "../src/features/preview/exportPreviewPng.ts";

test("exportPreviewPng writes decoded PNG bytes to the user-selected path in Tauri runtime", async () => {
  const writes: Array<{ path: string; bytes: Uint8Array }> = [];
  const downloads: Array<{ dataUrl: string; fileName: string }> = [];
  const defaultPaths: string[] = [];

  await exportPreviewPng(
    {
      dataUrl: "data:image/png;base64,AQID",
      seed: 1522766653,
    },
    {
      inTauriRuntime: () => true,
      chooseSavePath: async (defaultPath) => {
        defaultPaths.push(defaultPath);
        return "C:\\Users\\wgh\\Desktop\\oni-preview-1522766653.png";
      },
      writeBinaryFile: async (path, bytes) => {
        writes.push({ path, bytes });
      },
      downloadDataUrl: (dataUrl, fileName) => {
        downloads.push({ dataUrl, fileName });
      },
    }
  );

  assert.deepEqual(defaultPaths, ["oni-preview-1522766653.png"]);
  assert.equal(writes.length, 1);
  assert.equal(writes[0]?.path, "C:\\Users\\wgh\\Desktop\\oni-preview-1522766653.png");
  assert.deepEqual(Array.from(writes[0]?.bytes ?? []), [1, 2, 3]);
  assert.deepEqual(downloads, []);
});

test("exportPreviewPng does not write any file when the user cancels the save dialog", async () => {
  let writeCount = 0;

  await exportPreviewPng(
    {
      dataUrl: "data:image/png;base64,AQID",
      seed: 42,
    },
    {
      inTauriRuntime: () => true,
      chooseSavePath: async () => null,
      writeBinaryFile: async () => {
        writeCount += 1;
      },
      downloadDataUrl: () => {
        throw new Error("cancelled export should not fall back to browser download");
      },
    }
  );

  assert.equal(writeCount, 0);
});

test("exportPreviewPng falls back to browser download outside Tauri runtime", async () => {
  const downloads: Array<{ dataUrl: string; fileName: string }> = [];

  await exportPreviewPng(
    {
      dataUrl: "data:image/png;base64,AQID",
      seed: 77,
    },
    {
      inTauriRuntime: () => false,
      chooseSavePath: async () => {
        throw new Error("browser fallback should not open Tauri save dialog");
      },
      writeBinaryFile: async () => {
        throw new Error("browser fallback should not write files via Tauri fs");
      },
      downloadDataUrl: (dataUrl, fileName) => {
        downloads.push({ dataUrl, fileName });
      },
    }
  );

  assert.deepEqual(downloads, [
    {
      dataUrl: "data:image/png;base64,AQID",
      fileName: "oni-preview-77.png",
    },
  ]);
});
