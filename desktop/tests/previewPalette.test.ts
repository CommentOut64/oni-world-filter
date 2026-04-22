import test from "node:test";
import assert from "node:assert/strict";

import { zoneFillColor } from "../src/features/preview/previewPalette.ts";

test("preview palette separates wasteland and barren biome tones", () => {
  assert.equal(zoneFillColor(13), "#b79a54B3");
  assert.equal(zoneFillColor(16), "#7c7f82B3");
});
