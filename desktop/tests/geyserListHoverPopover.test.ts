import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

import { resolveGeyserListHoverPopoverWidth } from "../src/features/preview/GeyserListHoverPopover.tsx";

const GEYSER_LIST_HOVER_POPOVER_SOURCE = readFileSync(
  new URL("../src/features/preview/GeyserListHoverPopover.tsx", import.meta.url),
  "utf8"
);
const APP_CSS = readFileSync(new URL("../src/app/app.css", import.meta.url), "utf8");
const GEYSER_LIST_OVERLAY_SOURCE = readFileSync(
  new URL("../src/features/preview/GeyserListOverlay.tsx", import.meta.url),
  "utf8"
);

test("GeyserListHoverPopover constrains overlay width to popup container width", () => {
  assert.equal(resolveGeyserListHoverPopoverWidth(null), 220);
  assert.equal(resolveGeyserListHoverPopoverWidth({ clientWidth: 640 } as HTMLElement), 220);
  assert.equal(resolveGeyserListHoverPopoverWidth({ clientWidth: 280 } as HTMLElement), 220);
  assert.equal(resolveGeyserListHoverPopoverWidth({ clientWidth: 180 } as HTMLElement), 156);
});

test("GeyserListHoverPopover source keeps right-side click popover with compact four-field content", () => {
  assert.match(GEYSER_LIST_HOVER_POPOVER_SOURCE, /import\s*\{\s*Popover,\s*Typography\s*\}\s*from "antd"/);
  assert.match(GEYSER_LIST_HOVER_POPOVER_SOURCE, /placement="right"/);
  assert.match(GEYSER_LIST_HOVER_POPOVER_SOURCE, /trigger="click"|trigger=\{\["click"\]\}/);
  assert.match(GEYSER_LIST_HOVER_POPOVER_SOURCE, /open=\{open\}/);
  assert.match(GEYSER_LIST_HOVER_POPOVER_SOURCE, /onOpenChange=\{onOpenChange\}/);
  assert.doesNotMatch(GEYSER_LIST_HOVER_POPOVER_SOURCE, /mouseEnterDelay=/);
  assert.doesNotMatch(GEYSER_LIST_HOVER_POPOVER_SOURCE, /mouseLeaveDelay=/);
  assert.match(GEYSER_LIST_HOVER_POPOVER_SOURCE, /formatGeyserDetailEruptionRate\(geyserDetail\)/);
  assert.match(GEYSER_LIST_HOVER_POPOVER_SOURCE, /formatGeyserDetailAverageYield\(geyserDetail\)/);
  assert.match(GEYSER_LIST_HOVER_POPOVER_SOURCE, /formatGeyserDetailEruptionWindow\(geyserDetail\)/);
  assert.match(GEYSER_LIST_HOVER_POPOVER_SOURCE, /formatGeyserDetailActiveWindow\(geyserDetail\)/);
  assert.doesNotMatch(GEYSER_LIST_HOVER_POPOVER_SOURCE, /formatGeyserDetailTemperature/);
  assert.doesNotMatch(GEYSER_LIST_HOVER_POPOVER_SOURCE, /formatGeyserDetailTitle/);
  assert.doesNotMatch(GEYSER_LIST_HOVER_POPOVER_SOURCE, /formatGeyserDetailCoords/);
  assert.doesNotMatch(GEYSER_LIST_HOVER_POPOVER_SOURCE, /title=\{/);
  assert.doesNotMatch(GEYSER_LIST_HOVER_POPOVER_SOURCE, />\s*关闭\s*</);
  assert.doesNotMatch(GEYSER_LIST_HOVER_POPOVER_SOURCE, />\s*重试\s*</);
});

test("GeyserListOverlay source only enables click popover for ready parameter details and tracks active item", () => {
  assert.match(GEYSER_LIST_OVERLAY_SOURCE, /geyserDetailsStatus === "ready"/);
  assert.match(GEYSER_LIST_OVERLAY_SOURCE, /detail && detail\.hasParameters/);
  assert.match(GEYSER_LIST_OVERLAY_SOURCE, /<GeyserListHoverPopover/);
  assert.match(GEYSER_LIST_OVERLAY_SOURCE, /const \[activePopoverKey, setActivePopoverKey\] = useState<string \| null>\(null\)/);
  assert.match(GEYSER_LIST_OVERLAY_SOURCE, /open=\{isActive\}/);
  assert.match(GEYSER_LIST_OVERLAY_SOURCE, /onOpenChange=\{\(open\) =>/);
  assert.match(GEYSER_LIST_OVERLAY_SOURCE, /geyser-overlay-item-clickable/);
  assert.match(GEYSER_LIST_OVERLAY_SOURCE, /geyser-overlay-item-active/);
});

test("GeyserListHoverPopover styles keep compact width and height limits", () => {
  assert.match(
    APP_CSS,
    /\.geyser-list-hover-popover-overlay \.ant-popover-inner\s*\{[\s\S]*width:\s*100%;[\s\S]*max-width:\s*100%;[\s\S]*box-sizing:\s*border-box;[\s\S]*\}/
  );
  assert.doesNotMatch(APP_CSS, /\.geyser-list-hover-popover-overlay \.ant-popover-title\s*\{/);
  assert.doesNotMatch(
    APP_CSS,
    /\.geyser-list-hover-popover-overlay \.ant-popover-arrow\s*\{[\s\S]*display:\s*none;[\s\S]*\}/
  );
  assert.match(
    APP_CSS,
    /\.geyser-overlay-item-clickable\s*\{[\s\S]*cursor:\s*pointer;[\s\S]*\}/
  );
  assert.match(
    APP_CSS,
    /\.geyser-overlay-item-active\s*\{[\s\S]*border-left:\s*2px solid var\(--line-active\);[\s\S]*\}/
  );
  assert.match(
    APP_CSS,
    /\.geyser-list-hover-popover\s*\{[\s\S]*max-height:\s*180px;[\s\S]*overflow:\s*auto;[\s\S]*padding:\s*8px 10px;[\s\S]*\}/
  );
});
