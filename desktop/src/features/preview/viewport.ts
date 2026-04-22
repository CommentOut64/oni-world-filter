import type { PreviewViewModel } from "./previewModel";

export interface ViewportState {
  scale: number;
  x: number;
  y: number;
}

export interface FitViewportInput {
  worldWidth: number;
  worldHeight: number;
  stageWidth: number;
  stageHeight: number;
  padding?: number;
}

export function fitToWorld(input: FitViewportInput): ViewportState {
  const worldWidth = Math.max(1, input.worldWidth);
  const worldHeight = Math.max(1, input.worldHeight);
  const stageWidth = Math.max(1, input.stageWidth);
  const stageHeight = Math.max(1, input.stageHeight);
  const padding = input.padding ?? 24;
  // 水平方向留 padding，垂直方向不留白
  const scaleX = (stageWidth - padding * 2) / worldWidth;
  const scaleY = stageHeight / worldHeight;
  const scale = clampScale(Math.min(scaleX, scaleY));
  const contentWidth = worldWidth * scale;
  const contentHeight = worldHeight * scale;
  return {
    scale,
    x: (stageWidth - contentWidth) / 2,
    y: (stageHeight - contentHeight) / 2,
  };
}

export function clampScale(value: number): number {
  return Math.max(0.2, Math.min(10, value));
}

export function resetViewport(
  model: PreviewViewModel,
  stageWidth: number,
  stageHeight: number
): ViewportState {
  return fitToWorld({
    worldWidth: model.worldBounds.width,
    worldHeight: model.worldBounds.height,
    stageWidth,
    stageHeight,
  });
}

export function zoomAtPoint(
  viewport: ViewportState,
  point: { x: number; y: number },
  nextScale: number
): ViewportState {
  const clamped = clampScale(nextScale);
  const worldX = (point.x - viewport.x) / viewport.scale;
  const worldY = (point.y - viewport.y) / viewport.scale;
  return {
    scale: clamped,
    x: point.x - worldX * clamped,
    y: point.y - worldY * clamped,
  };
}

export function reconcileViewportOnStageResize(input: {
  hasManualViewportInteraction: boolean;
  currentViewport: ViewportState;
  fittedViewport: ViewportState;
}): ViewportState {
  if (input.hasManualViewportInteraction) {
    return input.currentViewport;
  }
  return input.fittedViewport;
}

export function shouldResetPreviewInteractionState(input: {
  previousSessionKey: string | null;
  nextSessionKey: string | null;
}): boolean {
  return input.previousSessionKey !== input.nextSessionKey;
}

export function pointerToWorld(
  pointer: { x: number; y: number },
  viewport: ViewportState
): { x: number; y: number } {
  return {
    x: (pointer.x - viewport.x) / viewport.scale,
    y: (pointer.y - viewport.y) / viewport.scale,
  };
}
