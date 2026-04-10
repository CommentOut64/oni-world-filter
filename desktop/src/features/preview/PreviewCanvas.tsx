import type { KonvaEventObject } from "konva/lib/Node";
import { forwardRef, useEffect, useImperativeHandle, useMemo, useRef, useState } from "react";
import { Circle, Layer, Line, Rect, Stage, Text } from "react-konva";

import type { PreviewPayload } from "../../lib/contracts";
import { previewPalette, zoneFillColor } from "./previewPalette";
import { toPreviewViewModel } from "./previewModel";
import { resetViewport, zoomAtPoint, type ViewportState } from "./viewport";

interface PreviewCanvasProps {
  preview: PreviewPayload | null;
  showBoundaries: boolean;
  showLabels: boolean;
  showGeysers: boolean;
  onHoverRegionChange: (region: { id: string; zoneType: number } | null) => void;
  onHoverGeyserChange: (index: number | null) => void;
  onSelectedGeyserChange: (index: number | null) => void;
}

export interface PreviewCanvasHandle {
  resetView: () => void;
  exportPng: () => void;
}

const DEFAULT_STAGE = { width: 560, height: 560 };

function shouldShowLabel(
  labelId: string,
  kind: "start" | "geyser" | "region",
  scale: number,
  selectedGeyserIndex: number | null
): boolean {
  if (scale >= 1.8) {
    return true;
  }
  if (kind === "start") {
    return true;
  }
  if (kind === "geyser" && selectedGeyserIndex !== null) {
    return labelId === `geyser-${selectedGeyserIndex}-label`;
  }
  return false;
}

const PreviewCanvas = forwardRef<PreviewCanvasHandle, PreviewCanvasProps>(function PreviewCanvas(
  {
    preview,
    showBoundaries,
    showLabels,
    showGeysers,
    onHoverRegionChange,
    onHoverGeyserChange,
    onSelectedGeyserChange,
  },
  ref
) {
  const wrapperRef = useRef<HTMLDivElement | null>(null);
  const stageRef = useRef<import("konva/lib/Stage").Stage | null>(null);
  const [stageSize, setStageSize] = useState(DEFAULT_STAGE);
  const [viewport, setViewport] = useState<ViewportState>({
    scale: 1,
    x: 0,
    y: 0,
  });
  const [hoverRegionId, setHoverRegionId] = useState<string | null>(null);
  const [hoverGeyserIndex, setHoverGeyserIndex] = useState<number | null>(null);
  const [selectedGeyserIndex, setSelectedGeyserIndex] = useState<number | null>(null);

  const model = useMemo(() => (preview ? toPreviewViewModel(preview) : null), [preview]);

  useEffect(() => {
    if (!wrapperRef.current) {
      return;
    }
    const updateSize = () => {
      const nextWidth = Math.max(280, Math.floor(wrapperRef.current?.clientWidth ?? DEFAULT_STAGE.width));
      setStageSize({
        width: nextWidth,
        height: Math.max(320, Math.floor(nextWidth * 0.86)),
      });
    };
    updateSize();
    const observer = new ResizeObserver(updateSize);
    observer.observe(wrapperRef.current);
    return () => observer.disconnect();
  }, []);

  useEffect(() => {
    if (!model) {
      return;
    }
    setViewport(resetViewport(model, stageSize.width, stageSize.height));
    setHoverRegionId(null);
    setHoverGeyserIndex(null);
    setSelectedGeyserIndex(null);
    onHoverRegionChange(null);
    onHoverGeyserChange(null);
    onSelectedGeyserChange(null);
  }, [model, onHoverGeyserChange, onHoverRegionChange, onSelectedGeyserChange, stageSize.height, stageSize.width]);

  useImperativeHandle(
    ref,
    () => ({
      resetView: () => {
        if (!model) {
          return;
        }
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
              strokeWidth={showBoundaries ? 0.6 : 0}
              onMouseEnter={() => {
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
            />
          ))}
          <Rect
            x={0}
            y={0}
            width={model.worldBounds.width}
            height={model.worldBounds.height}
            stroke={previewPalette.worldBorder}
            strokeWidth={0.8}
          />
        </Layer>

        {showGeysers ? (
          <Layer>
            {model.geysers.map((geyser) => (
              <Circle
                key={`geyser-${geyser.index}`}
                x={geyser.x}
                y={geyser.y}
                radius={2.8}
                fill={previewPalette.geyserMarker}
                onMouseEnter={() => {
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
                  setSelectedGeyserIndex(geyser.index);
                  onSelectedGeyserChange(geyser.index);
                }}
              />
            ))}
            <Circle
              x={model.startMarker.x}
              y={model.startMarker.y}
              radius={3.2}
              fill={previewPalette.startMarker}
            />
          </Layer>
        ) : null}

        {showLabels ? (
          <Layer listening={false}>
            {model.labelCandidates.map((label) => {
              if (
                !shouldShowLabel(
                  label.id,
                  label.kind,
                  viewport.scale,
                  selectedGeyserIndex
                )
              ) {
                return null;
              }
              if (label.kind === "geyser" && showGeysers === false) {
                return null;
              }
              if (label.kind === "region" && viewport.scale < 1.8) {
                return null;
              }
              return (
                <Text
                  key={label.id}
                  x={label.x + 1}
                  y={label.y + 1}
                  text={label.text}
                  fontSize={6.6}
                  fill={previewPalette.label}
                />
              );
            })}
          </Layer>
        ) : null}

        <Layer listening={false}>
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
                  strokeWidth={1.2}
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
                  radius={4.8}
                  stroke={previewPalette.geyserHover}
                  strokeWidth={1.3}
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
                  radius={6.2}
                  stroke={previewPalette.geyserSelected}
                  strokeWidth={1.8}
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
