import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createElement, type ReactNode } from "react";
import { renderToStaticMarkup } from "react-dom/server";
import { FormProvider, useForm } from "react-hook-form";

import CountRuleEditor from "../src/features/search/CountRuleEditor.tsx";
import DistanceRuleEditor from "../src/features/search/DistanceRuleEditor.tsx";
import GeyserConstraintEditor from "../src/features/search/GeyserConstraintEditor.tsx";
import MixingSelector from "../src/features/search/MixingSelector.tsx";
import SearchActions from "../src/features/search/SearchActions.tsx";
import WorldSelector from "../src/features/search/WorldSelector.tsx";
import { encodeMixingFromLevels, MIXING_SLOT_COUNT } from "../src/features/search/searchSchema.ts";

const GEYSERS = [
  { id: 1, key: "steam", name: "Steam" },
  { id: 2, key: "cool_steam", name: "Cool Steam" },
  { id: 3, key: "salt_water", name: "Salt Water" },
];

const MIXING_SLOTS = [
  { slot: 0, path: "DLC2_ID", name: "Spaced Out", description: "DLC2_ID" },
  {
    slot: 1,
    path: "dlc2::worldMixing/forest",
    name: "Forest",
    description: "STRINGS.SUBWORLDS.GARDEN.DESC",
  },
  { slot: 2, path: "DLC3_ID", name: "Frosty", description: "" },
  {
    slot: 3,
    path: "dlc3::worldMixing/radioactive",
    name: "Radioactive",
    description: "STRINGS.SUBWORLDS.RADIOACTIVE.DESC",
  },
];

function renderSearchActions(
  overrides?: Partial<{
    isSearching: boolean;
    isBusy: boolean;
    hasResults: boolean;
    resultsCount: number;
  }>
): string {
  return renderToStaticMarkup(
    createElement(SearchActions, {
      isSearching: false,
      isBusy: false,
      hasResults: true,
      resultsCount: 3,
      onViewResults: () => undefined,
      ...overrides,
    })
  );
}

const APP_CSS = readFileSync(new URL("../src/app/app.css", import.meta.url), "utf8");

function SearchFormHarness({
  children,
  defaultValues,
}: {
  children: ReactNode;
  defaultValues?: Partial<{
    worldType: number;
    mixing: number;
    seedStart: number;
    seedEnd: number;
    cpuMode: "balanced" | "turbo";
    cpuAllowSmt: boolean;
    cpuAllowLowPerf: boolean;
    required: Array<{ geyser: string }>;
    forbidden: Array<{ geyser: string }>;
    distance: Array<{ geyser: string; minDist: number; maxDist: number }>;
    count: Array<{ geyser: string; minCount: number; maxCount: number }>;
  }>;
}) {
  const methods = useForm({
    defaultValues: {
      worldType: 13,
      mixing: 0,
      seedStart: 0,
      seedEnd: 0,
      cpuMode: "balanced",
      cpuAllowSmt: true,
      cpuAllowLowPerf: false,
      required: [],
      forbidden: [],
      distance: [],
      count: [],
      ...defaultValues,
    },
  });

  return createElement(FormProvider, { ...methods }, children);
}

function WorldSelectorHarness() {
  return createElement(
    SearchFormHarness,
    null,
    createElement(WorldSelector, {
      worlds: [{ id: 13, code: "SNDST-A-" }],
    } as never)
  );
}

function MixingSelectorHarness() {
  return createElement(
    SearchFormHarness,
    null,
    createElement(MixingSelector, {
      mixingSlots: MIXING_SLOTS,
      disabledMixingSlots: new Set([3]),
    })
  );
}

function MixingSelectorEnabledHarness() {
  const levels = new Array(MIXING_SLOT_COUNT).fill(0);
  levels[0] = 1;
  levels[1] = 2;

  return createElement(
    SearchFormHarness,
    {
      defaultValues: {
        mixing: encodeMixingFromLevels(levels),
      },
    },
    createElement(MixingSelector, {
      mixingSlots: MIXING_SLOTS,
      disabledMixingSlots: new Set([3]),
    })
  );
}

function GeyserConstraintHarness() {
  return createElement(
    SearchFormHarness,
    {
      defaultValues: {
        required: [{ geyser: "steam" }],
      },
    },
    createElement(GeyserConstraintEditor, {
      title: "必须包含(required)",
      type: "required",
      geysers: GEYSERS,
    })
  );
}

function DistanceRuleHarness() {
  return createElement(
    SearchFormHarness,
    {
      defaultValues: {
        distance: [{ geyser: "steam", minDist: 0, maxDist: 80 }],
      },
    },
    createElement(DistanceRuleEditor, {
      geysers: GEYSERS,
    })
  );
}

function CountRuleHarness() {
  return createElement(
    SearchFormHarness,
    {
      defaultValues: {
        count: [{ geyser: "steam", minCount: 0, maxCount: 1 }],
      },
    },
    createElement(CountRuleEditor, {
      geysers: GEYSERS,
    })
  );
}

test("SearchActions renders antd buttons", () => {
  const markup = renderSearchActions();

  assert.match(markup, /ant-btn/);
  assert.match(markup, /开始搜索/);
  assert.match(markup, /查看结果/);
  assert.match(markup, /search-actions-row/);
  assert.match(markup, /search-action-primary/);
  assert.match(markup, /search-action-secondary/);
  assert.doesNotMatch(markup, /ant-space-compact/);
  assert.doesNotMatch(markup, /取消搜索/);
  assert.doesNotMatch(markup, /清空结果/);
  assert.doesNotMatch(markup, /复制协议 JSON/);
});

test("SearchActions shows analyze busy state before search starts", () => {
  const markup = renderSearchActions({
    isSearching: false,
    isBusy: true,
  });

  assert.match(markup, /正在分析/);
  assert.match(markup, /ant-btn-loading/);
  assert.match(markup, /disabled/);
});

test("WorldSelector renders antd segmented navigation", () => {
  const markup = renderToStaticMarkup(createElement(WorldSelectorHarness));

  assert.match(markup, /ant-segmented/);
  assert.match(markup, /world-selector-row/);
  assert.match(markup, /world-selector-field/);
});

test("Search section title clears default heading margins", () => {
  assert.match(
    APP_CSS,
    /\.search-section-header \.ant-typography\s*\{\s*margin:\s*0;\s*\}/
  );
  assert.match(
    APP_CSS,
    /\.mixing-package-header \.ant-typography,\s*\.advanced-debug-panel \.ant-typography\s*\{\s*margin:\s*0;\s*\}/
  );
  assert.match(
    APP_CSS,
    /\.host-debug-header \.ant-typography\s*\{\s*margin:\s*0;\s*\}/
  );
  assert.match(
    APP_CSS,
    /\.search-section-rules\s*\{\s*min-height:\s*0;\s*display:\s*flex;\s*flex-direction:\s*column;\s*flex:\s*1\s+1\s+auto;\s*overflow:\s*hidden;\s*\}/
  );
  assert.match(
    APP_CSS,
    /\.search-section-rules\.ant-card \.ant-card-body\s*\{\s*padding:\s*14px;\s*min-height:\s*0;\s*display:\s*flex;\s*flex-direction:\s*column;\s*overflow:\s*hidden;\s*\}/
  );
  assert.match(
    APP_CSS,
    /\.search-rule-grid\s*\{\s*min-height:\s*0;\s*display:\s*grid;\s*grid-template-columns:\s*repeat\(2,\s*minmax\(0,\s*1fr\)\);\s*gap:\s*12px;\s*align-content:\s*start;\s*flex:\s*1\s+1\s+auto;\s*overflow:\s*auto;\s*padding-right:\s*4px;\s*padding-bottom:\s*88px;\s*scroll-padding-bottom:\s*88px;\s*\}/
  );
  assert.match(
    APP_CSS,
    /\.search-actions\s*\{\s*position:\s*absolute;\s*left:\s*16px;\s*bottom:\s*16px;\s*z-index:\s*10;\s*width:\s*calc\(100%\s*-\s*32px\);\s*\}/
  );
});

test("MixingSelector renders antd grouping shell", () => {
  const markup = renderToStaticMarkup(createElement(MixingSelectorHarness));

  assert.match(markup, /ant-card/);
  assert.match(markup, /ant-collapse/);
  assert.match(markup, /mixing-package-stack/);
  assert.match(markup, /mixing-package-card-body/);
  assert.match(markup, /mixing-package-title-row/);
  assert.doesNotMatch(markup, /DLC2_ID/);
  assert.doesNotMatch(markup, /STRINGS\.SUBWORLDS\.GARDEN\.DESC/);
  assert.doesNotMatch(markup, /STRINGS\.SUBWORLDS\.RADIOACTIVE\.DESC/);
  assert.doesNotMatch(markup, /\[\d+\]\s*(森林|Forest|冰窟生态|Ice Cave Biome|放射生态|Radioactive)/);
});

test("MixingSelector keeps mode selector on biome rows but not DLC package rows", () => {
  const markup = renderToStaticMarkup(createElement(MixingSelectorEnabledHarness));

  assert.match(markup, /寒霜行星包/);
  assert.match(markup, /Forest/);
  assert.match(markup, /title="确保"/);
  assert.doesNotMatch(markup, /寒霜行星包<\/span><\/label><div class="ant-select[^"]*"/);
});

test("GeyserConstraintEditor renders antd controls", () => {
  const markup = renderToStaticMarkup(createElement(GeyserConstraintHarness));

  assert.match(markup, /ant-select/);
  assert.match(markup, /ant-btn/);
});

test("DistanceRuleEditor renders antd numeric inputs", () => {
  const markup = renderToStaticMarkup(createElement(DistanceRuleHarness));

  assert.match(markup, /ant-input-number/);
  assert.match(markup, /ant-btn/);
});

test("CountRuleEditor renders antd numeric inputs", () => {
  const markup = renderToStaticMarkup(createElement(CountRuleHarness));

  assert.match(markup, /ant-input-number/);
  assert.match(markup, /ant-btn/);
});
