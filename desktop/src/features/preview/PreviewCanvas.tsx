import type { KonvaEventObject } from "konva/lib/Node";
import { forwardRef, useEffect, useImperativeHandle, useMemo, useRef, useState } from "react";
import { Circle, Layer, Line, Rect, Stage, Text } from "react-konva";

import type { GeyserOption, PreviewPayload } from "../../lib/contracts";
import { previewPalette, zoneFillColor } from "./previewPalette";
import { toPreviewViewModel } from "./previewModel";
import {
  reconcileViewportOnStageResize,
  resetViewport,
  shouldResetPreviewInteractionState,
  zoomAtPoint,
  type ViewportState,
} from "./viewport";

interface PreviewCanvasProps {
  sessionKey: string | null;
  preview: PreviewPayload | null;
  geysers: readonly GeyserOption[];
  showBoundaries: boolean;
  showLabels: boolean;
  showGeysers: boolean;
  onHoverRegionChange: (region: { id: string; zoneType: number } | null) => void;
  onSelectedRegionChange: (region: { id: string; zoneType: number } | null) => void;
  onHoverGeyserChange: (index: number | null) => void;
  onSelectedGeyserChange: (index: number | null) => void;
}

export interface PreviewCanvasHandle {
  resetView: () => void;
  exportPng: () => void;
}

const DEFAULT_STAGE = { width: 560, height: 560 };

// 标签屏幕像素常量
const LABEL_FONT_PX = 11;
const LABEL_CHAR_WIDTH = 0.55; // 近似每字符宽度系数
const LABEL_PADDING = 4; // 碰撞检测额外间距

interface LabelRect {
  left: number;
  right: number;
  top: number;
  bottom: number;
}

function labelScreenRect(
  label: { x: number; y: number; text: string; kind: "start" | "geyser" | "region" },
  scale: number
): LabelRect {
  const charW = LABEL_FONT_PX * LABEL_CHAR_WIDTH;
  const textW = label.text.length * charW;
  const h = LABEL_FONT_PX;
  const pad = LABEL_PADDING;

  // 屏幕坐标 = world * scale，标签大小固定为屏幕像素
  const sx = label.x * scale;
  const sy = label.y * scale;

  if (label.kind === "region") {
    // 居中对齐
    return {
      left: sx - textW / 2 - pad,
      right: sx + textW / 2 + pad,
      top: sy - h / 2 - pad,
      bottom: sy + h / 2 + pad,
    };
  }
  // 喷口/起点：左上角偏移 +1px
  return {
    left: sx + 1 - pad,
    right: sx + 1 + textW + pad,
    top: sy + 1 - pad,
    bottom: sy + 1 + h + pad,
  };
}

function rectsOverlap(a: LabelRect, b: LabelRect): boolean {
  return a.left < b.right && a.right > b.left && a.top < b.bottom && a.bottom > b.top;
}

// 区域标签偏移尝试方向（屏幕像素）
const NUDGE_OFFSETS: [number, number][] = [
  [0, 0],      // 原位（质心）
  [0, -20],    // 上
  [0, 20],     // 下
  [-25, 0],    // 左
  [25, 0],     // 右
  [-20, -15],  // 左上
  [20, -15],   // 右上
  [-20, 15],   // 左下
  [20, 15],    // 右下
];

interface ResolvedLabel {
  x: number; // 最终世界坐标
  y: number;
}

/** 按优先级排序标签并进行碰撞检测，返回可见标签最终位置 */
function resolveVisibleLabels(
  candidates: readonly { id: string; x: number; y: number; text: string; kind: "start" | "geyser" | "region" }[],
  scale: number,
  selectedGeyserIndex: number | null,
  showGeysers: boolean
): Map<string, ResolvedLabel> {
  const kindPriority = { start: 0, geyser: 1, region: 2 } as const;

  const eligible = candidates.filter((label) => {
    if (label.kind === "geyser" && !showGeysers) return false;
    if (label.kind === "region" && scale < 1.8) return false;
    if (scale < 1.8) {
      if (label.kind === "start") return true;
      if (label.kind === "geyser") {
        return selectedGeyserIndex !== null && label.id === `geyser-${selectedGeyserIndex}-label`;
      }
      return false;
    }
    return true;
  });

  // 按优先级排序：start > 选中喷口 > 其他喷口 > 区域
  eligible.sort((a, b) => {
    const pa = kindPriority[a.kind];
    const pb = kindPriority[b.kind];
    if (pa !== pb) return pa - pb;
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
      // 起点/喷口标签：原位放置，不偏移
      const rect = labelScreenRect(label, scale);
      const overlaps = placed.some((p) => rectsOverlap(rect, p));
      if (!overlaps) {
        placed.push(rect);
        result.set(label.id, { x: label.x, y: label.y });
      }
      continue;
    }

    // 区域标签：尝试多个偏移位置，找到第一个不重叠的
    let bestRect: LabelRect | null = null;
    let bestPos: ResolvedLabel = { x: label.x, y: label.y };

    for (const [dx, dy] of NUDGE_OFFSETS) {
      const nudged = { ...label, x: label.x + dx / scale, y: label.y + dy / scale };
      const rect = labelScreenRect(nudged, scale);
      if (!placed.some((p) => rectsOverlap(rect, p))) {
        bestRect = rect;
        bestPos = { x: nudged.x, y: nudged.y };
        break;
      }
    }

    // 所有位置都重叠时，仍然显示在质心
    if (!bestRect) {
      bestRect = labelScreenRect(label, scale);
    }

    placed.push(bestRect);
    result.set(label.id, bestPos);
  }

  return result;
}

const PreviewCanvas = forwardRef<PreviewCanvasHandle, PreviewCanvasProps>(function PreviewCanvas(
  {
    sessionKey,
    preview,
    geysers,
    showBoundaries,
    showLabels,
    showGeysers,
    onHoverRegionChange,
    onSelectedRegionChange,
    onHoverGeyserChange,
    onSelectedGeyserChange,
  },
  ref
) {
  const wrapperRef = useRef<HTMLDivElement | null>(null);
  const stageRef = useRef<import("konva/lib/Stage").Stage | null>(null);
  const lastSessionKeyRef = useRef<string | null>(null);
  const initializedViewportSessionKeyRef = useRef<string | null>(null);
  const [stageSize, setStageSize] = useState(DEFAULT_STAGE);
  const [viewport, setViewport] = useState<ViewportState>({
    scale: 1,
    x: 0,
    y: 0,
  });
  const [hoverRegionId, setHoverRegionId] = useState<string | null>(null);
  const [selectedRegionId, setSelectedRegionId] = useState<string | null>(null);
  const [hoverGeyserIndex, setHoverGeyserIndex] = useState<number | null>(null);
  const [selectedGeyserIndex, setSelectedGeyserIndex] = useState<number | null>(null);
  const [hasManualViewportInteraction, setHasManualViewportInteraction] = useState(false);

  const model = useMemo(() => (preview ? toPreviewViewModel(preview, geysers) : null), [geysers, preview]);
  const fittedViewport = useMemo(() => {
    if (!model) {
      return null;
    }
    return resetViewport(model, stageSize.width, stageSize.height);
  }, [model, stageSize.height, stageSize.width]);

  const resolvedLabels = useMemo(() => {
    if (!model) return new Map<string, ResolvedLabel>();
    return resolveVisibleLabels(
      model.labelCandidates,
      viewport.scale,
      selectedGeyserIndex,
      showGeysers
    );
  }, [model, viewport.scale, selectedGeyserIndex, showGeysers]);

  useEffect(() => {
    if (!wrapperRef.current) {
      return;
    }
    const updateSize = () => {
      const nextWidth = Math.max(280, Math.floor(wrapperRef.current?.clientWidth ?? DEFAULT_STAGE.width));
      const nextHeight = Math.max(320, Math.floor(wrapperRef.current?.clientHeight ?? DEFAULT_STAGE.height));
      setStageSize({
        width: nextWidth,
        height: nextHeight,
      });
    };
    updateSize();
    const observer = new ResizeObserver(updateSize);
    observer.observe(wrapperRef.current);
    return () => observer.disconnect();
  }, []);

  useEffect(() => {
    if (
      !shouldResetPreviewInteractionState({
        previousSessionKey: lastSessionKeyRef.current,
        nextSessionKey: sessionKey,
      })
    ) {
      return;
    }
    lastSessionKeyRef.current = sessionKey;
    initializedViewportSessionKeyRef.current = null;
    setHasManualViewportInteraction(false);
    setHoverRegionId(null);
    setSelectedRegionId(null);
    setHoverGeyserIndex(null);
    setSelectedGeyserIndex(null);
    onHoverRegionChange(null);
    onSelectedRegionChange(null);
    onHoverGeyserChange(null);
    onSelectedGeyserChange(null);
  }, [onHoverGeyserChange, onHoverRegionChange, onSelectedGeyserChange, onSelectedRegionChange, sessionKey]);

  useEffect(() => {
    if (!sessionKey || !model) {
      return;
    }
    if (initializedViewportSessionKeyRef.current === sessionKey) {
      return;
    }
    initializedViewportSessionKeyRef.current = sessionKey;
    setViewport(resetViewport(model, stageSize.width, stageSize.height));
  }, [model, sessionKey, stageSize.height, stageSize.width]);

  useEffect(() => {
    if (!fittedViewport) {
      return;
    }
    setViewport((current) =>
      reconcileViewportOnStageResize({
        hasManualViewportInteraction,
        currentViewport: current,
        fittedViewport,
      })
    );
  }, [fittedViewport, hasManualViewportInteraction]);

  useImperativeHandle(
    ref,
    () => ({
      resetView: () => {
        if (!model) {
          return;
        }
        setHasManualViewportInteraction(false);
        setViewport(resetViewport(model, stageSize.width, stageSize.height));
      },
      exportPng: () => {
        if (!stageRef.current || !preview) {
          return;
        }
        const uri = stageRef.current.toDataURL({ pixelRatio: 2 });
        const link = document.createElement("a");
        link.href = uri;
        link.download = `oni-preview-${preview.summary.seed}.png`;
        link.click();
      },
    }),
    [model, preview, stageSize.height, stageSize.width]
  );

  if (!preview || !model) {
    return (
      <section className="preview-canvas-wrap preview-empty" ref={wrapperRef}>
        <p className="hint">无预览数据</p>
      </section>
    );
  }

  const handleWheel = (event: KonvaEventObject<WheelEvent>) => {
    event.evt.preventDefault();
    const pointer = stageRef.current?.getPointerPosition();
    if (!pointer) {
      return;
    }
    const direction = event.evt.deltaY > 0 ? 0.92 : 1.1;
    setHasManualViewportInteraction(true);
    setViewport((current) => zoomAtPoint(current, pointer, current.scale * direction));
  };

  return (
    <section className="preview-canvas-wrap" ref={wrapperRef}>
      <Stage
        ref={stageRef}
        width={stageSize.width}
        height={stageSize.height}
        x={viewport.x}
        y={viewport.y}
        scaleX={viewport.scale}
        scaleY={viewport.scale}
        draggable
        onWheel={handleWheel}
        onDragEnd={(event) => {
          setHasManualViewportInteraction(true);
          setViewport((current) => ({
            ...current,
            x: event.target.x(),
            y: event.target.y(),
          }));
        }}
      >
        <Layer listening={false}>
          <Rect
            x={-viewport.x / viewport.scale}
            y={-viewport.y / viewport.scale}
            width={stageSize.width / viewport.scale}
            height={stageSize.height / viewport.scale}
            fill={previewPalette.background}
          />
        </Layer>

        <Layer>
          {model.regions.map((region) => (
            <Line
              key={region.id}
              points={region.points}
              closed
              fill={zoneFillColor(region.zoneType)}
              stroke={showBoundaries ? previewPalette.boundary : "transparent"}
              strokeWidth={showBoundaries ? 1 / viewport.scale : 0}
              onMouseEnter={() => {
                setHasManualViewportInteraction(true);
                setHoverRegionId(region.id);
                onHoverRegionChange({
                  id: region.id,
                  zoneType: region.zoneType,
                });
              }}
              onMouseLeave={() => {
                setHoverRegionId((current) => (current === region.id ? null : current));
                onHoverRegionChange(null);
              }}
              onClick={() => {
                setHasManualViewportInteraction(true);
                setSelectedRegionId(region.id);
                setHoverRegionId((current) => (current === region.id ? null : current));
                onHoverRegionChange(null);
                onSelectedRegionChange({
                  id: region.id,
                  zoneType: region.zoneType,
                });
              }}
            />
          ))}
          <Rect
            x={0}
            y={0}
            width={model.worldBounds.width}
            height={model.worldBounds.height}
            stroke={previewPalette.worldBorder}
            strokeWidth={1 / viewport.scale}
            listening={false}
          />
        </Layer>

        {showGeysers ? (
          <Layer>
            {model.geysers.map((geyser) => (
              <Circle
                key={`geyser-${geyser.index}`}
                x={geyser.x}
                y={geyser.y}
                radius={5 / viewport.scale}
                fill={previewPalette.geyserMarker}
                onMouseEnter={() => {
                  setHasManualViewportInteraction(true);
                  setHoverGeyserIndex(geyser.index);
                  onHoverGeyserChange(geyser.index);
                }}
                onMouseLeave={() => {
                  setHoverGeyserIndex((current) =>
                    current === geyser.index ? null : current
                  );
                  onHoverGeyserChange(null);
                }}
                onClick={() => {
                  setHasManualViewportInteraction(true);
                  setSelectedGeyserIndex(geyser.index);
                  setHoverGeyserIndex((current) =>
                    current === geyser.index ? null : current
                  );
                  onHoverGeyserChange(null);
                  onSelectedGeyserChange(geyser.index);
                }}
              />
            ))}
            <Circle
              x={model.startMarker.x}
              y={model.startMarker.y}
              radius={6 / viewport.scale}
              fill={previewPalette.startMarker}
            />
          </Layer>
        ) : null}

        {showLabels ? (
          <Layer listening={false}>
            {model.labelCandidates.map((label) => {
              const resolved = resolvedLabels.get(label.id);
              if (!resolved) {
                return null;
              }
              return (
                <Text
                  key={label.id}
                  x={resolved.x}
                  y={resolved.y}
                  text={label.text}
                  fontSize={11 / viewport.scale}
                  fill={previewPalette.label}
                  align={label.kind === "region" ? "center" : "left"}
                  verticalAlign={label.kind === "region" ? "middle" : "top"}
                  offsetX={label.kind === "region" ? 40 / viewport.scale : -1 / viewport.scale}
                  offsetY={label.kind === "region" ? 5.5 / viewport.scale : -1 / viewport.scale}
                  width={label.kind === "region" ? 80 / viewport.scale : undefined}
                />
              );
            })}
          </Layer>
        ) : null}

        <Layer listening={false}>
          {selectedRegionId ? (
            (() => {
              const region = model.regions.find((item) => item.id === selectedRegionId);
              if (!region) {
                return null;
              }
              return (
                <Line
                  points={region.points}
                  closed
                  fill={previewPalette.regionSelected}
                  stroke={previewPalette.regionStrokeSelected}
                  strokeWidth={2 / viewport.scale}
                />
              );
            })()
          ) : null}

          {hoverRegionId ? (
            (() => {
              const region = model.regions.find((item) => item.id === hoverRegionId);
              if (!region) {
                return null;
              }
              return (
                <Line
                  points={region.points}
                  closed
                  fill={previewPalette.regionHover}
                  stroke={previewPalette.regionStrokeHover}
                  strokeWidth={2 / viewport.scale}
                />
              );
            })()
          ) : null}

          {showGeysers && hoverGeyserIndex !== null ? (
            (() => {
              const marker = model.geysers.find((item) => item.index === hoverGeyserIndex);
              if (!marker) {
                return null;
              }
              return (
                <Circle
                  x={marker.x}
                  y={marker.y}
                  radius={8 / viewport.scale}
                  stroke={previewPalette.geyserHover}
                  strokeWidth={1.5 / viewport.scale}
                />
              );
            })()
          ) : null}

          {showGeysers && selectedGeyserIndex !== null ? (
            (() => {
              const marker = model.geysers.find((item) => item.index === selectedGeyserIndex);
              if (!marker) {
                return null;
              }
              return (
                <Circle
                  x={marker.x}
                  y={marker.y}
                  radius={10 / viewport.scale}
                  stroke={previewPalette.geyserSelected}
                  strokeWidth={2 / viewport.scale}
                />
              );
            })()
          ) : null}
        </Layer>
      </Stage>
    </section>
  );
});

export default PreviewCanvas;
