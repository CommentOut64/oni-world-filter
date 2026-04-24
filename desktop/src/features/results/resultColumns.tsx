import { CopyOutlined } from "@ant-design/icons";
import { Button, message } from "antd";
import type { TableColumnsType } from "antd";

import type { GeyserOption, SearchMatchSummary } from "../../lib/contracts";
import { formatGeyserCountSummary } from "./resultSummary";

export interface CoordCopyDeps {
  writeText: (value: string) => Promise<void>;
  notifySuccess: (content: string) => void;
}

function createCoordCopyDeps(): CoordCopyDeps {
  return {
    writeText: (value) => navigator.clipboard.writeText(value),
    notifySuccess: (content) => {
      void message.success(content);
    },
  };
}

function formatGeyserSummary(
  match: SearchMatchSummary,
  geysers: readonly GeyserOption[],
  prioritizedGeyserKeys: readonly string[]
): string {
  return formatGeyserCountSummary(match.geysers, geysers, 4, prioritizedGeyserKeys);
}

export async function copyCoordCode(
  coord: string,
  deps: CoordCopyDeps = createCoordCopyDeps()
): Promise<void> {
  await deps.writeText(coord);
  deps.notifySuccess("复制成功");
}

export function createResultColumns(
  geysers: readonly GeyserOption[],
  prioritizedGeyserKeys: readonly string[] = []
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
      key: "copyCoord",
      title: "",
      width: 56,
      align: "center",
      render: (_, record) => (
        <Button
          type="text"
          size="small"
          aria-label={`复制坐标码 ${record.coord}`}
          icon={<CopyOutlined />}
          onClick={(event) => {
            event.stopPropagation();
            void copyCoordCode(record.coord);
          }}
        />
      ),
    },
    {
      key: "geyserSummary",
      title: "喷口概览",
      render: (_, record) => formatGeyserSummary(record, geysers, prioritizedGeyserKeys),
      ellipsis: true,
    },
    {
      key: "nearestDistance",
      dataIndex: "nearestDistance",
      title: "喷口最小距离",
      sorter: (lhs, rhs) => {
        const leftValue = lhs.nearestDistance ?? Number.POSITIVE_INFINITY;
        const rightValue = rhs.nearestDistance ?? Number.POSITIVE_INFINITY;
        return leftValue - rightValue;
      },
      render: (value: SearchMatchSummary["nearestDistance"]) =>
        value === null ? "-" : value.toFixed(1),
      width: 120,
    },
  ];
}
