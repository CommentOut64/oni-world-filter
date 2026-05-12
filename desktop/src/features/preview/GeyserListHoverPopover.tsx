import React from "react";
import { Popover, Typography } from "antd";

import type { GeyserDetail } from "../../lib/contracts.ts";
import {
  formatGeyserDetailActiveWindow,
  formatGeyserDetailAverageYield,
  formatGeyserDetailEruptionRate,
  formatGeyserDetailEruptionWindow,
} from "./geyserDetailFormatters.ts";

const GEYSER_LIST_HOVER_POPOVER_MAX_WIDTH = 220;
const GEYSER_LIST_HOVER_POPOVER_CONTAINER_MARGIN = 24;

export function resolveGeyserListHoverPopoverWidth(
  popupContainer: Pick<HTMLElement, "clientWidth"> | null
): number {
  if (!popupContainer) {
    return GEYSER_LIST_HOVER_POPOVER_MAX_WIDTH;
  }

  return Math.max(
    0,
    Math.min(
      GEYSER_LIST_HOVER_POPOVER_MAX_WIDTH,
      popupContainer.clientWidth - GEYSER_LIST_HOVER_POPOVER_CONTAINER_MARGIN
    )
  );
}

interface GeyserListHoverPopoverProps {
  geyserDetail: GeyserDetail;
  popupContainer: HTMLElement | null;
  open: boolean;
  onOpenChange: (open: boolean) => void;
  children: React.ReactNode;
}

export default function GeyserListHoverPopover({
  geyserDetail,
  popupContainer,
  open,
  onOpenChange,
  children,
}: GeyserListHoverPopoverProps) {
  void React;
  const overlayWidth = resolveGeyserListHoverPopoverWidth(popupContainer);
  const popupContainerResolver = popupContainer ? () => popupContainer : undefined;

  return (
    <Popover
      trigger="click"
      open={open}
      onOpenChange={onOpenChange}
      placement="right"
      overlayClassName="geyser-list-hover-popover-overlay"
      overlayStyle={{ width: overlayWidth, maxWidth: overlayWidth }}
      content={
        <div className="geyser-list-hover-popover">
          <div className="geyser-list-hover-popover-row">
            <Typography.Text className="geyser-list-hover-popover-label">喷发率 ：</Typography.Text>
            <Typography.Text className="geyser-list-hover-popover-value">
              {formatGeyserDetailEruptionRate(geyserDetail)}
            </Typography.Text>
          </div>
          <div className="geyser-list-hover-popover-row">
            <Typography.Text className="geyser-list-hover-popover-label">平均总产出 ：</Typography.Text>
            <Typography.Text className="geyser-list-hover-popover-value">
              {formatGeyserDetailAverageYield(geyserDetail)}
            </Typography.Text>
          </div>
          <div className="geyser-list-hover-popover-row">
            <Typography.Text className="geyser-list-hover-popover-label">喷发期 ：</Typography.Text>
            <Typography.Text className="geyser-list-hover-popover-value">
              {formatGeyserDetailEruptionWindow(geyserDetail)}
            </Typography.Text>
          </div>
          <div className="geyser-list-hover-popover-row">
            <Typography.Text className="geyser-list-hover-popover-label">活跃期 ：</Typography.Text>
            <Typography.Text className="geyser-list-hover-popover-value">
              {formatGeyserDetailActiveWindow(geyserDetail)}
            </Typography.Text>
          </div>
        </div>
      }
      getPopupContainer={popupContainerResolver}
    >
      {children}
    </Popover>
  );
}
