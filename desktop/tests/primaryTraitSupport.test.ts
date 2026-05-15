import assert from "node:assert/strict";
import test from "node:test";

import {
    getPrimaryTraitBlockingError,
    getPrimaryTraitAvailabilityConflict,
    getPrimaryTraitCountExceededReason,
    getPrimaryTraitFilterDisabledReason,
    isPrimaryTraitValidationMessage,
    PRIMARY_TRAIT_FILTER_UNSUPPORTED_ERROR,
    supportsPrimaryTraitFiltering,
} from "../src/features/search/primaryTraitSupport.ts";

test("supports primary trait filtering before world profile loads", () => {
    assert.equal(supportsPrimaryTraitFiltering(null), true);
    assert.equal(getPrimaryTraitFilterDisabledReason(null), null);
});

test("disables primary trait filtering when world profile has no possible traits", () => {
    const profile = { possibleTraitIds: [] };
    assert.equal(supportsPrimaryTraitFiltering(profile), false);
    assert.equal(
        getPrimaryTraitFilterDisabledReason(profile),
        "当前世界格式的主星不生成可筛选特质",
    );
});

test("keeps primary trait filtering enabled when world profile exposes traits", () => {
    const profile = { possibleTraitIds: ["traits/MetalRich"] };
    assert.equal(supportsPrimaryTraitFiltering(profile), true);
    assert.equal(getPrimaryTraitFilterDisabledReason(profile), null);
});

test("warns when required primary trait count exceeds world upper bound", () => {
    const profile = {
        possibleTraitIds: ["traits/MetalRich", "traits/MetalPoor"],
        possibleTraitCountUpper: 1,
    };
    const reason = getPrimaryTraitCountExceededReason(profile, [
        { traitId: "traits/MetalRich", mode: "required" as const },
        { traitId: "traits/GeoActive", mode: "required" as const },
        { traitId: "traits/MagmaVents", mode: "forbidden" as const },
    ]);

    assert.equal(
        reason,
        "当前世界主星最多只会生成 1 个特质，你设置的“必须包含”数量已超过。",
    );
});

test("reports impossible required primary trait explicitly", () => {
    const profile = {
        possibleTraitIds: ["traits/GeoActive"],
        impossibleTraitIds: ["traits/MetalRich"],
        possibleTraitCountUpper: 1,
    };
    const conflict = getPrimaryTraitAvailabilityConflict(profile, [
        { traitId: "traits/MetalRich", mode: "required" as const },
        { traitId: "traits/GeoActive", mode: "required" as const },
    ]);

    assert.deepEqual(conflict, {
        severity: "error",
        message: "当前世界不会生成这个主星特质：金属富足",
    });
});

test("reports redundant forbidden primary trait as warning", () => {
    const profile = {
        possibleTraitIds: ["traits/GeoActive"],
        impossibleTraitIds: ["traits/MetalPoor"],
        possibleTraitCountUpper: 1,
    };
    const conflict = getPrimaryTraitAvailabilityConflict(profile, [
        { traitId: "traits/MetalPoor", mode: "forbidden" as const },
    ]);

    assert.deepEqual(conflict, {
        severity: "warning",
        message: "当前世界已经天然排除了这个主星特质：金属贫瘠。相关“必须排除”条件可以考虑删除。",
    });
});

test("getPrimaryTraitBlockingError keeps count overflow ahead of illegal trait errors", () => {
    const profile = {
        possibleTraitIds: ["traits/GeoActive"],
        impossibleTraitIds: ["traits/MetalRich"],
        possibleTraitCountUpper: 1,
    };

    const error = getPrimaryTraitBlockingError(profile, [
        { traitId: "traits/MetalRich", mode: "required" as const },
        { traitId: "traits/GeoActive", mode: "required" as const },
    ]);

    assert.equal(error, "当前世界主星最多只会生成 1 个特质，你设置的“必须包含”数量已超过。");
});

test("getPrimaryTraitBlockingError falls through to next illegal trait after count issue is removed", () => {
    const profile = {
        possibleTraitIds: ["traits/GeoActive"],
        impossibleTraitIds: ["traits/MetalRich", "traits/MetalPoor"],
        possibleTraitCountUpper: 1,
    };

    const firstError = getPrimaryTraitBlockingError(profile, [
        { traitId: "traits/MetalRich", mode: "required" as const },
        { traitId: "traits/GeoActive", mode: "required" as const },
    ]);
    const nextError = getPrimaryTraitBlockingError(profile, [
        { traitId: "traits/MetalPoor", mode: "required" as const },
    ]);
    const clearedError = getPrimaryTraitBlockingError(profile, [
        { traitId: "traits/GeoActive", mode: "required" as const },
    ]);

    assert.equal(firstError, "当前世界主星最多只会生成 1 个特质，你设置的“必须包含”数量已超过。");
    assert.equal(nextError, "当前世界不会生成这个主星特质：金属贫瘠");
    assert.equal(clearedError, null);
});

test("isPrimaryTraitValidationMessage only matches managed trait validation errors", () => {
    assert.equal(isPrimaryTraitValidationMessage(PRIMARY_TRAIT_FILTER_UNSUPPORTED_ERROR), true);
    assert.equal(isPrimaryTraitValidationMessage("当前世界主星最多只会生成 1 个特质，你设置的“必须包含”数量已超过。"), true);
    assert.equal(isPrimaryTraitValidationMessage("当前世界不会生成这个主星特质：金属富足"), true);
    assert.equal(isPrimaryTraitValidationMessage("搜索条件校验失败，请调整当前条件后重试。"), false);
});

test("does not warn when required primary trait count stays within upper bound", () => {
    const profile = {
        possibleTraitIds: ["traits/MetalRich", "traits/MetalPoor"],
        possibleTraitCountUpper: 2,
    };
    const reason = getPrimaryTraitCountExceededReason(profile, [
        { traitId: "traits/MetalRich", mode: "required" as const },
        { traitId: "traits/MagmaVents", mode: "forbidden" as const },
    ]);

    assert.equal(reason, null);
});
