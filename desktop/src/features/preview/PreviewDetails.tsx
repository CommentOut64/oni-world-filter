import React from "react";
import { Card, Descriptions, Empty, Tag, Typography } from "antd";

import { formatGeyserNameFromSummary, formatZoneTypeName } from "../../lib/displayResolvers";
import type { PreviewPayload } from "../../lib/contracts";
import { useSearchStore } from "../../state/searchStore";

interface PreviewDetailsProps {
  preview: PreviewPayload | null;
  hoveredRegion: { id: string; zoneType: number } | null;
  selectedRegion: { id: string; zoneType: number } | null;
  hoverGeyserIndex: number | null;
  selectedGeyserIndex: number | null;
}

function traitLabel(trait: number): string {
  return `trait#${trait}`;
}

export default function PreviewDetails({
  preview,
  hoveredRegion,
  selectedRegion,
  hoverGeyserIndex,
  selectedGeyserIndex,
}: PreviewDetailsProps) {
  void React;
  const geysers = useSearchStore((state) => state.geysers);
  const selectedSeed = useSearchStore((state) => state.selectedSeed);

  if (!preview) {
    return (
      <Card size="small" className="preview-details preview-details-empty">
        <Empty description="请选择一条搜索结果加载预览。" />
      </Card>
    );
  }

  const focusIndex = selectedGeyserIndex ?? hoverGeyserIndex;
  const focusRegion = hoveredRegion ?? selectedRegion;
  const focusGeyser =
    focusIndex === null ? null : preview.summary.geysers[focusIndex] ?? null;
  const focusDistance =
    focusGeyser === null
      ? null
      : Number(
          Math.sqrt(
            (focusGeyser.x - preview.summary.start.x) ** 2 +
              (focusGeyser.y - preview.summary.start.y) ** 2
          ).toFixed(1)
        );

  return (
    <Card
      size="small"
      className="preview-details"
      title="当前预览详情"
      extra={
        selectedSeed !== null && selectedSeed !== preview.summary.seed ? (
          <Tag color="warning">选中 {selectedSeed}，预览仍为 {preview.summary.seed}</Tag>
        ) : (
          <Tag color="blue">Seed {preview.summary.seed}</Tag>
        )
      }
    >
      <Descriptions
        className="preview-details-grid"
        size="small"
        column={3}
        items={[
          {
            key: "world-size",
            label: "尺寸",
            children: `${preview.summary.worldSize.w} x ${preview.summary.worldSize.h}`,
          },
          {
            key: "start",
            label: "起点",
            children: `(${preview.summary.start.x}, ${preview.summary.start.y})`,
          },
          {
            key: "traits",
            label: "Traits",
            children: preview.summary.traits.length
              ? preview.summary.traits.map(traitLabel).join(", ")
              : "-",
          },
          {
            key: "region",
            label: "板块",
            children: focusRegion ? (
              <Tag>{formatZoneTypeName(focusRegion.zoneType)}</Tag>
            ) : (
              "-"
            ),
          },
          {
            key: "geyser",
            label: "喷口",
            children: focusGeyser ? (
              <Typography.Text>
                {formatGeyserNameFromSummary(focusGeyser, geysers)} ({focusGeyser.x}, {focusGeyser.y})
              </Typography.Text>
            ) : (
              "-"
            ),
          },
          {
            key: "distance",
            label: "距起点",
            children: focusDistance === null ? "-" : focusDistance.toFixed(1),
          },
        ]}
      />
    </Card>
  );
}
