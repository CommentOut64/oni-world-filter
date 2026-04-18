import {
  flexRender,
  getCoreRowModel,
  getSortedRowModel,
  type SortingState,
  useReactTable,
} from "@tanstack/react-table";
import { useMemo, useState } from "react";

import { useSearchStore } from "../../state/searchStore";
import { createResultColumns } from "./resultColumns";

export default function ResultsTable() {
  const results = useSearchStore((state) => state.results);
  const geysers = useSearchStore((state) => state.geysers);
  const selectedSeed = useSearchStore((state) => state.selectedSeed);
  const selectSeed = useSearchStore((state) => state.selectSeed);

  const [sorting, setSorting] = useState<SortingState>([
    { id: "seed", desc: false },
  ]);
  const columns = useMemo(() => createResultColumns(geysers), [geysers]);

  const table = useReactTable({
    data: results,
    columns,
    state: { sorting },
    onSortingChange: setSorting,
    getCoreRowModel: getCoreRowModel(),
    getSortedRowModel: getSortedRowModel(),
  });

  return (
    <section className="results-table-wrap">
      <table className="results-table">
        <thead>
          {table.getHeaderGroups().map((headerGroup) => (
            <tr key={headerGroup.id}>
              {headerGroup.headers.map((header) => (
                <th key={header.id}>
                  {header.isPlaceholder ? null : (
                    <button
                      type="button"
                      className="sort-button"
                      onClick={header.column.getToggleSortingHandler()}
                    >
                      {flexRender(
                        header.column.columnDef.header,
                        header.getContext()
                      )}
                    </button>
                  )}
                </th>
              ))}
            </tr>
          ))}
        </thead>
        <tbody>
          {table.getRowModel().rows.map((row) => (
            <tr
              key={row.id}
              className={row.original.seed === selectedSeed ? "selected" : undefined}
              onClick={() => {
                selectSeed(row.original.seed);
              }}
            >
              {row.getVisibleCells().map((cell) => (
                <td key={cell.id}>
                  {flexRender(cell.column.columnDef.cell, cell.getContext())}
                </td>
              ))}
            </tr>
          ))}
          {table.getRowModel().rows.length === 0 ? (
            <tr>
              <td colSpan={columns.length} className="empty">
                暂无命中结果
              </td>
            </tr>
          ) : null}
        </tbody>
      </table>
    </section>
  );
}
