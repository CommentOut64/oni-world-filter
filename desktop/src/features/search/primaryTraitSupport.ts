import type { WorldEnvelopeProfile } from "../../lib/contracts";
import { TRAIT_DISPLAY_NAMES } from "../../lib/traitDisplayNames.ts";

type TraitSupportProfile = Pick<
    WorldEnvelopeProfile,
    "possibleTraitIds" | "possibleTraitCountUpper" | "impossibleTraitIds"
>;
type TraitRuleLike = {
    traitId: string;
    mode: "required" | "forbidden";
};
type TraitAvailabilityConflict = {
    severity: "error" | "warning";
    message: string;
};

export const PRIMARY_TRAIT_FILTER_UNSUPPORTED_ERROR =
    "当前世界格式的主星不支持特质筛选，请清除“主星特质”规则或切换世界格式。";

function formatTraitName(traitId: string): string {
    return TRAIT_DISPLAY_NAMES[traitId] ?? traitId;
}

function isTraitImpossibleInWorld(
    profile: TraitSupportProfile,
    traitId: string,
): boolean {
    if (Array.isArray(profile.impossibleTraitIds) && profile.impossibleTraitIds.includes(traitId)) {
        return true;
    }
    return Array.isArray(profile.possibleTraitIds) && !profile.possibleTraitIds.includes(traitId);
}

export function supportsPrimaryTraitFiltering(
    profile: TraitSupportProfile | null | undefined,
): boolean {
    if (!profile || !Array.isArray(profile.possibleTraitIds)) {
        return true;
    }
    return profile.possibleTraitIds.length > 0;
}

export function getPrimaryTraitFilterDisabledReason(
    profile: TraitSupportProfile | null | undefined,
): string | null {
    if (supportsPrimaryTraitFiltering(profile)) {
        return null;
    }
    return "当前世界格式的主星不生成可筛选特质";
}

export function isPrimaryTraitValidationMessage(message: string): boolean {
    return (
        message === PRIMARY_TRAIT_FILTER_UNSUPPORTED_ERROR ||
        message.startsWith("当前世界主星最多只会生成 ") ||
        message.startsWith("当前世界不会生成这个主星特质：")
    );
}

export function getPrimaryTraitBlockingError(
    profile: TraitSupportProfile | null | undefined,
    traitRules: readonly TraitRuleLike[],
): string | null {
    const hasTraitConstraints = traitRules.some((item) => item.traitId.trim().length > 0);
    if (!supportsPrimaryTraitFiltering(profile) && hasTraitConstraints) {
        return PRIMARY_TRAIT_FILTER_UNSUPPORTED_ERROR;
    }
    const countExceededReason = getPrimaryTraitCountExceededReason(profile, traitRules);
    if (countExceededReason) {
        return countExceededReason;
    }
    const availabilityConflict = getPrimaryTraitAvailabilityConflict(profile, traitRules);
    if (availabilityConflict?.severity === "error") {
        return availabilityConflict.message;
    }
    return null;
}

export function getPrimaryTraitAvailabilityConflict(
    profile: TraitSupportProfile | null | undefined,
    traitRules: readonly TraitRuleLike[],
): TraitAvailabilityConflict | null {
    if (!profile || !Array.isArray(profile.possibleTraitIds) || !supportsPrimaryTraitFiltering(profile)) {
        return null;
    }
    for (const item of traitRules) {
        const traitId = item.traitId.trim();
        if (!traitId || item.mode !== "required") {
            continue;
        }
        if (!isTraitImpossibleInWorld(profile, traitId)) {
            continue;
        }
        return {
            severity: "error",
            message: `当前世界不会生成这个主星特质：${formatTraitName(traitId)}`,
        };
    }
    for (const item of traitRules) {
        const traitId = item.traitId.trim();
        if (!traitId || item.mode !== "forbidden") {
            continue;
        }
        if (!isTraitImpossibleInWorld(profile, traitId)) {
            continue;
        }
        return {
            severity: "warning",
            message: `当前世界已经天然排除了这个主星特质：${formatTraitName(traitId)}。相关“必须排除”条件可以考虑删除。`,
        };
    }
    return null;
}

export function getPrimaryTraitCountExceededReason(
    profile: TraitSupportProfile | null | undefined,
    traitRules: readonly TraitRuleLike[],
): string | null {
    if (!profile || !supportsPrimaryTraitFiltering(profile)) {
        return null;
    }
    const upper = profile.possibleTraitCountUpper;
    if (!Number.isInteger(upper) || upper <= 0) {
        return null;
    }
    const requiredCount = traitRules.filter(
        (item) => item.mode === "required" && item.traitId.trim().length > 0,
    ).length;
    if (requiredCount <= upper) {
        return null;
    }
    return `当前世界主星最多只会生成 ${upper} 个特质，你设置的“必须包含”数量已超过。`;
}
