import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

import type { PreviewPayload } from "../src/lib/contracts.ts";
import {
  buildReportStageLayout,
  renderPreviewReportImage,
} from "../src/features/report/renderPreviewReportImage.ts";

const PREVIEW_CANVAS_SOURCE = readFileSync(
  new URL("../src/features/preview/PreviewCanvas.tsx", import.meta.url),
  "utf8"
);
const OFFSCREEN_STAGE_SOURCE = readFileSync(
  new URL("../src/features/preview/OffscreenPreviewStage.tsx", import.meta.url),
  "utf8"
);

const PREVIEW: PreviewPayload = {
  summary: {
    seed: 100123,
    worldType: 13,
    start: { x: 12, y: 34 },
    worldSize: { w: 256, h: 384 },
    traits: [],
    geysers: [{ type: 0, x: 70, y: 90, id: "steam" }],
  },
  polygons: [],
};

test("buildReportStageLayout uses fixed 4000px width with export scale not limited by interactive viewport clamp", () => {
  const layout = buildReportStageLayout(PREVIEW);

  assert.equal(layout.stageWidth, 4000);
  assert.equal(layout.stageHeight, 5928);
  assert.equal(layout.viewport.x, 24);
  assert.equal(layout.viewport.y, 0);
  assert.equal(layout.viewport.scale, 15.4375);
});

test("renderPreviewReportImage always requests boundaries labels and geysers for report export", async () => {
  const snapshots: Array<Record<string, unknown>> = [];

  const dataUrl = await renderPreviewReportImage(
    {
      preview: PREVIEW,
      geysers: [{ id: 0, key: "steam" }],
      themeMode: "dark",
    },
    {
      snapshotPreviewScene: async (request) => {
        snapshots.push(request as Record<string, unknown>);
        return "data:image/png;base64,AAA=";
      },
    }
  );

  assert.equal(dataUrl, "data:image/png;base64,AAA=");
  assert.equal(snapshots.length, 1);
  assert.equal(snapshots[0].showBoundaries, true);
  assert.equal(snapshots[0].showLabels, true);
  assert.equal(snapshots[0].showGeysers, true);
  assert.equal(snapshots[0].selectedGeyserIndex, null);
  assert.equal(snapshots[0].stageWidth, 4000);
  assert.equal(snapshots[0].stageHeight, 5928);
});

test("visible preview keeps screen-sized primitives while offscreen report scales preview primitives proportionally", () => {
  assert.match(PREVIEW_CANVAS_SOURCE, /fontSize=\{LABEL_FONT_PX \/ viewport\.scale\}/);
  assert.match(
    PREVIEW_CANVAS_SOURCE,
    /offsetX=\{label\.kind === "region" \? 40 \/ viewport\.scale : -1 \/ viewport\.scale\}/
  );
  assert.match(
    PREVIEW_CANVAS_SOURCE,
    /width=\{label\.kind === "region" \? 80 \/ viewport\.scale : undefined\}/
  );
  assert.match(
    OFFSCREEN_STAGE_SOURCE,
    /fontSize=\{\(LABEL_FONT_PX \* reportVisualScale\) \/ viewport\.scale\}/
  );
  assert.match(
    OFFSCREEN_STAGE_SOURCE,
    /offsetX=\{[\s\S]*\(40 \* reportVisualScale\) \/ viewport\.scale[\s\S]*\(-1 \* reportVisualScale\) \/ viewport\.scale[\s\S]*\}/
  );
  assert.match(
    OFFSCREEN_STAGE_SOURCE,
    /width=\{label\.kind === "region" \? \(80 \* reportVisualScale\) \/ viewport\.scale : undefined\}/
  );
  assert.match(
    OFFSCREEN_STAGE_SOURCE,
    /radius=\{\(5 \* reportVisualScale\) \/ viewport\.scale\}/
  );
  assert.match(
    OFFSCREEN_STAGE_SOURCE,
    /radius=\{\(6 \* reportVisualScale\) \/ viewport\.scale\}/
  );
  assert.match(
    OFFSCREEN_STAGE_SOURCE,
    /strokeWidth=\{showBoundaries \? \(1 \* reportVisualScale\) \/ viewport\.scale : 0\}/
  );
});
