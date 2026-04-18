import test from "node:test";
import assert from "node:assert/strict";

import type { SearchMatchSummary } from "../src/lib/contracts.ts";
import { appendUniqueSearchResult } from "../src/state/searchResultsState.ts";

function makeResult(seed: number, mixing = 625): SearchMatchSummary {
  return {
    seed,
    worldType: 13,
    mixing,
    coord: `V-SNDST-C-${seed}-0-D3-HD`,
    traits: [],
    start: { x: 10, y: 20 },
    worldSize: { w: 240, h: 380 },
    geysers: [],
    nearestDistance: null,
  };
}

test("appendUniqueSearchResult ignores duplicate seed entries within the same search context", () => {
  const first = makeResult(100000);
  const duplicate = makeResult(100000);

  const results = appendUniqueSearchResult([first], duplicate);

  assert.equal(results.length, 1);
  assert.deepEqual(results[0], first);
});

test("appendUniqueSearchResult keeps distinct seeds", () => {
  const first = makeResult(100000);
  const second = makeResult(100001);

  const results = appendUniqueSearchResult([first], second);

  assert.equal(results.length, 2);
  assert.deepEqual(results[0], first);
  assert.deepEqual(results[1], second);
});
