import { useSearchStore } from "../../state/searchStore";

export default function ResultToolbar() {
  const isSearching = useSearchStore((state) => state.isSearching);
  const resultsCount = useSearchStore((state) => state.results.length);
  const selectedSeed = useSearchStore((state) => state.selectedSeed);

  return (
    <section className="result-toolbar">
      <span>结果总数: {resultsCount.toLocaleString()}</span>
      <span>状态: {isSearching ? "流式接收中" : "已停止"}</span>
      <span>当前选中: {selectedSeed === null ? "-" : selectedSeed}</span>
    </section>
  );
}
