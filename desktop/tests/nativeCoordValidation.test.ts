import test from "node:test";
import assert from "node:assert/strict";

import { validateNativeCoordInput } from "../src/features/search/nativeCoordValidation.ts";
import { FALLBACK_SEARCH_CATALOG } from "../src/lib/searchCatalog.ts";

const WORLD_CODES = FALLBACK_SEARCH_CATALOG.worlds.map((item) => item.code);

test("validateNativeCoordInput accepts native coord with zero trailing mixing code", () => {
  assert.equal(validateNativeCoordInput("V-SNDST-C-1927980015-0-3A-0", WORLD_CODES), null);
});

test("validateNativeCoordInput accepts short non-zero trailing mixing code", () => {
  assert.equal(validateNativeCoordInput("V-SNDST-C-123456-0-D3-HD", WORLD_CODES), null);
});

test("validateNativeCoordInput rejects trailing mixing code longer than five chars", () => {
  assert.equal(
    validateNativeCoordInput("V-SNDST-C-123456-0-D3-ABCDE1", WORLD_CODES),
    "坐标无效：请输入完整原生坐标，且最后一段需为 1 到 5 位大写 base36，且不能超出 mixing 有效范围。"
  );
});

test("validateNativeCoordInput rejects trailing mixing code outside mixing range", () => {
  assert.equal(
    validateNativeCoordInput("V-SNDST-C-123456-0-D3-ZZZZZ", WORLD_CODES),
    "坐标无效：请输入完整原生坐标，且最后一段需为 1 到 5 位大写 base36，且不能超出 mixing 有效范围。"
  );
});

test("validateNativeCoordInput rejects unknown world prefix", () => {
  assert.equal(
    validateNativeCoordInput("BAD-123456-0-3A-0", WORLD_CODES),
    "坐标无效：请输入完整原生坐标，且最后一段需为 1 到 5 位大写 base36，且不能超出 mixing 有效范围。"
  );
});
