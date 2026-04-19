import { useSearchStore } from "../../state/searchStore";

function formatDuration(startedAtMs: number | null): string {
  if (!startedAtMs) {
    return "00:00";
  }
  const seconds = Math.max(0, Math.floor((Date.now() - startedAtMs) / 1000));
  const minutesPart = String(Math.floor(seconds / 60)).padStart(2, "0");
  const secondsPart = String(seconds % 60).padStart(2, "0");
  return `${minutesPart}:${secondsPart}`;
}

export default function StatusBar() {
  const stats = useSearchStore((state) => state.stats);
  const isSearching = useSearchStore((state) => state.isSearching);
  const results = useSearchStore((state) => state.results.length);

  return (
    <footer className="status-bar">
      <span>状态: {isSearching ? "搜索中" : "空闲"}</span>
      <span>已扫描: {stats.processedSeeds.toLocaleString()}</span>
      <span>命中: {results.toLocaleString()}</span>
      <span>当前 seeds/s: {stats.currentSeedsPerSecond.toFixed(1)}</span>
      <span>峰值 seeds/s: {stats.peakSeedsPerSecond.toFixed(1)}</span>
      <span>运行时长: {formatDuration(stats.startedAtMs)}</span>
    </footer>
  );
}
