import test from "node:test";
import assert from "node:assert/strict";

import { beginSidecarBinding } from "../src/state/searchStoreState.ts";

test("beginSidecarBinding blocks duplicate subscription while first listener is still binding", () => {
  const initial = {
    listening: false,
    bindingSidecar: false,
    lastError: null,
  };

  const first = beginSidecarBinding(initial);
  const second = beginSidecarBinding(first.nextState);

  assert.equal(first.shouldSubscribe, true);
  assert.equal(second.shouldSubscribe, false);
});
