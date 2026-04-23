import test from "node:test";
import assert from "node:assert/strict";

import type { SearchAnalysisPayload, ValidationIssue } from "../src/lib/contracts.ts";
import {
  isBlockingSearchWarning,
  shouldShowSearchWarningConfirmation,
} from "../src/features/search/searchWarningPolicy.ts";

function createWarning(code: string): ValidationIssue {
  return {
    layer: "layer4",
    code,
    field: "constraints",
    message: code,
  };
}

function createAnalysis(
  probability: number,
  warnings: ValidationIssue[]
): SearchAnalysisPayload {
  return {
    worldProfile: {
      valid: true,
      worldType: 13,
      worldCode: "V-SNDST-C-",
      width: 240,
      height: 380,
      diagonal: 449,
      activeMixingSlots: [],
      disabledMixingSlots: [],
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
      seedStart: 0,
      seedEnd: 0,
      mixing: 0,
      groups: [],
    },
    errors: [],
    warnings,
    bottlenecks: [],
    predictedBottleneckProbability: probability,
  };
}

test("low probability warnings should require confirmation", () => {
  assert.equal(isBlockingSearchWarning(createWarning("predict.low_probability.warning")), true);
  assert.equal(isBlockingSearchWarning(createWarning("predict.low_probability.strong-warning")), true);
});

test("generic capacity pruning should require confirmation", () => {
  assert.equal(isBlockingSearchWarning(createWarning("predict.generic_capacity_pruned")), true);
});

test("dependency fallback warning should not require confirmation", () => {
  const analysis = createAnalysis(0.29006, [createWarning("predict.dependency_fallback_min")]);

  assert.equal(shouldShowSearchWarningConfirmation(analysis), false);
});

test("low probability warning should only confirm below threshold", () => {
  const lowProbabilityAnalysis = createAnalysis(0.049, [
    createWarning("predict.low_probability.warning"),
  ]);
  const contradictoryAnalysis = createAnalysis(0.29006, [
    createWarning("predict.low_probability.warning"),
  ]);

  assert.equal(shouldShowSearchWarningConfirmation(lowProbabilityAnalysis), true);
  assert.equal(shouldShowSearchWarningConfirmation(contradictoryAnalysis), false);
});

test("generic capacity pruning should still confirm regardless of probability", () => {
  const analysis = createAnalysis(0.29006, [
    createWarning("predict.generic_capacity_pruned"),
  ]);

  assert.equal(shouldShowSearchWarningConfirmation(analysis), true);
});
