import { Alert, Button } from "antd";
import { useEffect, useState } from "react";

import DesktopShell from "../components/layout/DesktopShell";
import HostDebugWindow from "../features/debug/HostDebugWindow";
import PreviewPane from "../features/preview/PreviewPane";
import ResultSummaryCards from "../features/results/ResultSummaryCards";
import ResultsTable from "../features/results/ResultsTable";
import ResultToolbar from "../features/results/ResultToolbar";
import SearchPanel from "../features/search/SearchPanel";
import { isHostDebugWindow } from "../lib/hostDebugWindow";
import { disposeSidecarListener, useSearchStore } from "../state/searchStore";

function App() {
  if (isHostDebugWindow()) {
    return <HostDebugWindow />;
  }

  const bootstrap = useSearchStore((state) => state.bootstrap);
  const bindSidecarEvents = useSearchStore((state) => state.bindSidecarEvents);
  const lastError = useSearchStore((state) => state.lastError);
  const clearError = useSearchStore((state) => state.clearError);
  const results = useSearchStore((state) => state.results);
  const isSearching = useSearchStore((state) => state.isSearching);
  const hasResults = results.length > 0 || isSearching;
  const [activePage, setActivePage] = useState<"search" | "results">(() =>
    hasResults ? "results" : "search"
  );

  useEffect(() => {
    void bootstrap();
    void bindSidecarEvents();
    return () => {
      disposeSidecarListener();
    };
  }, [bootstrap, bindSidecarEvents]);
  return (
    <DesktopShell className={activePage === "results" ? "desktop-shell-results" : "desktop-shell-search"}>
      {activePage === "results" ? (
        <section className="results-screen">
          <section className="panel panel-results-list">
            <section className="results-pane">
              <header className="results-pane-header">
                <Button type="default" className="back-button" onClick={() => setActivePage("search")}>
                  返回参数页
                </Button>
                <div>
                  <h3>结果列表</h3>
                </div>
              </header>
              {lastError ? (
                <Alert
                  className="shell-inline-alert"
                  type="error"
                  showIcon
                  title={`后端事件: ${lastError}`}
                  closable
                  onClose={clearError}
                />
              ) : null}
              <ResultSummaryCards />
              <ResultToolbar />
              <ResultsTable />
            </section>
          </section>
          <aside className="panel panel-results-preview">
            <PreviewPane />
          </aside>
        </section>
      ) : (
        <section className="search-screen">
          <section className="panel panel-search-page">
            <SearchPanel
              onSearchStarted={() => {
                setActivePage("results");
              }}
              onViewResults={() => {
                setActivePage("results");
              }}
            />
          </section>
        </section>
      )}
    </DesktopShell>
  );
}

export default App;
