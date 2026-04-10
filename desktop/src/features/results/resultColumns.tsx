import type { ColumnDef } from "@tanstack/react-table";

import type { SearchMatchSummary } from "../../lib/contracts";

function formatTraitSummary(traits: number[]): string {
  if (!traits.length) {
    return "-";
  }
  return traits.slice(0, 6).join(", ");
}

function formatGeyserSummary(match: SearchMatchSummary): string {
  if (!match.geysers.length) {
    return "-";
  }
  return match.geysers
    .slice(0, 4)
    .map((geyser) => geyser.id ?? `type#${geyser.type}`)
    .join(", ");
}

export const resultColumns: ColumnDef<SearchMatchSummary>[] = [
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
    cell: ({ row }) => formatGeyserSummary(row.original),
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
