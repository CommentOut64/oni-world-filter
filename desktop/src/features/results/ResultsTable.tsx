import React, { useEffect, useMemo, useRef, useState } from "react";
import { Table } from "antd";

import { useSearchStore } from "../../state/searchStore";
import { createResultColumns } from "./resultColumns";

const DEFAULT_SCROLL_Y = 240;
const MIN_SCROLL_Y = 120;

export default function ResultsTable() {
  void React;
  const results = useSearchStore((state) => state.results);
  const geysers = useSearchStore((state) => state.geysers);
  const selectedSeed = useSearchStore((state) => state.selectedSeed);
  const selectSeed = useSearchStore((state) => state.selectSeed);
  const wrapperRef = useRef<HTMLElement | null>(null);
  const [scrollY, setScrollY] = useState(DEFAULT_SCROLL_Y);

  const columns = useMemo(() => createResultColumns(geysers), [geysers]);

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
      <Table
        className="results-table"
        rowKey="seed"
        size="small"
        pagination={false}
        scroll={{ y: scrollY }}
        columns={columns}
        dataSource={results}
        locale={{ emptyText: "暂无命中结果" }}
        rowClassName={(record) =>
          record.seed === selectedSeed ? "results-table-row-selected" : "results-table-row"
        }
        onRow={(record) => ({
          onClick: () => {
            selectSeed(record.seed);
          },
        })}
      />
    </section>
  );
}
