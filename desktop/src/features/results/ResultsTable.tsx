import React, { useEffect, useMemo, useRef, useState } from "react";
import { Table } from "antd";

import type { SearchMatchSummary } from "../../lib/contracts";
import { useSearchStore } from "../../state/searchStore";
import { createResultColumns } from "./resultColumns";

const DEFAULT_SCROLL_Y = 240;
const MIN_SCROLL_Y = 120;

function buildPrioritizedGeyserKeys(
  lastSubmittedRequest: ReturnType<typeof useSearchStore.getState>["lastSubmittedRequest"]
): string[] {
  if (!lastSubmittedRequest) {
    return [];
  }

  const orderedKeys = [
    ...lastSubmittedRequest.constraints.required,
    ...lastSubmittedRequest.constraints.forbidden,
    ...lastSubmittedRequest.constraints.distance.map((item) => item.geyser),
    ...lastSubmittedRequest.constraints.count.map((item) => item.geyser),
  ];

  return [...new Set(orderedKeys)];
}

export default function ResultsTable() {
  void React;
  const results = useSearchStore((state) => state.results);
  const geysers = useSearchStore((state) => state.geysers);
  const selectedSeed = useSearchStore((state) => state.selectedSeed);
  const selectSeed = useSearchStore((state) => state.selectSeed);
  const lastSubmittedRequest = useSearchStore((state) => state.lastSubmittedRequest);
  const wrapperRef = useRef<HTMLElement | null>(null);
  const [scrollY, setScrollY] = useState(DEFAULT_SCROLL_Y);

  const prioritizedGeyserKeys = useMemo(
    () => buildPrioritizedGeyserKeys(lastSubmittedRequest),
    [lastSubmittedRequest]
  );
  const columns = useMemo(
    () => createResultColumns(geysers, prioritizedGeyserKeys),
    [geysers, prioritizedGeyserKeys]
  );

  useEffect(() => {
    const wrapper = wrapperRef.current;
    if (!wrapper || typeof ResizeObserver === "undefined") {
      return undefined;
    }

    const syncScrollY = () => {
      const wrapperHeight = wrapper.clientHeight;
      if (!wrapperHeight) {
        return;
      }

      const headerHeight = wrapper.querySelector<HTMLElement>(".ant-table-header")?.offsetHeight ?? 0;
      const nextScrollY = Math.max(MIN_SCROLL_Y, Math.floor(wrapperHeight - headerHeight));
      setScrollY((current) => (current === nextScrollY ? current : nextScrollY));
    };

    syncScrollY();

    const resizeObserver = new ResizeObserver(() => {
      syncScrollY();
    });

    resizeObserver.observe(wrapper);

    const header = wrapper.querySelector<HTMLElement>(".ant-table-header");
    if (header) {
      resizeObserver.observe(header);
    }

    return () => {
      resizeObserver.disconnect();
    };
  }, [columns]);

  return (
    <section ref={wrapperRef} className="results-table-wrap">
      <Table<SearchMatchSummary>
        className="results-table"
        rowKey="seed"
        virtual
        size="small"
        pagination={false}
        scroll={{ y: scrollY }}
        columns={columns}
        dataSource={results}
        locale={{ emptyText: "暂无命中结果" }}
        rowClassName={(record) => (record.seed === selectedSeed ? "ant-table-row-selected" : "")}
        onRow={(record) => ({
          onClick: () => {
            selectSeed(record.seed);
          },
        })}
      />
    </section>
  );
}
