import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createElement } from "react";
import { renderToStaticMarkup } from "react-dom/server";

import PreviewToolbar from "../src/features/preview/PreviewToolbar.tsx";

const PREVIEW_PANE_SOURCE = readFileSync(
  new URL("../src/features/preview/PreviewPane.tsx", import.meta.url),
  "utf8"
);

test("PreviewToolbar shows loading and disabled state while report generation is running", () => {
  const markup = renderToStaticMarkup(
    createElement(PreviewToolbar, {
      showBoundaries: true,
      showLabels: true,
      showGeysers: true,
      geyserCount: 2,
      isGeneratingReport: true,
      onToggleBoundaries: () => undefined,
      onToggleLabels: () => undefined,
      onToggleGeysers: () => undefined,
      onResetView: () => undefined,
      onGenerateReport: () => undefined,
      onOpenGeyserList: () => undefined,
    })
  );

  assert.match(markup, /ant-btn-loading/);
  assert.match(markup, /disabled=""/);
});

test("PreviewPane manages report generation lifecycle, success toast and error propagation", () => {
  assert.match(PREVIEW_PANE_SOURCE, /const \[isGeneratingReport,\s*setIsGeneratingReport\] = useState\(false\);/);
  assert.match(PREVIEW_PANE_SOURCE, /setIsGeneratingReport\(true\);/);
  assert.match(PREVIEW_PANE_SOURCE, /const exported = await exportWorldReport\(/);
  assert.match(PREVIEW_PANE_SOURCE, /if \(exported\) \{\s*void message\.success\("报告已生成。"\);\s*\}/);
  assert.match(PREVIEW_PANE_SOURCE, /lastError:\s*`生成报告失败: \$\{formatTauriError\(error\)\}`/);
  assert.match(PREVIEW_PANE_SOURCE, /finally \{\s*setIsGeneratingReport\(false\);\s*\}/);
  assert.match(PREVIEW_PANE_SOURCE, /isGeneratingReport=\{isGeneratingReport\}/);
});
