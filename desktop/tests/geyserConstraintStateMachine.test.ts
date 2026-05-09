import test from "node:test";
import assert from "node:assert/strict";

import {
  buildGeyserConstraintState,
  getConstraintStateForGeyser,
} from "../src/features/search/geyserConstraintStateMachine.ts";
import { COUNT_MAX_SENTINEL } from "../src/features/search/searchSchema.ts";

function buildEmptyInput() {
  return {
    required: [] as { geyser: string }[],
    forbidden: [] as { geyser: string }[],
    distance: [] as { geyser: string; minDist: number; maxDist: number }[],
    count: [] as { geyser: string; minCount: number; maxCount: number }[],
  };
}

test("state machine resolves the five single-section states and the count plus distance state", () => {
  const state = buildGeyserConstraintState({
    required: [{ geyser: "steam" }],
    forbidden: [{ geyser: "chlorine_gas" }],
    count: [{ geyser: "hot_water", minCount: 1, maxCount: 2 }],
    distance: [
      { geyser: "slush_water", minDist: 10, maxDist: 50 },
      { geyser: "salt_water", minDist: 20, maxDist: 80 },
    ],
  });

  assert.equal(getConstraintStateForGeyser(state, "steam")?.state, "required_only");
  assert.equal(getConstraintStateForGeyser(state, "chlorine_gas")?.state, "forbidden_only");
  assert.equal(getConstraintStateForGeyser(state, "hot_water")?.state, "count_only");
  assert.equal(getConstraintStateForGeyser(state, "slush_water")?.state, "distance_only");
  assert.equal(getConstraintStateForGeyser(state, "salt_water")?.state, "distance_only");

  const combined = buildGeyserConstraintState({
    ...buildEmptyInput(),
    count: [{ geyser: "steam", minCount: 1, maxCount: 2 }],
    distance: [{ geyser: "steam", minDist: 0, maxDist: 80 }],
  });

  assert.equal(getConstraintStateForGeyser(combined, "steam")?.state, "count_with_distance");
});

test("state machine reports required plus count as an invalid legacy combination", () => {
  const state = buildGeyserConstraintState({
    ...buildEmptyInput(),
    required: [{ geyser: "steam" }],
    count: [{ geyser: "steam", minCount: 1, maxCount: 2 }],
  });

  const group = getConstraintStateForGeyser(state, "steam");
  assert.equal(group?.state, "invalid_legacy");
  assert.deepEqual(
    group?.issues.map((item) => item.code),
    ["conflict.required_with_count"]
  );
});

test("state machine reports required plus forbidden as an invalid legacy combination", () => {
  const state = buildGeyserConstraintState({
    ...buildEmptyInput(),
    required: [{ geyser: "steam" }],
    forbidden: [{ geyser: "steam" }],
  });

  const group = getConstraintStateForGeyser(state, "steam");
  assert.equal(group?.state, "invalid_legacy");
  assert.deepEqual(
    group?.issues.map((item) => item.code),
    ["conflict.required_forbidden"]
  );
});

test("state machine reports forbidden plus distance as an invalid legacy combination", () => {
  const state = buildGeyserConstraintState({
    ...buildEmptyInput(),
    forbidden: [{ geyser: "steam" }],
    distance: [{ geyser: "steam", minDist: 0, maxDist: 80 }],
  });

  const group = getConstraintStateForGeyser(state, "steam");
  assert.equal(group?.state, "invalid_legacy");
  assert.deepEqual(
    group?.issues.map((item) => item.code),
    ["conflict.forbidden_with_distance"]
  );
});

test("state machine reports zero-count rules as invalid legacy input", () => {
  const zeroLowerBoundState = buildGeyserConstraintState({
    ...buildEmptyInput(),
    count: [{ geyser: "steam", minCount: 0, maxCount: 2 }],
  });

  const zeroLowerBoundGroup = getConstraintStateForGeyser(zeroLowerBoundState, "steam");
  assert.equal(zeroLowerBoundGroup?.state, "invalid_legacy");
  assert.deepEqual(
    zeroLowerBoundGroup?.issues.map((item) => item.code),
    ["range.count_zero_not_allowed"]
  );

  const zeroUpperBoundState = buildGeyserConstraintState({
    ...buildEmptyInput(),
    count: [{ geyser: "steam", minCount: 1, maxCount: 0 }],
  });

  const zeroUpperBoundGroup = getConstraintStateForGeyser(zeroUpperBoundState, "steam");
  assert.equal(zeroUpperBoundGroup?.state, "invalid_legacy");
  assert.deepEqual(
    zeroUpperBoundGroup?.issues.map((item) => item.code),
    ["range.count_zero_not_allowed"]
  );
});

test("state machine accepts Max as a legal upper bound for must-include count", () => {
  const state = buildGeyserConstraintState({
    ...buildEmptyInput(),
    count: [{ geyser: "steam", minCount: 1, maxCount: COUNT_MAX_SENTINEL }],
  });

  const group = getConstraintStateForGeyser(state, "steam");
  assert.equal(group?.state, "count_only");
  assert.deepEqual(group?.issues, []);
});
