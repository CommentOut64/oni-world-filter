import test from "node:test";
import assert from "node:assert/strict";

import { desktopTheme } from "../src/app/antdTheme.ts";

test("desktopTheme exposes the unified dark token palette", () => {
  assert.equal(desktopTheme.token?.colorBgBase, "#08111b");
  assert.equal(desktopTheme.token?.colorBgLayout, "#0b1624");
  assert.equal(desktopTheme.token?.colorPrimary, "#3b82f6");
  assert.equal(desktopTheme.token?.colorText, "#e7f0fa");
  assert.equal(desktopTheme.token?.colorError, "#f87171");
});
