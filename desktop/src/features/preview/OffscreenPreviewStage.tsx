import type { Stage as KonvaStage } from "konva/lib/Stage";
import { useEffect, useMemo, useRef } from "react";
import { createRoot } from "react-dom/client";
import { Circle, Layer, Line, Rect, Stage, Text } from "react-konva";

import type { DesktopThemeMode } from "../../app/antdTheme";
import type { GeyserOption, PreviewPayload } from "../../lib/contracts.ts";
import { LABEL_FONT_PX, resolveVisibleLabels } from "./previewLabelLayout.ts";
import { createPreviewPalette, zoneFillColor } from "./previewPalette.ts";
import { toPreviewViewModel } from "./previewModel.ts";
import type { ViewportState } from "./viewport.ts";

export interface SnapshotPreviewSceneRequest {
  preview: PreviewPayload;
  geysers: readonly GeyserOption[];
  themeMode: DesktopThemeMode;
  stageWidth: number;
  stageHeight: number;
  viewport: ViewportState;
  showBoundaries: boolean;
  showLabels: boolean;
  showGeysers: boolean;
  selectedGeyserIndex: number | null;
}

interface OffscreenPreviewStageProps extends SnapshotPreviewSceneRequest {
  onReady: (stage: KonvaStage) => void;
}

function OffscreenPreviewStage(props: OffscreenPreviewStageProps) {
  const {
    preview,
    geysers,
    themeMode,
    stageWidth,
    stageHeight,
    viewport,
    showBoundaries,
    showLabels,
    showGeysers,
    selectedGeyserIndex,
    onReady,
  } = props;
  const stageRef = useRef<KonvaStage | null>(null);
  const model = useMemo(() => toPreviewViewModel(preview, geysers), [geysers, preview]);
  const previewPalette = useMemo(() => createPreviewPalette(themeMode), [themeMode]);
  const reportVisualScale = useMemo(() => stageWidth / 560, [stageWidth]);
  const resolvedLabels = useMemo(
    () =>
      resolveVisibleLabels(
        model.labelCandidates,
        viewport.scale,
        reportVisualScale,
        selectedGeyserIndex,
        showGeysers
      ),
    [model.labelCandidates, reportVisualScale, selectedGeyserIndex, showGeysers, viewport.scale]
  );

  useEffect(() => {
    if (!stageRef.current) {
      return;
    }
    let frame1 = 0;
    let frame2 = 0;
    frame1 = requestAnimationFrame(() => {
      frame2 = requestAnimationFrame(() => {
        if (stageRef.current) {
          onReady(stageRef.current);
        }
      });
    });
    return () => {
      cancelAnimationFrame(frame1);
      cancelAnimationFrame(frame2);
    };
  }, [onReady]);

  return (
    <Stage
      ref={stageRef}
      width={stageWidth}
      height={stageHeight}
      x={viewport.x}
      y={viewport.y}
      scaleX={viewport.scale}
      scaleY={viewport.scale}
      listening={false}
    >
      <Layer listening={false}>
        <Rect
          x={-viewport.x / viewport.scale}
          y={-viewport.y / viewport.scale}
          width={stageWidth / viewport.scale}
          height={stageHeight / viewport.scale}
          fill={previewPalette.background}
        />
      </Layer>

      <Layer listening={false}>
        {model.regions.map((region) => (
          <Line
            key={region.id}
            points={region.points}
            closed
            fill={zoneFillColor(region.zoneType, themeMode)}
            stroke={showBoundaries ? previewPalette.boundary : "transparent"}
            strokeWidth={showBoundaries ? (1 * reportVisualScale) / viewport.scale : 0}
          />
        ))}
        <Rect
          x={0}
          y={0}
          width={model.worldBounds.width}
          height={model.worldBounds.height}
          stroke={previewPalette.worldBorder}
          strokeWidth={(1 * reportVisualScale) / viewport.scale}
        />
      </Layer>

      {showGeysers ? (
        <Layer listening={false}>
          {model.geysers.map((geyser) => (
            <Circle
              key={`geyser-${geyser.index}`}
              x={geyser.x}
              y={geyser.y}
              radius={(5 * reportVisualScale) / viewport.scale}
              fill={previewPalette.geyserMarker}
            />
          ))}
          <Circle
            x={model.startMarker.x}
            y={model.startMarker.y}
            radius={(6 * reportVisualScale) / viewport.scale}
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
                fontSize={(LABEL_FONT_PX * reportVisualScale) / viewport.scale}
                fill={previewPalette.label}
                align={label.kind === "region" ? "center" : "left"}
                verticalAlign={label.kind === "region" ? "middle" : "top"}
                offsetX={
                  label.kind === "region"
                    ? (40 * reportVisualScale) / viewport.scale
                    : (-1 * reportVisualScale) / viewport.scale
                }
                offsetY={
                  label.kind === "region"
                    ? (5.5 * reportVisualScale) / viewport.scale
                    : (-1 * reportVisualScale) / viewport.scale
                }
                width={label.kind === "region" ? (80 * reportVisualScale) / viewport.scale : undefined}
              />
            );
          })}
        </Layer>
      ) : null}
    </Stage>
  );
}

export async function snapshotPreviewSceneToDataUrl(
  request: SnapshotPreviewSceneRequest
): Promise<string> {
  if (typeof document === "undefined") {
    throw new Error("当前环境不支持离屏地图渲染。");
  }

  return await new Promise<string>((resolve, reject) => {
    const host = document.createElement("div");
    host.style.position = "fixed";
    host.style.left = "-100000px";
    host.style.top = "0";
    host.style.width = `${request.stageWidth}px`;
    host.style.height = `${request.stageHeight}px`;
    host.style.pointerEvents = "none";
    document.body.append(host);

    const root = createRoot(host);

    const cleanup = () => {
      root.unmount();
      host.remove();
    };

    const handleReady = (stage: KonvaStage) => {
      try {
        const dataUrl = stage.toDataURL({ pixelRatio: 1 });
        cleanup();
        resolve(dataUrl);
      } catch (error) {
        cleanup();
        reject(error);
      }
    };

    try {
      root.render(<OffscreenPreviewStage {...request} onReady={handleReady} />);
    } catch (error) {
      cleanup();
      reject(error);
    }
  });
}
