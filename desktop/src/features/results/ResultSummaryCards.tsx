import React from "react";
import { Card, Statistic } from "antd";

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
  void React;
  const stats = useSearchStore((state) => state.stats);

  return (
    <section className="summary-cards">
      <Card size="small">
        <Statistic title="已扫描种子" value={stats.processedSeeds.toLocaleString()} />
      </Card>
      <Card size="small">
        <Statistic title="命中数" value={stats.totalMatches.toLocaleString()} />
      </Card>
      <Card size="small">
        <Statistic title="当前 seeds/s" value={stats.currentSeedsPerSecond.toFixed(1)} />
      </Card>
      <Card size="small">
        <Statistic title="运行时长" value={formatRuntime(stats.startedAtMs)} />
      </Card>
    </section>
  );
}
