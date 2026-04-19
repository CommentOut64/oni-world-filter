import assert from "node:assert/strict";

import { formatGeyserCountSummary } from "../src/features/results/resultSummary.ts";

const geysers = [
  { id: 2, key: "hot_water" },
  { id: 0, key: "steam" },
  { id: 6, key: "salt_water" },
];

const summary = formatGeyserCountSummary(
  [
    { type: 2, x: 10, y: 10 },
    { type: 0, x: 20, y: 20 },
    { type: 2, x: 30, y: 30 },
    { type: 6, x: 40, y: 40 },
    { type: 0, x: 50, y: 50 },
  ],
  geysers
);

assert.equal(
  summary,
  "清水泉（Water Geyser） x2, 低温蒸汽喷孔（Cool Steam Vent） x2, 盐水泉（Salt Water Geyser）"
);

console.log("geyser summary ok");
