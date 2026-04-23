import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

import {
  createDesktopTheme,
  getPreferredDesktopThemeMode,
  syncDesktopThemeMode,
} from "../src/app/antdTheme.ts";

const APP_CSS = readFileSync(new URL("../src/app/app.css", import.meta.url), "utf8");

test("createDesktopTheme exposes the unified dark token palette", () => {
  const desktopTheme = createDesktopTheme("dark");

  assert.equal(desktopTheme.token?.colorBgBase, "#08111b");
  assert.equal(desktopTheme.token?.colorBgLayout, "#0b1624");
  assert.equal(desktopTheme.token?.colorPrimary, "#3b82f6");
  assert.equal(desktopTheme.token?.colorText, "#e7f0fa");
  assert.equal(desktopTheme.token?.colorError, "#f87171");
});

test("createDesktopTheme exposes a light palette backed by antd default algorithm", () => {
  const desktopTheme = createDesktopTheme("light");

  assert.equal(desktopTheme.token?.colorBgBase, "#f5f5f7");
  assert.equal(desktopTheme.token?.colorBgLayout, "#ececef");
  assert.equal(desktopTheme.token?.colorText, "#18181b");
  assert.equal(desktopTheme.token?.colorBorder, "#d4d4d8");
});

test("getPreferredDesktopThemeMode falls back to dark when matchMedia is unavailable", () => {
  assert.equal(getPreferredDesktopThemeMode(undefined), "dark");
});

test("getPreferredDesktopThemeMode follows system preference when available", () => {
  assert.equal(getPreferredDesktopThemeMode(() => ({ matches: false } as MediaQueryList)), "light");
  assert.equal(getPreferredDesktopThemeMode(() => ({ matches: true } as MediaQueryList)), "dark");
});

test("syncDesktopThemeMode writes data-theme on the document element", () => {
  const calls: Array<[string, string]> = [];

  syncDesktopThemeMode("light", {
    setAttribute: (name: string, value: string) => {
      calls.push([name, value]);
    },
  } as Pick<HTMLElement, "setAttribute">);

  assert.deepEqual(calls, [["data-theme", "light"]]);
});

test("app.css defines both light and dark css variable scopes", () => {
  assert.match(APP_CSS, /html\[data-theme="dark"\]\s*\{/);
  assert.match(APP_CSS, /html\[data-theme="light"\]\s*\{/);
});
