import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

const APP_CSS = readFileSync(new URL("../src/app/app.css", import.meta.url), "utf8");
const PREVIEW_PANE_SOURCE = readFileSync(
  new URL("../src/features/preview/PreviewPane.tsx", import.meta.url),
  "utf8"
);

test("PreviewPane renders a dedicated world switch group beside the preview title", () => {
  assert.match(
    PREVIEW_PANE_SOURCE,
    /const selectedWorldCategory\s*=\s*selectedMatch\s*\?\s*findCategoryForWorld\(worlds,\s*selectedMatch\.worldType\)\s*:\s*null/
  );
  assert.match(
    PREVIEW_PANE_SOURCE,
    /const isMoonletResult\s*=\s*selectedWorldCategory\s*===\s*"moonletCluster"/
  );
  assert.match(
    PREVIEW_PANE_SOURCE,
    /const showWorldSwitch\s*=\s*Boolean\(isMoonletResult\s*\|\|\s*hasSecondaryPreview\)/
  );
  assert.match(
    PREVIEW_PANE_SOURCE,
    /const secondarySwitchDisabled\s*=\s*!hasSecondaryPreview/
  );
  assert.match(
    PREVIEW_PANE_SOURCE,
    /<div className="preview-pane-title-row">[\s\S]*<Typography\.Title level=\{3\}>地图预览<\/Typography\.Title>[\s\S]*<Segmented<PreviewTarget>[\s\S]*className="theme-toggle preview-world-switch"/
  );
  assert.match(PREVIEW_PANE_SOURCE, /label:\s*"主星",\s*value:\s*"primary"/);
  assert.match(PREVIEW_PANE_SOURCE, /label:\s*"副星",\s*value:\s*"secondary",\s*disabled:\s*secondarySwitchDisabled/);
  assert.match(
    APP_CSS,
    /\.preview-world-switch\s*\{[\s\S]*flex:\s*0 0 auto;[\s\S]*\}/
  );
});

test("PreviewPane requests secondary on first switch and returns to primary through setActiveTarget", () => {
  assert.match(
    PREVIEW_PANE_SOURCE,
    /onChange=\{\(value\)\s*=>\s*\{[\s\S]*if\s*\(value\s*===\s*"primary"\)\s*\{[\s\S]*setActiveTarget\("primary"\);[\s\S]*return;[\s\S]*\}[\s\S]*if\s*\(!selectedMatch\)\s*\{[\s\S]*return;[\s\S]*\}[\s\S]*void loadByMatch\(selectedMatch,\s*"secondary"\);/s
  );
  assert.match(
    PREVIEW_PANE_SOURCE,
    /disabled:\s*secondarySwitchDisabled/
  );
});

test("PreviewPane clears local hover and geyser selection state when active target changes", () => {
  assert.match(PREVIEW_PANE_SOURCE, /useEffect\(\(\)\s*=>\s*\{[\s\S]*setHoveredRegion\(null\);/s);
  assert.match(PREVIEW_PANE_SOURCE, /setSelectedRegion\(null\);/);
  assert.match(PREVIEW_PANE_SOURCE, /setHoverGeyserIndex\(null\);/);
  assert.match(PREVIEW_PANE_SOURCE, /setSelectedGeyserIndex\(null\);/);
  assert.match(PREVIEW_PANE_SOURCE, /setSelectedGeyserAnchor\(null\);/);
  assert.match(PREVIEW_PANE_SOURCE, /setShowGeyserList\(false\);/);
  assert.match(PREVIEW_PANE_SOURCE, /\},\s*\[activeTarget\]\);/);
});
