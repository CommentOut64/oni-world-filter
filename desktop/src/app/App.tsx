import { useEffect } from "react";

import DesktopShell from "../components/layout/DesktopShell";
import PreviewPane from "../features/preview/PreviewPane";
import ResultSummaryCards from "../features/results/ResultSummaryCards";
import ResultsTable from "../features/results/ResultsTable";
import ResultToolbar from "../features/results/ResultToolbar";
import SearchPanel from "../features/search/SearchPanel";
import { disposeSidecarListener, useSearchStore } from "../state/searchStore";

function App() {
  const bootstrap = useSearchStore((state) => state.bootstrap);
  const bindSidecarEvents = useSearchStore((state) => state.bindSidecarEvents);
  const lastError = useSearchStore((state) => state.lastError);
  const clearError = useSearchStore((state) => state.clearError);

  useEffect(() => {
    void bootstrap();
    void bindSidecarEvents();
    return () => {
      disposeSidecarListener();
    };
  }, [bootstrap, bindSidecarEvents]);

  return (
    <DesktopShell
      left={<SearchPanel />}
      center={
        <section className="results-pane">
          <header>
            <h3>结果列表</h3>
            <p>搜索命中结果实时流式追加，支持选中后触发右侧预览。</p>
          </header>
          {lastError ? (
            <p className="error-inline" onClick={clearError}>
              后端事件: {lastError}
            </p>
          ) : null}
          <ResultSummaryCards />
          <ResultToolbar />
          <ResultsTable />
        </section>
      }
      right={<PreviewPane />}
    />
  );
}

export default App;
