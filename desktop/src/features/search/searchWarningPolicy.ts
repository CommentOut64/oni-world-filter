import type { SearchAnalysisPayload, ValidationIssue } from "../../lib/contracts";

const LOW_PROBABILITY_CONFIRM_THRESHOLD = 0.05;

export function isBlockingSearchWarning(issue: ValidationIssue): boolean {
  return (
    issue.code.startsWith("predict.low_probability.") ||
    issue.code === "predict.generic_capacity_pruned"
  );
}

export function shouldRequireSearchWarningConfirmation(
  warnings: readonly ValidationIssue[]
): boolean {
  return warnings.some((issue) => isBlockingSearchWarning(issue));
}

function hasValidProbability(probability: number): boolean {
  return Number.isFinite(probability) && probability >= 0;
}

export function shouldShowSearchWarningConfirmation(
  analysis: Pick<SearchAnalysisPayload, "warnings" | "predictedBottleneckProbability">
): boolean {
  if (!shouldRequireSearchWarningConfirmation(analysis.warnings)) {
    return false;
  }

  if (!hasValidProbability(analysis.predictedBottleneckProbability)) {
    return false;
  }

  return analysis.warnings.some((issue) => {
    if (!isBlockingSearchWarning(issue)) {
      return false;
    }
    if (issue.code === "predict.generic_capacity_pruned") {
      return true;
    }
    if (issue.code.startsWith("predict.low_probability.")) {
      return analysis.predictedBottleneckProbability < LOW_PROBABILITY_CONFIRM_THRESHOLD;
    }
    return false;
  });
}
