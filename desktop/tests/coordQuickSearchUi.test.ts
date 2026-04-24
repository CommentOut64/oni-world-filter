import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createElement } from "react";
import { renderToStaticMarkup } from "react-dom/server";

import CoordQuickSearch from "../src/features/search/CoordQuickSearch.tsx";
import SearchActions from "../src/features/search/SearchActions.tsx";

const APP_CSS = readFileSync(new URL("../src/app/app.css", import.meta.url), "utf8");

test("CoordQuickSearch renders a standalone button search bar for standard coord input", () => {
  const markup = renderToStaticMarkup(
    createElement(CoordQuickSearch, {
      value: "V-SNDST-C-123456-0-D3-HD",
      loading: false,
      disabled: false,
      onChange: () => undefined,
      onSubmit: () => undefined,
    })
  );

  assert.match(markup, /标准坐标码/);
  assert.match(markup, /搜\s*索/);
  assert.match(markup, /type="button"/);
  assert.match(markup, /coord-quick-search/);
  assert.doesNotMatch(markup, /type="submit"/);
});

test("CoordQuickSearch disables both input and button when busy", () => {
  const markup = renderToStaticMarkup(
    createElement(CoordQuickSearch, {
      value: "V-SNDST-C-123456-0-D3-HD",
      loading: true,
      disabled: true,
      onChange: () => undefined,
      onSubmit: () => undefined,
    })
  );

  assert.match(markup, /disabled=""/);
  assert.match(markup, /coord-quick-search-input/);
  assert.match(markup, /coord-quick-search-button/);
});

test("SearchActions disables batch search submit when an external busy state is active", () => {
  const markup = renderToStaticMarkup(
    createElement(SearchActions, {
      isSearching: false,
      isBusy: true,
      hasResults: true,
      resultsCount: 3,
      onViewResults: () => undefined,
    })
  );

  assert.match(markup, /开始搜索/);
  assert.match(markup, /type="submit"/);
  assert.match(markup, /disabled=""/);
});

test("search header CSS exposes dedicated quick-search layout hooks", () => {
  assert.match(APP_CSS, /\.search-panel-header-center\s*\{/);
  assert.match(APP_CSS, /\.search-panel-title\s*\{/);
  assert.match(APP_CSS, /\.coord-quick-search\s*\{/);
  assert.match(APP_CSS, /\.coord-quick-search-input\s*\{/);
  assert.match(APP_CSS, /\.coord-quick-search-button\s*\{/);
});

test("search header centers the quick-search column with a bounded width", () => {
  const rowRule = APP_CSS.match(/\.search-panel-header-row\s*\{[^}]*\}/)?.[0] ?? "";
  const centerRule = APP_CSS.match(/\.search-panel-header-center\s*\{[^}]*\}/)?.[0] ?? "";
  const themeToggleRule = APP_CSS.match(/\.theme-toggle\s*\{[^}]*\}/)?.[0] ?? "";

  assert.match(rowRule, /display:\s*grid;/);
  assert.match(rowRule, /grid-template-columns:\s*auto minmax\(360px,\s*520px\) auto;/);
  assert.match(centerRule, /justify-self:\s*center;/);
  assert.match(centerRule, /width:\s*100%;/);
  assert.match(themeToggleRule, /justify-self:\s*end;/);
  assert.match(themeToggleRule, /width:\s*max-content;/);
});

test("search panel form preserves inner scroll layout after adding coord quick search", () => {
  const formRule = APP_CSS.match(/\.search-panel-form\s*\{[^}]*\}/)?.[0] ?? "";
  const gridRule = APP_CSS.match(/\.search-panel-grid\s*\{[^}]*\}/)?.[0] ?? "";

  assert.match(formRule, /display:\s*flex;/);
  assert.match(formRule, /flex-direction:\s*column;/);
  assert.match(formRule, /min-height:\s*0;/);
  assert.match(formRule, /overflow:\s*hidden;/);

  assert.match(gridRule, /flex:\s*1\s+1\s+auto;/);
  assert.match(gridRule, /min-height:\s*0;/);
  assert.match(gridRule, /display:\s*grid;/);
});
