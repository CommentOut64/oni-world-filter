import type { PreviewLabelCandidate } from "./previewModel.ts";

export const LABEL_FONT_PX = 11;
const LABEL_CHAR_WIDTH = 0.55;
const LABEL_PADDING = 4;

interface LabelRect {
  left: number;
  right: number;
  top: number;
  bottom: number;
}

const NUDGE_OFFSETS: [number, number][] = [
  [0, 0],
  [0, -20],
  [0, 20],
  [-25, 0],
  [25, 0],
  [-20, -15],
  [20, -15],
  [-20, 15],
  [20, 15],
];

export interface ResolvedLabel {
  x: number;
  y: number;
}

function labelScreenRect(
  label: PreviewLabelCandidate,
  viewportScale: number,
  visualScale: number
): LabelRect {
  const scaledFontPx = LABEL_FONT_PX * visualScale;
  const charW = scaledFontPx * LABEL_CHAR_WIDTH;
  const textW = label.text.length * charW;
  const h = scaledFontPx;
  const pad = LABEL_PADDING;
  const sx = label.x * viewportScale;
  const sy = label.y * viewportScale;

  if (label.kind === "region") {
    const width = 80 * visualScale;
    return {
      left: sx - width / 2 - pad,
      right: sx + width / 2 + pad,
      top: sy - h / 2 - pad,
      bottom: sy + h / 2 + pad,
    };
  }

  const screenOffset = visualScale;
  return {
    left: sx + screenOffset - pad,
    right: sx + screenOffset + textW + pad,
    top: sy + screenOffset - pad,
    bottom: sy + screenOffset + h + pad,
  };
}

function rectsOverlap(a: LabelRect, b: LabelRect): boolean {
  return a.left < b.right && a.right > b.left && a.top < b.bottom && a.bottom > b.top;
}

export function resolveVisibleLabels(
  candidates: readonly PreviewLabelCandidate[],
  viewportScale: number,
  visualScale: number,
  selectedGeyserIndex: number | null,
  showGeysers: boolean
): Map<string, ResolvedLabel> {
  const kindPriority = { start: 0, geyser: 1, region: 2 } as const;

  const eligible = candidates.filter((label) => {
    if (label.kind === "geyser" && !showGeysers) {
      return false;
    }
    if (label.kind === "region" && viewportScale < 1.8) {
      return false;
    }
    if (viewportScale < 1.8) {
      if (label.kind === "start") {
        return true;
      }
      if (label.kind === "geyser") {
        return selectedGeyserIndex !== null && label.id === `geyser-${selectedGeyserIndex}-label`;
      }
      return false;
    }
    return true;
  });

  eligible.sort((a, b) => {
    const pa = kindPriority[a.kind];
    const pb = kindPriority[b.kind];
    if (pa !== pb) {
      return pa - pb;
    }
    if (a.kind === "geyser" && b.kind === "geyser" && selectedGeyserIndex !== null) {
      const aSelected = a.id === `geyser-${selectedGeyserIndex}-label` ? 0 : 1;
      const bSelected = b.id === `geyser-${selectedGeyserIndex}-label` ? 0 : 1;
      return aSelected - bSelected;
    }
    return 0;
  });

  const placed: LabelRect[] = [];
  const result = new Map<string, ResolvedLabel>();

  for (const label of eligible) {
    if (label.kind !== "region") {
      const rect = labelScreenRect(label, viewportScale, visualScale);
      if (!placed.some((item) => rectsOverlap(rect, item))) {
        placed.push(rect);
        result.set(label.id, { x: label.x, y: label.y });
      }
      continue;
    }

    let bestRect: LabelRect | null = null;
    let bestPos: ResolvedLabel = { x: label.x, y: label.y };

    for (const [dx, dy] of NUDGE_OFFSETS) {
      const nudged = {
        ...label,
        x: label.x + (dx * visualScale) / viewportScale,
        y: label.y + (dy * visualScale) / viewportScale,
      };
      const rect = labelScreenRect(nudged, viewportScale, visualScale);
      if (!placed.some((item) => rectsOverlap(rect, item))) {
        bestRect = rect;
        bestPos = { x: nudged.x, y: nudged.y };
        break;
      }
    }

    bestRect ??= labelScreenRect(label, viewportScale, visualScale);
    placed.push(bestRect);
    result.set(label.id, bestPos);
  }

  return result;
}
