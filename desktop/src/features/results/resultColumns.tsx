import type { ColumnDef } from "@tanstack/react-table";

import type { GeyserOption, SearchMatchSummary } from "../../lib/contracts";
import { formatGeyserCountSummary } from "./resultSummary";

function formatTraitSummary(traits: number[]): string {
  if (!traits.length) {
    return "-";
  }
  return traits.slice(0, 6).join(", ");
}

function formatGeyserSummary(match: SearchMatchSummary, geysers: readonly GeyserOption[]): string {
  return formatGeyserCountSummary(match.geysers, geysers);
}

export function createResultColumns(geysers: readonly GeyserOption[]): ColumnDef<SearchMatchSummary>[] {
  return [
    {
      accessorKey: "seed",
      header: "Seed",
    },
    {
      accessorKey: "coord",
      header: "坐标码",
    },
    {
      id: "geyserSummary",
      header: "喷口概览",
      cell: ({ row }) => formatGeyserSummary(row.original, geysers),
    },
    {
      accessorKey: "nearestDistance",
      header: "最近距离",
      cell: ({ row }) =>
        row.original.nearestDistance === null ? "-" : row.original.nearestDistance.toFixed(1),
    },
    {
      id: "traitSummary",
      header: "Traits",
      cell: ({ row }) => formatTraitSummary(row.original.traits),
    },
  ];
}
