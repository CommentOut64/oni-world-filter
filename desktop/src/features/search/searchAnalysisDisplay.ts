import type {
  GeyserOption,
  MixingSlotMeta,
  SearchAnalysisPayload,
  ValidationIssue,
} from "../../lib/contracts.ts";
import { formatGeyserNameByKey, formatMixingSlotName } from "../../lib/displayResolvers.ts";
import type { SearchDraft } from "../../state/searchStore.ts";
import { decodeMixingToLevels, MIXING_SLOT_COUNT } from "./searchSchema.ts";

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

function formatGeyserTail(message: string): string | null {
  const geyserId = extractTrailingGeyserId(message);
  if (!geyserId) {
    return null;
  }
  return formatGeyserNameByKey(geyserId);
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

  if (issue.code === "range.distance_min_negative") {
    return "距离规则的最小值不能小于 0。";
  }
  if (issue.code === "range.distance_max_negative") {
    return "距离规则的最大值不能小于 0。";
  }
  if (issue.code === "range.distance_min_gt_max") {
    return "距离规则的最大值不能小于最小值。";
  }
  if (issue.code === "range.count_min_less_than_one") {
    return "必须包含的数量至少为 1。";
  }
  if (issue.code === "range.count_max_less_than_one") {
    return "必须包含的数量至少为 1。";
  }
  if (issue.code === "range.count_min_gt_max") {
    return "必须包含的最大数量不能小于最小数量。";
  }
  if (issue.code === "world.distance_max_gt_world_diagonal") {
    return "距离规则超出了当前世界的最大可选范围。";
  }
  if (issue.code === "world.distance_min_gt_world_diagonal") {
    return "距离规则超出了当前世界的最大可选范围。";
  }

  const geyserName = formatGeyserTail(issue.message);
  if (issue.code === "world.geyser_impossible" && geyserName) {
    return `当前世界不会生成这个喷口：${geyserName}`;
  }
  if (issue.code === "world.count_max_gt_possible_max" && geyserName) {
    return `这个喷口在当前世界里达不到你设置的“必须包含”数量上限：${geyserName}`;
  }
  if (issue.code === "world.count_min_gt_possible_max" && geyserName) {
    return `这个喷口在当前世界里达不到你设置的“必须包含”数量下限：${geyserName}`;
  }
  if (issue.code === "world.forbidden_geyser_already_impossible" && geyserName) {
    return `当前世界已经天然排除了这个喷口：${geyserName}。相关“必须排除”条件可以考虑删除。`;
  }
  if (issue.code === "conflict.required_forbidden" && geyserName) {
    return `同一个喷口不能同时设置“必须包含”和“必须排除”：${geyserName}`;
  }
  if (issue.code === "conflict.required_with_count" && geyserName) {
    return `这个喷口已经在“必须包含”里设置了数量，不要再单独添加旧的包含条件：${geyserName}`;
  }
  if (issue.code === "conflict.required_with_distance" && geyserName) {
    return `这个喷口已经设置了距离规则，不要再单独添加“必须包含”：${geyserName}`;
  }
  if (issue.code === "conflict.forbidden_with_count" && geyserName) {
    return `这个喷口已经设置了“必须排除”，不能再设置“必须包含”：${geyserName}`;
  }
  if (issue.code === "conflict.forbidden_with_distance" && geyserName) {
    return `这个喷口已经设置了“必须排除”，不能再设置距离规则：${geyserName}`;
  }
  if (issue.code === "conflict.count_min_gt_max" && geyserName) {
    return `这个喷口的“必须包含”最大数量不能小于最小数量：${geyserName}`;
  }

  return issue.message;
}

