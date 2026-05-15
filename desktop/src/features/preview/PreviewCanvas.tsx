import type { KonvaEventObject } from "konva/lib/Node";
import { forwardRef, useEffect, useImperativeHandle, useMemo, useRef, useState } from "react";
import { Circle, Layer, Line, Rect, Stage, Text } from "react-konva";

import type { DesktopThemeMode } from "../../app/antdTheme";
import type { GeyserOption, PreviewPayload } from "../../lib/contracts";
import type { GeyserParameterAnchor } from "./GeyserParameterPopover";
import { LABEL_FONT_PX, resolveVisibleLabels, type ResolvedLabel } from "./previewLabelLayout.ts";
import { createPreviewPalette, zoneFillColor } from "./previewPalette";
import { toPreviewViewModel, type PreviewGeyserMarker } from "./previewModel";
import {
  reconcileViewportOnStageResize,
  resetViewport,
  shouldResetPreviewInteractionState,
  zoomAtPoint,
  type ViewportState,
} from "./viewport";

interface PreviewCanvasProps {
  themeMode: DesktopThemeMode;
  sessionKey: string | null;
  preview: PreviewPayload | null;
  geysers: readonly GeyserOption[];
  geyserPopoverEnabled: boolean;
  showBoundaries: boolean;
  showBiomes: boolean;
  showGeysers: boolean;
  selectedGeyserIndex: number | null;
  onHoverRegionChange: (region: { id: string; zoneType: number } | null) => void;
  onSelectedRegionChange: (region: { id: string; zoneType: number } | null) => void;
  onHoverGeyserChange: (index: number | null) => void;
  onSelectedGeyserChange: (index: number | null) => void;
  onSelectedGeyserAnchorChange: (anchor: GeyserParameterAnchor | null) => void;
}

export interface PreviewCanvasHandle {
  resetView: () => void;
}

const DEFAULT_STAGE = { width: 560, height: 560 };

const GEYSER_POPOVER_MARGIN = 12;
const GEYSER_POPOVER_WIDTH = 320;
const GEYSER_POPOVER_HEIGHT = 240;

function resolveSelectedGeyserAnchor(
  marker: PreviewGeyserMarker,
  viewport: ViewportState,
  stageSize: { width: number; height: number }
): GeyserParameterAnchor {
  const screenX = marker.x * viewport.scale + viewport.x;
  const screenY = marker.y * viewport.scale + viewport.y;

  return {
    left: Math.max(
      GEYSER_POPOVER_MARGIN,
      Math.min(screenX + GEYSER_POPOVER_MARGIN, stageSize.width - GEYSER_POPOVER_WIDTH - GEYSER_POPOVER_MARGIN)
    ),
    top: Math.max(
      GEYSER_POPOVER_MARGIN,
      Math.min(screenY + GEYSER_POPOVER_MARGIN, stageSize.height - GEYSER_POPOVER_HEIGHT - GEYSER_POPOVER_MARGIN)
    ),
  };
}

const PreviewCanvas = forwardRef<PreviewCanvasHandle, PreviewCanvasProps>(function PreviewCanvas(
  {
    themeMode,
    sessionKey,
    preview,
    geysers,
    geyserPopoverEnabled,
    showBoundaries,
    showBiomes,
    showGeysers,
    selectedGeyserIndex,
    onHoverRegionChange,
    onSelectedRegionChange,
    onHoverGeyserChange,
    onSelectedGeyserChange,
    onSelectedGeyserAnchorChange,
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
  const [hasManualViewportInteraction, setHasManualViewportInteraction] = useState(false);

  const model = useMemo(() => (preview ? toPreviewViewModel(preview, geysers) : null), [geysers, preview]);
  const previewPalette = useMemo(() => createPreviewPalette(themeMode), [themeMode]);
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
      1,
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
    onHoverRegionChange(null);
    onSelectedRegionChange(null);
    onHoverGeyserChange(null);
    onSelectedGeyserChange(null);
    onSelectedGeyserAnchorChange(null);
  }, [
    onHoverGeyserChange,
    onHoverRegionChange,
    onSelectedGeyserAnchorChange,
    onSelectedGeyserChange,
    onSelectedRegionChange,
    sessionKey,
  ]);

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

  useEffect(() => {
    if (showGeysers || selectedGeyserIndex === null) {
      return;
    }
    setHoverGeyserIndex(null);
    onHoverGeyserChange(null);
    onSelectedGeyserChange(null);
    onSelectedGeyserAnchorChange(null);
  }, [
    onHoverGeyserChange,
    onSelectedGeyserAnchorChange,
    onSelectedGeyserChange,
    selectedGeyserIndex,
    showGeysers,
  ]);

  useEffect(() => {
    if (!geyserPopoverEnabled || !model || !showGeysers || selectedGeyserIndex === null) {
      onSelectedGeyserAnchorChange(null);
      return;
    }
    const marker = model.geysers.find((item) => item.index === selectedGeyserIndex);
    if (!marker) {
      onSelectedGeyserAnchorChange(null);
      return;
    }
    onSelectedGeyserAnchorChange(resolveSelectedGeyserAnchor(marker, viewport, stageSize));
  }, [
    model,
    geyserPopoverEnabled,
    onSelectedGeyserAnchorChange,
    selectedGeyserIndex,
    showGeysers,
    stageSize,
    viewport,
  ]);

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
    }),
    [model, stageSize.height, stageSize.width]
  );

  if (!preview || !model) {
    return (
      <section
        className="preview-canvas-wrap preview-empty"
        ref={wrapperRef}
        style={{ backgroundColor: previewPalette.background }}
      >
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
    <section
      className="preview-canvas-wrap"
      ref={wrapperRef}
      style={{ backgroundColor: previewPalette.background }}
    >
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
        onClick={(event) => {
          if (event.target !== event.target.getStage()) {
            return;
          }
          setHoverGeyserIndex(null);
          onHoverGeyserChange(null);
          onSelectedGeyserChange(null);
          onSelectedGeyserAnchorChange(null);
        }}
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
              fill={zoneFillColor(region.zoneType, themeMode)}
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
                  const nextSelectedGeyserIndex =
                    selectedGeyserIndex === geyser.index ? null : geyser.index;
                  setHoverGeyserIndex((current) =>
                    current === geyser.index ? null : current
                  );
                  onHoverGeyserChange(null);
                  onSelectedGeyserChange(nextSelectedGeyserIndex);
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

        {showBiomes || showGeysers ? (
          <Layer listening={false}>
            {model.labelCandidates.map((label) => {
              if (label.kind === "region" && !showBiomes) {
                return null;
              }
              if (label.kind !== "region" && !showGeysers) {
                return null;
              }
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
                  fontSize={LABEL_FONT_PX / viewport.scale}
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
