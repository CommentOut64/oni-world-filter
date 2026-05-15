import type { SearchAnalysisPayload } from "../../lib/contracts";
import { formatGeyserNameByKey } from "../../lib/displayResolvers";
import type { SearchDraft } from "../../state/searchStore.ts";
import { formatProbabilityUpper } from "./searchProbabilityFormat";
import { formatAnalysisErrorMessage } from "./searchAnalysisDisplay.ts";

const EMPTY_DRAFT: SearchDraft = {
  worldType: 0,
  seedStart: 0,
  seedEnd: 0,
  mixing: 0,
  cpu: {
    mode: "balanced",
    allowSmt: true,
    allowLowPerf: false,
    placement: "strict",
  },
  constraints: {
    required: [],
    forbidden: [],
    distance: [],
    count: [],
    requiredTraits: [],
    forbiddenTraits: [],
  },
};

interface SearchAnalysisHintsProps {
  analysis: SearchAnalysisPayload | null;
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
          乐观估计可匹配概率: {formatProbabilityUpper(analysis.predictedBottleneckProbability)}
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
            <li key={`${item.code}-${index}`}>
              {formatAnalysisErrorMessage(item, analysis, EMPTY_DRAFT, [], [])}
            </li>
          ))}
        </ul>
      ) : null}

      {analysis.warnings.length > 0 ? (
        <ul className="analysis-list analysis-list-warning">
          {analysis.warnings.map((item, index) => (
            <li key={`${item.code}-${index}`}>
              {formatAnalysisErrorMessage(item, analysis, EMPTY_DRAFT, [], [])}
            </li>
          ))}
        </ul>
      ) : null}
    </section>
  );
}
