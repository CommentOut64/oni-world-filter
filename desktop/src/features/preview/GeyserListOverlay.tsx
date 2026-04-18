import { useMemo } from "react";
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
    <div className="geyser-overlay">
      <div className="geyser-overlay-header">
        <strong>喷口列表 ({geysersData.length})</strong>
        <button className="geyser-overlay-close" onClick={onClose}>
          关闭
        </button>
      </div>
      <ul className="geyser-overlay-list">
        {sorted.map((item) => (
          <li key={item.key}>
            {item.name} @ ({item.geyser.x}, {item.geyser.y})
          </li>
        ))}
      </ul>
    </div>
  );
}
