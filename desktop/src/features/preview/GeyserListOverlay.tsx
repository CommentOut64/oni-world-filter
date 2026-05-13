import React, { useMemo, useState } from "react";
import { Button, Card, Typography } from "antd";
import { formatGeyserNameFromSummary, geyserKeyFromType } from "../../lib/displayResolvers";
import type {
  GeyserDetail,
  GeyserDetailsStatus,
  GeyserSummary,
  PreviewTarget,
} from "../../lib/contracts";
import { sortResolvedGeyserItems } from "../../lib/geyserOrdering.ts";
import { useSearchStore } from "../../state/searchStore";
import GeyserListHoverPopover from "./GeyserListHoverPopover";

interface GeyserListOverlayProps {
  activeTarget: PreviewTarget;
  geysersData: GeyserSummary[];
  geyserDetails: readonly GeyserDetail[];
  geyserDetailsStatus: GeyserDetailsStatus;
  popupContainer: HTMLElement | null;
  onClose: () => void;
}

export default function GeyserListOverlay({
  activeTarget,
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
          const canShowPopover = activeTarget === "primary" && interactiveDetail !== null;
          const isActive = canShowPopover && activePopoverKey === item.key;
          const content = (
            <button
              type="button"
              key={item.key}
              className={`geyser-overlay-item${isInteractive ? " geyser-overlay-item-clickable" : ""}${isActive ? " geyser-overlay-item-active" : ""}`}
              aria-disabled={!isInteractive}
              disabled={!isInteractive}
            >
              <Typography.Text>
                {item.name} @ ({item.geyser.x}, {item.geyser.y})
              </Typography.Text>
            </button>
          );

          if (isInteractive) {
            return (
              <GeyserListHoverPopover
                key={item.key}
                geyserDetail={interactiveDetail}
                popupContainer={popupContainer}
                open={canShowPopover ? isActive : false}
                onOpenChange={(open) => {
                  if (!canShowPopover) {
                    setActivePopoverKey(null);
                    return;
                  }
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
