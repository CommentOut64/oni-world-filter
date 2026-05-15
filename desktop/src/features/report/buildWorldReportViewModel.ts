import type { SearchCatalog, WorldOption, WorldReportData } from "../../lib/contracts.ts";
import { normalizeSearchCatalog } from "../../lib/searchCatalog.ts";
import {
  formatGeyserNameFromSummary,
  formatMixingSlotName,
  formatWorldName,
  formatWorldNameByAssetId,
  geyserKeyFromType,
} from "../../lib/displayResolvers.ts";
import { sortResolvedGeyserItems } from "../../lib/geyserOrdering.ts";
import {
  formatGeyserDetailActiveWindow,
  formatGeyserDetailAverageYield,
  formatGeyserDetailCoords,
  formatGeyserDetailEruptionRate,
  formatGeyserDetailEruptionWindow,
  formatGeyserDetailTemperature,
} from "../preview/geyserDetailFormatters.ts";
import { decodeMixingToLevels } from "../search/searchSchema.ts";
import {
  classifyWorld,
  groupMixingSlots,
  levelToUiMode,
  WORLD_CATEGORY_OPTIONS,
} from "../search/worldParameterUi.ts";
import type { WorldReportGeyserRow, WorldReportViewModel } from "./reportTypes.ts";

function formatPointLabel(point: { x: number; y: number }): string {
  return `(${point.x}, ${point.y})`;
}

function resolveWorldOption(worlds: readonly WorldOption[], worldType: number): WorldOption {
  return worlds.find((item) => item.id === worldType) ?? { id: worldType, code: String(worldType) };
}

function resolveWorldOptionByCoord(worlds: readonly WorldOption[], coord: string): WorldOption | null {
  let resolved: WorldOption | null = null;
  for (const world of worlds) {
    if (!coord.startsWith(world.code)) {
      continue;
    }
    if (resolved === null || world.code.length > resolved.code.length) {
      resolved = world;
    }
  }
  return resolved;
}

function formatMixingMode(level: number): string {
  return levelToUiMode(level) === "guaranteed" ? "必出" : "开启";
}

function buildMixingSummary(report: WorldReportData, catalog: SearchCatalog): string {
  const levels = decodeMixingToLevels(report.mixing, catalog.mixingSlots.length);
  const groups = groupMixingSlots(catalog.mixingSlots);
  const parts: string[] = [];

  for (const group of groups) {
    const packageLevel = levels[group.packageSlot.slot] ?? 0;
    if (packageLevel <= 0) {
      continue;
    }

    const enabledChildren = group.children
      .filter((slot) => (levels[slot.slot] ?? 0) > 0)
      .map((slot) => formatMixingSlotName(slot));

    const packageLabel = `${formatMixingSlotName(group.packageSlot)}（${formatMixingMode(packageLevel)}）`;
    parts.push(
      enabledChildren.length > 0 ? `${packageLabel}：${enabledChildren.join("、")}` : packageLabel
    );
  }

  return parts.length > 0 ? parts.join("；") : "未启用 DLC 混搭";
}

function buildGeyserRows(report: WorldReportData, catalog: SearchCatalog): WorldReportGeyserRow[] {
  const sortedDetails = sortResolvedGeyserItems(report.geyserDetails, (detail) => ({
    geyserKey: detail.summary.id ?? geyserKeyFromType(detail.summary.type, catalog.geysers),
    name: formatGeyserNameFromSummary(detail.summary, catalog.geysers),
    disabled: false,
    stableKey: `${detail.summary.id ?? detail.summary.type}-${detail.index}`,
  }));

  return sortedDetails.map((detail) => {
    if (!detail.hasParameters) {
      return {
        name: formatGeyserNameFromSummary(detail.summary, catalog.geysers),
        coord: formatGeyserDetailCoords(detail.summary),
        temperature: "-",
        eruptionRate: "-",
        averageYield: "-",
        eruptionWindow: "-",
        activeWindow: "-",
      };
    }

    return {
      name: formatGeyserNameFromSummary(detail.summary, catalog.geysers),
      coord: formatGeyserDetailCoords(detail.summary),
      temperature: formatGeyserDetailTemperature(detail),
      eruptionRate: formatGeyserDetailEruptionRate(detail),
      averageYield: formatGeyserDetailAverageYield(detail),
      eruptionWindow: formatGeyserDetailEruptionWindow(detail),
      activeWindow: formatGeyserDetailActiveWindow(detail),
    };
  });
}

export function buildWorldReportViewModel(
  report: WorldReportData,
  rawCatalog: Partial<SearchCatalog> | SearchCatalog | null | undefined
): WorldReportViewModel {
  const catalog = normalizeSearchCatalog(rawCatalog);
  const summary = report.preview.summary;
  const coordWorld = resolveWorldOptionByCoord(catalog.worlds, report.coord);
  const fallbackWorld = resolveWorldOption(catalog.worlds, summary.worldType);
  const worldCategory = classifyWorld((coordWorld ?? fallbackWorld).code);
  const worldCategoryLabel =
    WORLD_CATEGORY_OPTIONS.find((item) => item.id === worldCategory)?.label ?? "未知";
  const worldName =
    !summary.isPrimary && summary.worldAssetId
      ? formatWorldNameByAssetId(summary.worldAssetId)
      : formatWorldName(coordWorld ?? fallbackWorld);

  return {
    worldCategoryLabel,
    worldName,
    coord: report.coord,
    seedLabel: String(summary.seed),
    worldSizeLabel: `${summary.worldSize.w} × ${summary.worldSize.h}`,
    startLabel: formatPointLabel(summary.start),
    mixingSummary: buildMixingSummary(report, catalog),
    geyserRows: buildGeyserRows(report, catalog),
  };
}
