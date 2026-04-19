import type { SearchAnalysisPayload } from "../../lib/contracts";
import { formatGeyserNameByKey } from "../../lib/displayResolvers";

interface SearchAnalysisHintsProps {
  analysis: SearchAnalysisPayload | null;
}

function formatProbabilityUpper(probability: number): string {
  if (!Number.isFinite(probability)) {
    return "-";
  }
  if (probability <= 0) {
    return "0";
  }
  if (probability < 0.0001) {
    return probability.toExponential(2);
  }
  return `${(probability * 100).toFixed(3)}%`;
}

export default function SearchAnalysisHints({ analysis }: SearchAnalysisHintsProps) {
  if (!analysis) {
    return null;
  }

  const hasStrongWarning = analysis.warnings.some((item) =>
    item.code.includes("strong-warning")
  );
  const severityClass = hasStrongWarning
    ? "analysis-hints-strong"
    : analysis.warnings.length > 0
      ? "analysis-hints-warning"
      : "analysis-hints-normal";

  return (
    <section className={`analysis-hints ${severityClass}`}>
      <header>
        <h4>分析结果</h4>
        <span className="analysis-probability">
          瓶颈概率上界: {formatProbabilityUpper(analysis.predictedBottleneckProbability)}
        </span>
      </header>

      {analysis.bottlenecks.length > 0 ? (
        <p className="analysis-bottlenecks">
          主要瓶颈: {analysis.bottlenecks.map((item) => formatGeyserNameByKey(item)).join(", ")}
        </p>
      ) : (
        <p className="hint">暂无瓶颈组输出</p>
      )}

      {analysis.errors.length > 0 ? (
        <ul className="analysis-list analysis-list-error">
          {analysis.errors.map((item, index) => (
            <li key={`${item.code}-${index}`}>{item.message}</li>
          ))}
        </ul>
      ) : null}

      {analysis.warnings.length > 0 ? (
        <ul className="analysis-list analysis-list-warning">
          {analysis.warnings.map((item, index) => (
            <li key={`${item.code}-${index}`}>{item.message}</li>
          ))}
        </ul>
      ) : null}
    </section>
  );
}
