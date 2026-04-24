import React, { useMemo } from "react";
import { Button, Card, Typography } from "antd";
import { formatGeyserNameFromSummary } from "../../lib/displayResolvers";
import type { GeyserSummary } from "../../lib/contracts";
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
    return [...geysersData]
      .map((geyser, index) => ({
        geyser,
        name: formatGeyserNameFromSummary(geyser, geysers),
        key: `${geyser.type}-${index}`,
      }))
      .sort((a, b) => a.name.localeCompare(b.name, "zh-Hans"));
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
