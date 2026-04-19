import { useSearchStore } from "../../state/searchStore";
import { openHostDebugWindow } from "../../lib/hostDebugWindow";

export default function ResultToolbar() {
  const isSearching = useSearchStore((state) => state.isSearching);
  const resultsCount = useSearchStore((state) => state.results.length);
  const selectedSeed = useSearchStore((state) => state.selectedSeed);
  const lastSubmittedRequest = useSearchStore((state) => state.lastSubmittedRequest);
  const lastHostDebugMessages = useSearchStore((state) => state.lastHostDebugMessages);

  async function handleOpenHostDebugWindow(): Promise<void> {
    await openHostDebugWindow({
      request: lastSubmittedRequest,
      messages: lastHostDebugMessages,
    });
  }

  return (
    <>
      <section className="result-toolbar">
        <span>结果总数: {resultsCount.toLocaleString()}</span>
        <span>状态: {isSearching ? "流式接收中" : "已停止"}</span>
        <span>当前选中: {selectedSeed === null ? "-" : selectedSeed}</span>
        <button type="button" onClick={() => void handleOpenHostDebugWindow()}>
          打开调试窗口
        </button>
      </section>
    </>
  );
}
