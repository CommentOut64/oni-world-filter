import type { GeyserDetail, GeyserSummary } from "../../lib/contracts.ts";
import { formatGeyserNameFromSummary } from "../../lib/displayResolvers";
import type { GeyserOption } from "../../lib/contracts.ts";

const NO_PARAMETER_MESSAGES: Record<string, string> = {
  facility: "该对象是固定设施，不生成喷口参数。",
  reservoir: "该对象不适用当前喷口参数算法。",
  unknown: "该对象暂无可展示的参数信息。",
};

export function formatGeyserDetailTitle(summary: GeyserSummary, geysers: readonly GeyserOption[]): string {
  return formatGeyserNameFromSummary(summary, geysers);
}

export function formatGeyserDetailCoords(summary: GeyserSummary): string {
  return `(${summary.x}, ${summary.y})`;
}

export function formatGeyserDetailMissingMessage(parameterKind: string): string {
  return NO_PARAMETER_MESSAGES[parameterKind] ?? NO_PARAMETER_MESSAGES.unknown;
}

export function formatGeyserDetailTemperature(detail: GeyserDetail): string {
  return `${detail.derived.temperatureCelsius.toFixed(1)} °C`;
}

export function formatGeyserDetailEruptionRate(detail: GeyserDetail): string {
  return `${detail.derived.eruptionRateKgPerSecond.toFixed(1)} kg/s`;
}

export function formatGeyserDetailAverageYield(detail: GeyserDetail): string {
  return `${detail.derived.averageOverallYieldGPerSecond.toFixed(1)} g/s`;
}

export function formatGeyserDetailEruptionWindow(detail: GeyserDetail): string {
  return `每 ${detail.native.eruptionPeriodSeconds.toFixed(1)} 秒喷发 ${detail.derived.eruptionSeconds.toFixed(1)} 秒`;
}

export function formatGeyserDetailActiveWindow(detail: GeyserDetail): string {
  return `每 ${detail.derived.totalCycles.toFixed(1)} 周期活跃 ${detail.derived.activeCycles.toFixed(1)} 周期`;
}

