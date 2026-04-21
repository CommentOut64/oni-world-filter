import test from "node:test";
import assert from "node:assert/strict";

import type {
  GeyserOption,
  MixingSlotMeta,
  SearchAnalysisPayload,
  ValidationIssue,
} from "../src/lib/contracts.ts";
import type { SearchDraft } from "../src/state/searchStore.ts";
import { formatAnalysisErrorMessage } from "../src/features/search/searchAnalysisDisplay.ts";
import { encodeMixingFromLevels, MIXING_SLOT_COUNT } from "../src/features/search/searchSchema.ts";

const geysers: GeyserOption[] = [{ id: 2, key: "hot_water" }];

const mixingSlots: MixingSlotMeta[] = [
  { slot: 0, path: "DLC2_ID", type: "dlc", name: "The Frosty Planet Pack", description: "" },
  {
    slot: 1,
    path: "dlc2::worldMixing/CeresMixingSettings",
    type: "world",
    name: "Ceres Fragment",
    description: "",
  },
];

const draft: SearchDraft = {
  worldType: 13,
  seedStart: 100000,
  seedEnd: 120000,
  mixing: encodeMixingFromLevels([
    1,
    1,
    ...new Array<number>(MIXING_SLOT_COUNT - 2).fill(0),
  ]),
  threads: 0,
  cpu: {
    mode: "balanced",
    workers: 0,
    allowSmt: true,
    allowLowPerf: false,
    placement: "preferred",
    enableWarmup: false,
    enableAdaptiveDown: true,
    chunkSize: 64,
    progressInterval: 1000,
    sampleWindowMs: 2000,
    adaptiveMinWorkers: 1,
    adaptiveDropThreshold: 0.12,
    adaptiveDropWindows: 3,
    adaptiveCooldownMs: 8000,
  },
  constraints: {
    required: [],
    forbidden: [],
    distance: [],
    count: [],
  },
};

function createAnalysis(issue: ValidationIssue): SearchAnalysisPayload {
  return {
    worldProfile: {
      valid: true,
      worldType: 13,
      worldCode: "V-SNDST-C-",
      width: 240,
      height: 380,
      diagonal: 449,
      activeMixingSlots: [],
      disabledMixingSlots: [0, 1],
      possibleGeyserTypes: [],
      impossibleGeyserTypes: [],
      possibleMaxCountByType: {},
      genericTypeUpperById: {},
      genericSlotUpper: 0,
      exactSourceSummary: [],
      genericSourceSummary: [],
      sourcePools: [],
      spatialEnvelopes: [],
    },
    normalizedRequest: {
      worldType: 13,
      seedStart: 100000,
      seedEnd: 120000,
      mixing: draft.mixing,
      threads: 0,
      groups: [],
    },
    errors: [issue],
    warnings: [],
    bottlenecks: [],
    predictedBottleneckProbability: 1,
  };
}

test("formatAnalysisErrorMessage maps disabled mixing slots to frontend names", () => {
  const issue: ValidationIssue = {
    layer: "layer2",
    code: "world.disabled_mixing_slot_enabled",
    field: "mixing",
    message: "当前 worldType 禁用了 mixing slot: 0, 1",
  };

  const message = formatAnalysisErrorMessage(issue, createAnalysis(issue), draft, mixingSlots, geysers);

  assert.equal(message, "当前世界不允许启用：寒霜行星包、谷神星碎片。请先关闭这些选项，再开始搜索。");
});

test("formatAnalysisErrorMessage maps geyser ids to Chinese names", () => {
  const issue: ValidationIssue = {
    layer: "layer2",
    code: "world.geyser_impossible",
    field: "constraints",
    message: "当前 worldType + mixing 下 geyser 不可能出现: hot_water",
  };

  const message = formatAnalysisErrorMessage(issue, createAnalysis(issue), draft, mixingSlots, geysers);

  assert.equal(message, "当前 worldType + mixing 下 geyser 不可能出现: 清水泉");
});
