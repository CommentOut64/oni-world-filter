import { useSearchStore } from "../../state/searchStore";

function formatRuntime(startedAtMs: number | null): string {
  if (!startedAtMs) {
    return "00:00";
  }
  const elapsed = Math.max(0, Math.floor((Date.now() - startedAtMs) / 1000));
  const minutesPart = String(Math.floor(elapsed / 60)).padStart(2, "0");
  const secondsPart = String(elapsed % 60).padStart(2, "0");
  return `${minutesPart}:${secondsPart}`;
}

export default function ResultSummaryCards() {
  const stats = useSearchStore((state) => state.stats);

  return (
    <section className="summary-cards">
      <article>
        <h4>已扫描种子</h4>
        <p>{stats.processedSeeds.toLocaleString()}</p>
      </article>
      <article>
        <h4>命中数</h4>
        <p>{stats.totalMatches.toLocaleString()}</p>
      </article>
      <article>
        <h4>当前 seeds/s</h4>
        <p>{stats.currentSeedsPerSecond.toFixed(1)}</p>
      </article>
      <article>
        <h4>运行时长</h4>
        <p>{formatRuntime(stats.startedAtMs)}</p>
      </article>
    </section>
  );
}
