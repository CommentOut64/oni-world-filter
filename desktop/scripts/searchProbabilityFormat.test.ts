import test from "node:test";
import assert from "node:assert/strict";

import {
  formatProbabilityUpper,
  formatSearchWarningProbabilityCopy,
} from "../src/features/search/searchProbabilityFormat.ts";

test("formatProbabilityUpper trims redundant percent decimals", () => {
  assert.equal(formatProbabilityUpper(1), "100%");
  assert.equal(formatProbabilityUpper(0.125), "12.5%");
  assert.equal(formatProbabilityUpper(0.00005), "< 0.01%");
});

test("formatSearchWarningProbabilityCopy uses upper-bound wording", () => {
  assert.equal(formatSearchWarningProbabilityCopy(1), "当前瓶颈概率上界为 100%。");
});
