import React from "react";
import { Button, Descriptions, Popover, Space, Typography } from "antd";
import type { TooltipRef } from "antd/es/tooltip";

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
  const popoverRef = React.useRef<TooltipRef | null>(null);
  const hasPopover = Boolean(anchor && geyser);
  const anchorLeft = anchor?.left ?? 0;
  const anchorTop = anchor?.top ?? 0;
  const geyserX = geyser?.x ?? 0;
  const geyserY = geyser?.y ?? 0;
  const overlayWidth = resolveGeyserPopoverWidth(popupContainer);

  React.useLayoutEffect(() => {
    if (!hasPopover) {
      return;
    }

    // rc-trigger 不会因为锚点 left/top 变化自动重排，这里显式请求重新对齐。
    popoverRef.current?.forceAlign?.();
  }, [anchorLeft, anchorTop, geyserX, geyserY, hasPopover, overlayWidth]);

  if (!anchor || !geyser) {
    return null;
  }

  const title = formatGeyserDetailTitle(geyser, geysers);
  const detail = geyserDetail ?? null;
  const popupContainerResolver = popupContainer ? () => popupContainer : undefined;

  const popoverTitle = (
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
    <Popover
      ref={popoverRef}
      open={Boolean(anchor && geyser)}
      trigger={[]}
      placement="rightTop"
      overlayClassName="geyser-parameter-popover-overlay"
      overlayStyle={{ width: overlayWidth, maxWidth: overlayWidth }}
      title={popoverTitle}
      content={popoverContent}
      getPopupContainer={popupContainerResolver}
    >
      <span className="geyser-parameter-anchor" style={{ left: anchor.left, top: anchor.top }} />
    </Popover>
  );
}
