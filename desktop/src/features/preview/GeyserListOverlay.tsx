import React, { useMemo } from "react";
import { Button, Card, Typography } from "antd";
import { formatGeyserNameFromSummary, geyserKeyFromType } from "../../lib/displayResolvers";
import type { GeyserSummary } from "../../lib/contracts";
import { sortResolvedGeyserItems } from "../../lib/geyserOrdering.ts";
import { useSearchStore } from "../../state/searchStore";

interface GeyserListOverlayProps {
  geysersData: GeyserSummary[];
  onClose: () => void;
}

export default function GeyserListOverlay({
  geysersData,
  onClose,
}: GeyserListOverlayProps) {
  void React;
  const geysers = useSearchStore((state) => state.geysers);

  const sorted = useMemo(() => {
    return sortResolvedGeyserItems(
      geysersData.map((geyser, index) => ({
        geyser,
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
        {sorted.map((item) => (
          <div key={item.key} className="geyser-overlay-item">
            <Typography.Text>
              {item.name} @ ({item.geyser.x}, {item.geyser.y})
            </Typography.Text>
          </div>
        ))}
      </div>
    </Card>
  );
}
