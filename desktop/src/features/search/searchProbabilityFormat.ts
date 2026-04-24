function trimTrailingZeros(value: string): string {
  return value.replace(/(?:\.0+|(\.\d*?[1-9])0+)$/, "$1");
}

export function formatProbabilityUpper(probability: number): string {
  if (!Number.isFinite(probability)) {
    return "-";
  }
  if (probability <= 0) {
    return "0%";
  }

  const percent = probability * 100;
  if (percent < 0.01) {
    return "< 0.01%";
  }

  return `${trimTrailingZeros(percent.toFixed(3))}%`;
}

export function formatSearchWarningProbabilityCopy(probability: number): string {
  const display = formatProbabilityUpper(probability);
  if (display === "-") {
    return "乐观估计暂不可用。";
  }
  return `乐观估计可匹配概率约为 ${display}。`;
}
