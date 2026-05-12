import React, { useMemo, useState } from "react";
import { Button, Card, Typography } from "antd";
import { formatGeyserNameFromSummary, geyserKeyFromType } from "../../lib/displayResolvers";
import type { GeyserDetail, GeyserDetailsStatus, GeyserSummary } from "../../lib/contracts";
import { sortResolvedGeyserItems } from "../../lib/geyserOrdering.ts";
import { useSearchStore } from "../../state/searchStore";
import GeyserListHoverPopover from "./GeyserListHoverPopover";

interface GeyserListOverlayProps {
  geysersData: GeyserSummary[];
  geyserDetails: readonly GeyserDetail[];
  geyserDetailsStatus: GeyserDetailsStatus;
  popupContainer: HTMLElement | null;
  onClose: () => void;
}

export default function GeyserListOverlay({
  geysersData,
  geyserDetails,
  geyserDetailsStatus,
  popupContainer,
  onClose,
}: GeyserListOverlayProps) {
  void React;
  const geysers = useSearchStore((state) => state.geysers);
  const [activePopoverKey, setActivePopoverKey] = useState<string | null>(null);

  const sorted = useMemo(() => {
    return sortResolvedGeyserItems(
      geysersData.map((geyser, index) => ({
        geyser,
        index,
        name: formatGeyserNameFromSummary(geyser, geysers),
        key: `${geyser.id ?? geyser.type}-${index}`,
      })),
      (item) => ({
        geyserKey: item.geyser.id ?? geyserKeyFromType(item.geyser.type, geysers),
        name: item.name,
        disabled: false,
        stableKey: item.key,
      })
    );
  }, [geysersData, geysers]);

  return (
    <Card
      size="small"
      className="geyser-overlay"
      variant="outlined"
      title={`喷口列表 (${geysersData.length})`}
      extra={
        <Button className="geyser-overlay-close" htmlType="button" size="small" onClick={onClose}>
          关闭
        </Button>
      }
    >
      <div className="geyser-overlay-list">
        {sorted.map((item) => {
          const detail =
            geyserDetailsStatus === "ready"
              ? geyserDetails.find((geyserDetail) => geyserDetail.index === item.index) ?? null
              : null;
          const interactiveDetail = detail && detail.hasParameters ? detail : null;
          const isInteractive = interactiveDetail !== null;
          const isActive = isInteractive && activePopoverKey === item.key;
          const content = (
            <div
              key={item.key}
              className={`geyser-overlay-item${isInteractive ? " geyser-overlay-item-clickable" : ""}${isActive ? " geyser-overlay-item-active" : ""}`}
            >
              <Typography.Text>
                {item.name} @ ({item.geyser.x}, {item.geyser.y})
              </Typography.Text>
            </div>
          );

          if (isInteractive) {
            return (
              <GeyserListHoverPopover
                key={item.key}
                geyserDetail={interactiveDetail}
                popupContainer={popupContainer}
                open={isActive}
                onOpenChange={(open) => {
                  setActivePopoverKey(open ? item.key : null);
                }}
              >
                {content}
              </GeyserListHoverPopover>
            );
          }

          return content;
        })}
      </div>
    </Card>
  );
}
