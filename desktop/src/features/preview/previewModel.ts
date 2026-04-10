import type { PreviewPayload } from "../../lib/contracts";

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

function centroid(vertices: [number, number][]): { x: number; y: number } {
  if (!vertices.length) {
    return { x: 0, y: 0 };
  }
  let sumX = 0;
  let sumY = 0;
  for (const [x, y] of vertices) {
    sumX += x;
    sumY += y;
  }
  return { x: sumX / vertices.length, y: sumY / vertices.length };
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

export function toPreviewViewModel(preview: PreviewPayload): PreviewViewModel {
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
    labels.push({
      id: `${id}-label`,
      x: center.x,
      y: center.y,
      text: `zone#${polygon.zoneType}`,
      kind: "region",
    });
  });

  const start = preview.summary.start;
  const geysers: PreviewGeyserMarker[] = preview.summary.geysers.map((item, index) => {
    const dx = item.x - start.x;
    const dy = item.y - start.y;
    return {
      index,
      id: item.id ?? `type#${item.type}`,
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
