import type { PreviewPayload } from "../../lib/contracts";

interface PreviewDetailsProps {
  preview: PreviewPayload | null;
  hoveredRegion: { id: string; zoneType: number } | null;
  hoverGeyserIndex: number | null;
  selectedGeyserIndex: number | null;
}

function traitLabel(trait: number): string {
  return `trait#${trait}`;
}

export default function PreviewDetails({
  preview,
  hoveredRegion,
  hoverGeyserIndex,
  selectedGeyserIndex,
}: PreviewDetailsProps) {
  if (!preview) {
    return (
      <section className="preview-details">
        <h4>详情</h4>
        <p className="hint">请选择一条搜索结果加载预览。</p>
      </section>
    );
  }

  const focusIndex = selectedGeyserIndex ?? hoverGeyserIndex;
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
      <h4>详情</h4>
      <p>
        世界尺寸: {preview.summary.worldSize.w} x {preview.summary.worldSize.h}
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
      <p>
        当前板块:{" "}
        {hoveredRegion ? `${hoveredRegion.id} (zone#${hoveredRegion.zoneType})` : "-"}
      </p>
      <p>
        当前喷口:{" "}
        {focusGeyser
          ? `${focusGeyser.id ?? `type#${focusGeyser.type}`} (${focusGeyser.x}, ${
              focusGeyser.y
            })`
          : "-"}
      </p>
      <p>起点距离: {focusDistance === null ? "-" : focusDistance.toFixed(1)}</p>
      <div className="geyser-list">
        <strong>喷口列表</strong>
        <ul>
          {preview.summary.geysers.map((geyser, index) => (
            <li key={`${geyser.type}-${index}`}>
              {geyser.id ?? `type#${geyser.type}`} @ ({geyser.x}, {geyser.y})
            </li>
          ))}
        </ul>
      </div>
    </section>
  );
}
