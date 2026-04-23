import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

import { shouldIgnoreSidecarStderr } from "../src/lib/tauri.ts";

const APP_TSX = readFileSync(new URL("../src/app/App.tsx", import.meta.url), "utf8");
const SEARCH_PANEL_TSX = readFileSync(
  new URL("../src/features/search/SearchPanel.tsx", import.meta.url),
  "utf8"
);
const PREVIEW_PANE_TSX = readFileSync(
  new URL("../src/features/preview/PreviewPane.tsx", import.meta.url),
  "utf8"
);

test("shouldIgnoreSidecarStderr ignores recoverable template placement diagnostics", () => {
  assert.equal(
    shouldIgnoreSidecarStderr("error ApplyTemplateRules:430 can not place all templates"),
    true
  );
});

test("frontend Alerts use title instead of deprecated message prop", () => {
  assert.doesNotMatch(APP_TSX, /<Alert[\s\S]*\bmessage=/);
  assert.doesNotMatch(SEARCH_PANEL_TSX, /<Alert[\s\S]*\bmessage=/);
  assert.doesNotMatch(PREVIEW_PANE_TSX, /<Alert[\s\S]*\bmessage=/);

  assert.match(APP_TSX, /<Alert[\s\S]*\btitle=/);
  assert.match(SEARCH_PANEL_TSX, /<Alert[\s\S]*\btitle=/);
  assert.match(PREVIEW_PANE_TSX, /<Alert[\s\S]*\btitle=/);
});
