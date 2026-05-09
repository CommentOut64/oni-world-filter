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
  cpu: {
    mode: "balanced",
    allowSmt: true,
    allowLowPerf: false,
    placement: "preferred",
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

  assert.equal(message, "当前世界不会生成这个喷口：清水泉");
});

test("formatAnalysisErrorMessage rewrites required with count conflict into user copy", () => {
  const issue: ValidationIssue = {
    layer: "layer3",
    code: "conflict.required_with_count",
    field: "constraints.required/constraints.count",
    message: "同一 geyser 不能同时设置 required 和 count: hot_water",
  };

  const message = formatAnalysisErrorMessage(issue, createAnalysis(issue), draft, mixingSlots, geysers);

  assert.equal(message, "这个喷口已经在“必须包含”里设置了数量，不要再单独添加旧的包含条件：清水泉");
});

test("formatAnalysisErrorMessage rewrites required with distance conflict into user copy", () => {
  const issue: ValidationIssue = {
    layer: "layer3",
    code: "conflict.required_with_distance",
    field: "constraints.required/constraints.distance",
    message: "同一 geyser 不能同时设置 required 和 distance: hot_water",
  };

  const message = formatAnalysisErrorMessage(issue, createAnalysis(issue), draft, mixingSlots, geysers);

  assert.equal(message, "这个喷口已经设置了距离规则，不要再单独添加“必须包含”：清水泉");
});

test("formatAnalysisErrorMessage rewrites forbidden with count conflict into user copy", () => {
  const issue: ValidationIssue = {
    layer: "layer3",
    code: "conflict.forbidden_with_count",
    field: "constraints.forbidden/constraints.count",
    message: "同一 geyser 不能同时设置 forbidden 和 count: hot_water",
  };

  const message = formatAnalysisErrorMessage(issue, createAnalysis(issue), draft, mixingSlots, geysers);

  assert.equal(message, "这个喷口已经设置了“必须排除”，不能再设置“必须包含”：清水泉");
});

test("formatAnalysisErrorMessage rewrites impossible forbidden warning into user copy", () => {
  const issue: ValidationIssue = {
    layer: "layer2",
    code: "world.forbidden_geyser_already_impossible",
    field: "constraints.forbidden",
    message: "当前 worldType + mixing 下 geyser 已天然不可能出现: hot_water",
  };

  const message = formatAnalysisErrorMessage(issue, createAnalysis(issue), draft, mixingSlots, geysers);

  assert.equal(message, "当前世界已经天然排除了这个喷口：清水泉。相关“必须排除”条件可以考虑删除。");
});

test("formatAnalysisErrorMessage rewrites required and forbidden conflict into user copy", () => {
  const issue: ValidationIssue = {
    layer: "layer3",
    code: "conflict.required_forbidden",
    field: "constraints.required/constraints.forbidden",
    message: "同一 geyser 不能同时 required 和 forbidden: hot_water",
  };

  const message = formatAnalysisErrorMessage(issue, createAnalysis(issue), draft, mixingSlots, geysers);

  assert.equal(message, "同一个喷口不能同时设置“必须包含”和“必须排除”：清水泉");
});

test("formatAnalysisErrorMessage rewrites forbidden with distance conflict into user copy", () => {
  const issue: ValidationIssue = {
    layer: "layer3",
    code: "conflict.forbidden_with_distance",
    field: "constraints.distance",
    message: "已被排空的 geyser 不能同时设置 distance: hot_water",
  };

  const message = formatAnalysisErrorMessage(issue, createAnalysis(issue), draft, mixingSlots, geysers);

  assert.equal(message, "这个喷口已经设置了“必须排除”，不能再设置距离规则：清水泉");
});

test("formatAnalysisErrorMessage rewrites world count upper bound issue into user copy", () => {
  const issue: ValidationIssue = {
    layer: "layer2",
    code: "world.count_max_gt_possible_max",
    field: "constraints.count",
    message: "count.maxCount 超过当前世界上界: hot_water",
  };

  const message = formatAnalysisErrorMessage(issue, createAnalysis(issue), draft, mixingSlots, geysers);

  assert.equal(message, "这个喷口在当前世界里达不到你设置的“必须包含”数量上限：清水泉");
});

test("formatAnalysisErrorMessage rewrites count range issue into user copy", () => {
  const issue: ValidationIssue = {
    layer: "layer1",
    code: "range.count_min_gt_max",
    field: "constraints.count[0].maxCount",
    message: "count.minCount 必须 <= count.maxCount",
  };

  const message = formatAnalysisErrorMessage(issue, createAnalysis(issue), draft, mixingSlots, geysers);

  assert.equal(message, "必须包含的最大数量不能小于最小数量。");
});
