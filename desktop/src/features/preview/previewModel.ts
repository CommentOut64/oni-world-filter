import { formatGeyserNameFromSummary, formatPlayerBiomeNameByZoneType } from "../../lib/displayResolvers";
import type { GeyserOption, PreviewPayload } from "../../lib/contracts";

export interface PreviewRegion {
  id: string;
  zoneType: number;
  points: number[];
  centroid: { x: number; y: number };
}

export interface PreviewRegionBounds {
  regionId: string;
  minX: number;
  maxX: number;
  minY: number;
  maxY: number;
}

export interface PreviewGeyserMarker {
  index: number;
  id: string;
  x: number;
  y: number;
  distanceToStart: number;
}

export interface PreviewLabelCandidate {
  id: string;
  x: number;
  y: number;
  text: string;
  kind: "start" | "geyser" | "region";
}

export interface PreviewViewModel {
  regions: PreviewRegion[];
  regionBounds: PreviewRegionBounds[];
  geysers: PreviewGeyserMarker[];
  startMarker: { x: number; y: number; text: string };
  worldBounds: { width: number; height: number };
  labelCandidates: PreviewLabelCandidate[];
}

function flattenPolygon(vertices: [number, number][]): number[] {
  const points: number[] = [];
  for (const [x, y] of vertices) {
    points.push(x, y);
  }
  return points;
}

// 基于 Shoelace 公式的多边形几何质心，对不规则形状比顶点平均值准确得多
function centroid(vertices: [number, number][]): { x: number; y: number } {
  if (!vertices.length) {
    return { x: 0, y: 0 };
  }
  if (vertices.length < 3) {
    let sumX = 0;
    let sumY = 0;
    for (const [x, y] of vertices) {
      sumX += x;
      sumY += y;
    }
    return { x: sumX / vertices.length, y: sumY / vertices.length };
  }

  let area = 0;
  let cx = 0;
  let cy = 0;
  for (let i = 0; i < vertices.length; i++) {
    const [x0, y0] = vertices[i];
    const [x1, y1] = vertices[(i + 1) % vertices.length];
    const cross = x0 * y1 - x1 * y0;
    area += cross;
    cx += (x0 + x1) * cross;
    cy += (y0 + y1) * cross;
  }

  if (Math.abs(area) < 1e-10) {
    // 退化多边形，回退到顶点平均
    let sumX = 0;
    let sumY = 0;
    for (const [x, y] of vertices) {
      sumX += x;
      sumY += y;
    }
    return { x: sumX / vertices.length, y: sumY / vertices.length };
  }

  area *= 0.5;
  cx /= 6 * area;
  cy /= 6 * area;
  return { x: cx, y: cy };
}

function regionBounds(regionId: string, points: number[]): PreviewRegionBounds {
  let minX = Number.POSITIVE_INFINITY;
  let maxX = Number.NEGATIVE_INFINITY;
  let minY = Number.POSITIVE_INFINITY;
  let maxY = Number.NEGATIVE_INFINITY;

  for (let i = 0; i < points.length; i += 2) {
    minX = Math.min(minX, points[i]);
    maxX = Math.max(maxX, points[i]);
    minY = Math.min(minY, points[i + 1]);
    maxY = Math.max(maxY, points[i + 1]);
  }

  return { regionId, minX, maxX, minY, maxY };
}

export function toPreviewViewModel(
  preview: PreviewPayload,
  geyserOptions: readonly GeyserOption[]
): PreviewViewModel {
  const regions: PreviewRegion[] = [];
  const bounds: PreviewRegionBounds[] = [];
  const labels: PreviewLabelCandidate[] = [];

  preview.polygons.forEach((polygon, index) => {
    if (!polygon.vertices.length) {
      return;
    }
    const id = `region-${index}`;
    const points = flattenPolygon(polygon.vertices);
    const center = centroid(polygon.vertices);
    regions.push({
      id,
      zoneType: polygon.zoneType,
      points,
      centroid: center,
    });
    bounds.push(regionBounds(id, points));
    const regionText = formatPlayerBiomeNameByZoneType(polygon.zoneType);
    if (regionText) {
      labels.push({
        id: `${id}-label`,
        x: center.x,
        y: center.y,
        text: regionText,
        kind: "region",
      });
    }
  });

  const start = preview.summary.start;
  const geysers: PreviewGeyserMarker[] = preview.summary.geysers.map((item, index) => {
    const dx = item.x - start.x;
    const dy = item.y - start.y;
    return {
      index,
      id: formatGeyserNameFromSummary(item, geyserOptions),
      x: item.x,
      y: item.y,
      distanceToStart: Number(Math.sqrt(dx * dx + dy * dy).toFixed(1)),
    };
  });

  labels.push({
    id: "start-marker-label",
    x: start.x,
    y: start.y,
    text: "起点",
    kind: "start",
  });

  for (const geyser of geysers) {
    labels.push({
      id: `geyser-${geyser.index}-label`,
      x: geyser.x,
      y: geyser.y,
      text: geyser.id,
      kind: "geyser",
    });
  }

  return {
    regions,
    regionBounds: bounds,
    geysers,
    startMarker: {
      x: start.x,
      y: start.y,
      text: "起点",
    },
    worldBounds: {
      width: preview.summary.worldSize.w,
      height: preview.summary.worldSize.h,
    },
    labelCandidates: labels,
  };
}
