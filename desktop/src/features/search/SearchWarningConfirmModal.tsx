import type { GeyserOption, SearchAnalysisPayload } from "../../lib/contracts";
import { formatGeyserNameByKey } from "../../lib/displayResolvers";

interface SearchWarningConfirmModalProps {
  open: boolean;
  analysis: SearchAnalysisPayload | null;
  geysers: readonly GeyserOption[];
  onContinue: () => void;
  onAbandon: () => void;
}

function formatProbability(probability: number): string {
  if (!Number.isFinite(probability) || probability <= 0) {
    return "接近 0%";
  }
  const percent = probability * 100;
  if (percent < 0.01) {
    return "< 0.01%";
  }
  return `${percent.toFixed(3)}%`;
}

function formatBottlenecks(analysis: SearchAnalysisPayload, geysers: readonly GeyserOption[]): string {
  if (analysis.bottlenecks.length === 0) {
    return "暂无";
  }
  return analysis.bottlenecks
    .map((item) => {
      const matched = geysers.find((geyser) => geyser.key === item);
      return matched ? formatGeyserNameByKey(matched.key) : formatGeyserNameByKey(item);
    })
    .join("、");
}

export default function SearchWarningConfirmModal({
  open,
  analysis,
  geysers,
  onContinue,
  onAbandon,
}: SearchWarningConfirmModalProps) {
  if (!open || !analysis) {
    return null;
  }

  return (
    <div className="search-warning-modal-overlay" role="dialog" aria-modal="true">
      <section className="search-warning-modal">
        <header className="search-warning-modal-header">
          <h4>搜索前提醒</h4>
        </header>
        <div className="search-warning-modal-body">
          <p>乐观估计找到目标结果的概率为 {formatProbability(analysis.predictedBottleneckProbability)}。</p>
          <p>主要瓶颈在于：{formatBottlenecks(analysis, geysers)}。</p>
          <p>是否继续搜索？</p>
        </div>
        <footer className="search-warning-modal-actions">
          <button type="button" className="primary" onClick={onContinue}>
            继续
          </button>
          <button type="button" onClick={onAbandon}>
            放弃
          </button>
        </footer>
      </section>
    </div>
  );
}
