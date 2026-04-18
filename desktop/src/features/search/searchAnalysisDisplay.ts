import type {
  GeyserOption,
  MixingSlotMeta,
  SearchAnalysisPayload,
  ValidationIssue,
} from "../../lib/contracts";
import { formatGeyserNameByKey, formatMixingSlotName } from "../../lib/displayResolvers";
import type { SearchDraft } from "../../state/searchStore";
import { decodeMixingToLevels, MIXING_SLOT_COUNT } from "./searchSchema";

function extractTrailingGeyserId(message: string): string | null {
  const matched = message.match(/: ([a-z0-9_]+)$/i);
  return matched?.[1] ?? null;
}

function joinNames(names: readonly string[]): string {
  return names.join("、");
}

function formatDisabledMixingSlots(
  draft: SearchDraft,
  analysis: SearchAnalysisPayload,
  mixingSlots: readonly MixingSlotMeta[]
): string | null {
  const slotCount = Math.max(mixingSlots.length, MIXING_SLOT_COUNT);
  const levels = decodeMixingToLevels(draft.mixing, slotCount);
  const invalidSlots = analysis.worldProfile.disabledMixingSlots.filter(
    (slot) => slot >= 0 && slot < levels.length && levels[slot] > 0
  );
  if (invalidSlots.length === 0) {
    return null;
  }
  const names = invalidSlots.map((slot) => {
    const matched = mixingSlots.find((item) => item.slot === slot);
    return matched ? formatMixingSlotName(matched) : `slot#${slot}`;
  });
  return `当前世界不允许启用：${joinNames(names)}。请先关闭这些选项，再开始搜索。`;
}

function formatGeyserIssue(message: string): string {
  const geyserId = extractTrailingGeyserId(message);
  if (!geyserId) {
    return message;
  }
  return message.replace(geyserId, formatGeyserNameByKey(geyserId));
}

export function formatAnalysisErrorMessage(
  issue: ValidationIssue,
  analysis: SearchAnalysisPayload,
  draft: SearchDraft,
  mixingSlots: readonly MixingSlotMeta[],
  geysers: readonly GeyserOption[]
): string {
  void geysers;

  if (issue.code === "world.disabled_mixing_slot_enabled") {
    return formatDisabledMixingSlots(draft, analysis, mixingSlots) ?? issue.message;
  }

  if (
    issue.code === "world.geyser_impossible" ||
    issue.code === "world.count_max_gt_possible_max" ||
    issue.code === "world.count_min_gt_possible_max" ||
    issue.code === "conflict.required_forbidden" ||
    issue.code === "conflict.count_min_gt_max" ||
    issue.code === "conflict.forbidden_with_distance"
  ) {
    return formatGeyserIssue(issue.message);
  }

  return issue.message;
}

