import React from "react";
import { Button } from "antd";

import { openHostDebugWindow } from "../../lib/hostDebugWindow";
import { usePreviewStore } from "../../state/previewStore";
import { useSearchStore } from "../../state/searchStore";

export default function ResultToolbar() {
  void React;
  const isSearching = useSearchStore((state) => state.isSearching);
  const isCancelling = useSearchStore((state) => state.isCancelling);
  const cancelSearchJob = useSearchStore((state) => state.cancelSearchJob);
  const clearResults = useSearchStore((state) => state.clearResults);
  const lastSubmittedRequest = useSearchStore((state) => state.lastSubmittedRequest);
  const lastHostDebugMessages = useSearchStore((state) => state.lastHostDebugMessages);
  const clearPreview = usePreviewStore((state) => state.clear);

  return (
    <section className="result-toolbar">
      <div className="result-toolbar-actions">
        <Button
          htmlType="button"
          onClick={() => {
            void cancelSearchJob();
          }}
          disabled={!isSearching || isCancelling}
          loading={isCancelling}
        >
          {isCancelling ? "取消中..." : "取消搜索"}
        </Button>
        <Button
          htmlType="button"
          onClick={() => {
            clearResults();
            clearPreview();
          }}
        >
          清空结果
        </Button>
        <Button
          htmlType="button"
          onClick={() => {
            void openHostDebugWindow({
              request: lastSubmittedRequest,
              messages: lastHostDebugMessages,
            });
          }}
        >
          打开调试窗口
        </Button>
      </div>
    </section>
  );
}
