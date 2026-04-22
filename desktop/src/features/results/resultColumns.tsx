import type { TableColumnsType } from "antd";

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

export function createResultColumns(
  geysers: readonly GeyserOption[]
): TableColumnsType<SearchMatchSummary> {
  return [
    {
      key: "seed",
      dataIndex: "seed",
      title: "Seed",
      sorter: (lhs, rhs) => lhs.seed - rhs.seed,
      defaultSortOrder: "ascend",
      width: 120,
    },
    {
      key: "coord",
      dataIndex: "coord",
      title: "坐标码",
      ellipsis: true,
      width: 220,
    },
    {
      key: "geyserSummary",
      title: "喷口概览",
      render: (_, record) => formatGeyserSummary(record, geysers),
      ellipsis: true,
    },
    {
      key: "nearestDistance",
      dataIndex: "nearestDistance",
      title: "最近距离",
      sorter: (lhs, rhs) => {
        const leftValue = lhs.nearestDistance ?? Number.POSITIVE_INFINITY;
        const rightValue = rhs.nearestDistance ?? Number.POSITIVE_INFINITY;
        return leftValue - rightValue;
      },
      render: (value: SearchMatchSummary["nearestDistance"]) =>
        value === null ? "-" : value.toFixed(1),
      width: 120,
    },
    {
      key: "traitSummary",
      title: "Traits",
      render: (_, record) => formatTraitSummary(record.traits),
      ellipsis: true,
      width: 180,
    },
  ];
}
