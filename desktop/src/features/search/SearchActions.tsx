import React from "react";
import { Button } from "antd";

interface SearchActionsProps {
  isSearching: boolean;
  isBusy?: boolean;
  hasResults: boolean;
  resultsCount: number;
  onViewResults: () => void;
}

export default function SearchActions({
  isSearching,
  isBusy = false,
  hasResults,
  resultsCount,
  onViewResults,
}: SearchActionsProps) {
  void React;
  const showPrimaryBusyState = isSearching || isBusy;
  const disablePrimaryAction = showPrimaryBusyState;

  return (
    <section className="search-actions">
      <div className="search-actions-row">
        <Button
          htmlType="submit"
          type="primary"
          className="search-action-primary"
          loading={showPrimaryBusyState}
          disabled={disablePrimaryAction}
        >
          {isSearching ? "搜索进行中..." : isBusy ? "正在分析..." : "开始搜索"}
        </Button>
        <Button
          htmlType="button"
          className="search-action-secondary"
          onClick={onViewResults}
          disabled={!hasResults}
        >
          查看结果{isSearching ? "（搜索中…）" : hasResults ? ` (${resultsCount})` : ""}
        </Button>
      </div>
    </section>
  );
}
