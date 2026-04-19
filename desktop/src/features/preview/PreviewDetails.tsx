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
  const geysers = useSearchStore((state) => state.geysers);
  const selectedSeed = useSearchStore((state) => state.selectedSeed);

  if (!preview) {
    return (
      <section className="preview-details">
        <p className="hint">请选择一条搜索结果加载预览。</p>
      </section>
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
    <section className="preview-details">
      <div className="preview-details-grid">
        <div className="preview-details-col">
          <p>
            Seed:{" "}
            {selectedSeed !== null && selectedSeed !== preview.summary.seed
              ? `${preview.summary.seed} (选中 ${selectedSeed}，未同步)`
              : preview.summary.seed}
          </p>
          <p>
            尺寸: {preview.summary.worldSize.w} x {preview.summary.worldSize.h}
          </p>
          <p>
            起点: ({preview.summary.start.x}, {preview.summary.start.y})
          </p>
          <p>
            Traits:{" "}
            {preview.summary.traits.length
              ? preview.summary.traits.map(traitLabel).join(", ")
              : "-"}
          </p>
        </div>
        <div className="preview-details-col">
          <p>
            板块:{" "}
            {focusRegion ? formatZoneTypeName(focusRegion.zoneType) : "-"}
          </p>
          <p>
            喷口:{" "}
            {focusGeyser
              ? `${formatGeyserNameFromSummary(focusGeyser, geysers)} (${focusGeyser.x}, ${focusGeyser.y})`
              : "-"}
          </p>
          <p>距起点: {focusDistance === null ? "-" : focusDistance.toFixed(1)}</p>
        </div>
      </div>
    </section>
  );
}
