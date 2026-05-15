import test from "node:test";
import assert from "node:assert/strict";

import { hasMatchingWorldProfileRequest } from "../src/features/search/worldProfileRequestState.ts";

test("hasMatchingWorldProfileRequest returns false when no cached profile exists", () => {
  assert.equal(hasMatchingWorldProfileRequest(null, 13, 0), false);
});

test("hasMatchingWorldProfileRequest requires both worldType and mixing to match", () => {
  assert.equal(
    hasMatchingWorldProfileRequest(
      {
        worldType: 13,
        mixing: 0,
      },
      13,
      0
    ),
    true
  );
  assert.equal(
    hasMatchingWorldProfileRequest(
      {
        worldType: 13,
        mixing: 0,
      },
      13,
      625
    ),
    false
  );
  assert.equal(
    hasMatchingWorldProfileRequest(
      {
        worldType: 13,
        mixing: 0,
      },
      14,
      0
    ),
    false
  );
});
