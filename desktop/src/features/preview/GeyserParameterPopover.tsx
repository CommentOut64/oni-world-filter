import React from "react";
import { Button, Descriptions, Space, Typography } from "antd";

import type {
  GeyserDetail,
  GeyserDetailsStatus,
  GeyserSummary,
} from "../../lib/contracts.ts";
import { useSearchStore } from "../../state/searchStore.ts";
import {
  formatGeyserDetailActiveWindow,
  formatGeyserDetailAverageYield,
  formatGeyserDetailCoords,
  formatGeyserDetailEruptionRate,
  formatGeyserDetailEruptionWindow,
  formatGeyserDetailMissingMessage,
  formatGeyserDetailTemperature,
  formatGeyserDetailTitle,
} from "./geyserDetailFormatters.ts";

export interface GeyserParameterAnchor {
  left: number;
  top: number;
}

const GEYSER_POPOVER_MAX_WIDTH = 320;
const GEYSER_POPOVER_CONTAINER_MARGIN = 24;

export function resolveGeyserPopoverWidth(
  popupContainer: Pick<HTMLElement, "clientWidth"> | null
): number {
  if (!popupContainer) {
    return GEYSER_POPOVER_MAX_WIDTH;
  }

  return Math.max(
    0,
    Math.min(GEYSER_POPOVER_MAX_WIDTH, popupContainer.clientWidth - GEYSER_POPOVER_CONTAINER_MARGIN)
  );
}

interface GeyserParameterPopoverProps {
  anchor: GeyserParameterAnchor | null;
  popupContainer: HTMLElement | null;
  geyser: GeyserSummary | null;
  geyserDetail: GeyserDetail | null;
  geyserDetailsStatus: GeyserDetailsStatus;
  geyserDetailsError: string | null;
  onClose: () => void;
  onRetry: () => void;
}

export default function GeyserParameterPopover({
  anchor,
  popupContainer,
  geyser,
  geyserDetail,
  geyserDetailsStatus,
  geyserDetailsError,
  onClose,
  onRetry,
}: GeyserParameterPopoverProps) {
  void React;
  const geysers = useSearchStore((state) => state.geysers);
  const overlayWidth = resolveGeyserPopoverWidth(popupContainer);

  if (!anchor || !geyser) {
    return null;
  }

  const title = formatGeyserDetailTitle(geyser, geysers);
  const detail = geyserDetail ?? null;
  const panelTitle = (
    <div className="geyser-parameter-popover-title">
      <span>{title}</span>
      <Space size={8}>
        {geyserDetailsStatus === "failed" ? (
          <Button htmlType="button" size="small" onClick={onRetry}>
            重试
          </Button>
        ) : null}
        <Button htmlType="button" size="small" onClick={onClose}>
          关闭
        </Button>
      </Space>
    </div>
  );
  const popoverContent = (
    <div className="geyser-parameter-popover">
      <Typography.Text className="geyser-parameter-coords">
        坐标 {formatGeyserDetailCoords(geyser)}
      </Typography.Text>

      {geyserDetailsStatus === "loading" || geyserDetailsStatus === "idle" ? (
        <Typography.Paragraph className="geyser-parameter-message">
          参数计算中...
        </Typography.Paragraph>
      ) : null}

      {geyserDetailsStatus === "failed" ? (
        <Typography.Paragraph className="geyser-parameter-message geyser-parameter-error">
          参数加载失败: {geyserDetailsError ?? "未知错误"}
        </Typography.Paragraph>
      ) : null}

      {geyserDetailsStatus === "ready" && detail && detail.hasParameters ? (
        <Descriptions
          className="geyser-parameter-grid"
          size="small"
          column={1}
          items={[
            {
              key: "temperature",
              label: "温度",
              children: formatGeyserDetailTemperature(detail),
            },
            {
              key: "eruption-rate",
              label: "喷发率",
              children: formatGeyserDetailEruptionRate(detail),
            },
            {
              key: "average-yield",
              label: "平均总产出",
              children: formatGeyserDetailAverageYield(detail),
            },
            {
              key: "eruption-window",
              label: "喷发期",
              children: formatGeyserDetailEruptionWindow(detail),
            },
            {
              key: "active-window",
              label: "活跃期",
              children: formatGeyserDetailActiveWindow(detail),
            },
          ]}
        />
      ) : null}

      {geyserDetailsStatus === "ready" && detail && !detail.hasParameters ? (
        <Typography.Paragraph className="geyser-parameter-message">
          {formatGeyserDetailMissingMessage(detail.parameterKind)}
        </Typography.Paragraph>
      ) : null}

      {geyserDetailsStatus === "ready" && !detail ? (
        <Typography.Paragraph className="geyser-parameter-message">
          当前喷口详情暂不可用。
        </Typography.Paragraph>
      ) : null}
    </div>
  );
  return (
    <div
      className="geyser-parameter-popover-overlay geyser-parameter-popover-floating ant-popover ant-popover-placement-rightTop"
      style={{
        left: anchor.left,
        top: anchor.top,
        width: overlayWidth,
        maxWidth: overlayWidth,
      }}
    >
      <div className="ant-popover-content">
        <div className="ant-popover-inner" role="tooltip">
          <div className="ant-popover-title">{panelTitle}</div>
          <div className="ant-popover-inner-content">{popoverContent}</div>
        </div>
      </div>
    </div>
  );
}
