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

function renderSearchActions(): string {
  return renderToStaticMarkup(
    createElement(SearchActions, {
      isSearching: false,
      isCancelling: false,
      onCancel: () => undefined,
      onClear: () => undefined,
      onCopy: () => undefined,
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
