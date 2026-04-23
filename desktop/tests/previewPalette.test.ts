import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

import { createPreviewPalette, zoneFillColor } from "../src/features/preview/previewPalette.ts";

const PREVIEW_CANVAS_SOURCE = readFileSync(
  new URL("../src/features/preview/PreviewCanvas.tsx", import.meta.url),
  "utf8"
);

test("preview palette adapts canvas chrome between dark and light theme", () => {
  const darkPalette = createPreviewPalette("dark");
  const lightPalette = createPreviewPalette("light");

  assert.equal(darkPalette.background, "#0f1726");
  assert.equal(lightPalette.background, "#f4f4f6");
  assert.equal(darkPalette.label, "rgba(230, 244, 255, 0.92)");
  assert.equal(lightPalette.label, "rgba(55, 58, 65, 0.88)");
  assert.notEqual(darkPalette.boundary, lightPalette.boundary);
  assert.notEqual(darkPalette.startMarker, lightPalette.startMarker);
});

test("preview canvas wrapper uses the same background color as the konva palette", () => {
  assert.match(
    PREVIEW_CANVAS_SOURCE,
    /<section\s+className="preview-canvas-wrap"[\s\S]*style=\{\{\s*backgroundColor:\s*previewPalette\.background\s*\}\}/
  );
});

test("preview palette separates wasteland and barren biome tones", () => {
  assert.equal(zoneFillColor(13, "dark"), "#d4b872CC");
  assert.equal(zoneFillColor(16, "dark"), "#9a9ea0CC");
});

test("zone fill uses higher opacity in light mode for vivid colors", () => {
  assert.equal(zoneFillColor(13, "light"), "#d4b872E6");
  assert.equal(zoneFillColor(16, "light"), "#9a9ea0E6");
});
