import type {
  GeyserOption,
  MixingSlotMeta,
  SearchAnalysisPayload,
  ValidationIssue,
} from "../../lib/contracts.ts";
import { formatGeyserNameByKey, formatMixingSlotName } from "../../lib/displayResolvers.ts";
import { TRAIT_DISPLAY_NAMES } from "../../lib/traitDisplayNames.ts";
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

function extractTrailingTraitId(message: string): string | null {
    const matched = message.match(/: ([^:]+)$/);
    return matched?.[1]?.trim() ?? null;
}

function extractTrailingTraitPair(
    message: string,
): readonly [string, string] | null {
    const matched = message.match(/: ([^:]+?) \/ ([^:]+)$/);
    if (!matched) {
        return null;
    }
    return [matched[1].trim(), matched[2].trim()];
}

function formatTraitName(traitId: string): string {
    return TRAIT_DISPLAY_NAMES[traitId] ?? traitId;
}

function formatGenericAnalysisIssue(issue: ValidationIssue): string {
    if (issue.code.startsWith("predict.low_probability.")) {
        return "当前条件命中概率很低，建议先放宽主要瓶颈条件后再搜索。";
    }
    if (issue.code === "predict.generic_capacity_pruned") {
        return "当前条件超出了共享 generic 槽位容量上限，建议先放宽相关“必须包含”条件。";
    }
    if (issue.code === "predict.dependency_fallback_min") {
        return "当前瓶颈组存在共享依赖，分析结果已回退为保守估计。";
    }
    return `搜索条件校验失败，请调整当前条件后重试。（${issue.code}）`;
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
      return (
          formatDisabledMixingSlots(draft, analysis, mixingSlots) ??
          "当前世界不允许启用所选混搭项，请先关闭后再开始搜索。"
      );
  }

  if (issue.code === "range.seed_start_negative") {
      return "Seed Start 不能小于 0。";
  }
  if (issue.code === "range.seed_end_negative") {
      return "Seed End 不能小于 0。";
  }
  if (issue.code === "range.seed_start_gt_seed_end") {
      return "Seed End 不能小于 Seed Start。";
  }
  if (issue.code === "range.mixing_out_of_bounds") {
      return "当前世界混搭参数无效。";
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
  if (issue.code === "world.world_type_out_of_range") {
      return "当前世界格式无效。";
  }
  if (issue.code === "world.required_trait_count_gt_possible_max") {
      const upper = analysis.worldProfile.possibleTraitCountUpper;
      if (Number.isInteger(upper) && upper > 0) {
          return `当前世界主星最多只会生成 ${upper} 个特质，你设置的“必须包含”数量已超过。`;
      }
      return "当前世界主星可生成的特质数量上限不足，无法同时满足这些“主星特质”条件。";
  }

  const geyserName = formatGeyserTail(issue.message);
  const traitId = extractTrailingTraitId(issue.message);
  const traitPair = extractTrailingTraitPair(issue.message);
  if (issue.code === "world.unknown_geyser" && geyserName) {
      return `存在未知喷口条件：${geyserName}`;
  }
  if (issue.code === "world.unknown_geyser") {
      return "存在未知喷口条件。";
  }
  if (issue.code === "world.unknown_trait" && traitId) {
      return `存在未知主星特质条件：${formatTraitName(traitId)}`;
  }
  if (issue.code === "world.unknown_trait") {
      return "存在未知主星特质条件。";
  }
  if (issue.code === "world.geyser_impossible" && geyserName) {
    return `当前世界不会生成这个喷口：${geyserName}`;
  }
  if (issue.code === "world.required_trait_impossible" && traitId) {
      return `当前世界不会生成这个主星特质：${formatTraitName(traitId)}`;
  }
  if (issue.code === "world.forbidden_trait_already_impossible" && traitId) {
      return `当前世界已经天然排除了这个主星特质：${formatTraitName(traitId)}。相关“必须排除”条件可以考虑删除。`;
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
  if (
      (issue.code === "conflict.required_trait_duplicate" ||
          issue.code === "conflict.forbidden_trait_duplicate") &&
      traitId
  ) {
      return `“主星特质”里有重复特质：${formatTraitName(traitId)}`;
  }
  if (issue.code === "conflict.required_forbidden_trait" && traitId) {
      return `同一个主星特质不能同时设置“必须包含”和“必须排除”：${formatTraitName(traitId)}`;
  }
  if (
      issue.code === "conflict.required_traits_mutually_exclusive" &&
      traitPair
  ) {
      return `这两个主星特质互斥，不能同时选择：${joinNames(traitPair.map(formatTraitName))}`;
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

  return formatGenericAnalysisIssue(issue);
}

