import { useEffect, useRef, useState } from "react";

import { usePreviewStore } from "../../state/previewStore";
import { useSearchStore } from "../../state/searchStore";
import PreviewCanvas, { type PreviewCanvasHandle } from "./PreviewCanvas";
import PreviewDetails from "./PreviewDetails";
import GeyserListOverlay from "./GeyserListOverlay";
import PreviewLegend from "./PreviewLegend";
import PreviewToolbar from "./PreviewToolbar";

export default function PreviewPane() {
  const selectedSeed = useSearchStore((state) => state.selectedSeed);
  const results = useSearchStore((state) => state.results);
  const geysers = useSearchStore((state) => state.geysers);

  const loadByMatch = usePreviewStore((state) => state.loadByMatch);
  const preview = usePreviewStore((state) => state.activePreview);
  const isLoading = usePreviewStore((state) => state.isLoading);
  const lastError = usePreviewStore((state) => state.lastError);
  const clearError = usePreviewStore((state) => state.clearError);
  const canvasRef = useRef<PreviewCanvasHandle | null>(null);

  const [showBoundaries, setShowBoundaries] = useState(true);
  const [showLabels, setShowLabels] = useState(true);
  const [showGeysers, setShowGeysers] = useState(true);
  const [hoveredRegion, setHoveredRegion] = useState<{
    id: string;
    zoneType: number;
  } | null>(null);
  const [selectedRegion, setSelectedRegion] = useState<{
    id: string;
    zoneType: number;
  } | null>(null);
  const [hoverGeyserIndex, setHoverGeyserIndex] = useState<number | null>(null);
  const [selectedGeyserIndex, setSelectedGeyserIndex] = useState<number | null>(null);
  const [showGeyserList, setShowGeyserList] = useState(false);

  const selectedMatch =
    selectedSeed === null
      ? null
      : results.find((item) => item.seed === selectedSeed) ?? null;

  useEffect(() => {
    if (!selectedMatch) {
      return;
    }
    void loadByMatch(selectedMatch);
  }, [selectedMatch, loadByMatch]);

  return (
    <section className="preview-pane">
      <header>
        <h3>地图预览</h3>
        <p>仅在选中结果后按需请求 preview，不影响左侧批量搜索。</p>
      </header>
      {isLoading ? <p className="hint">预览加载中...</p> : null}
      {lastError ? (
        <p className="error-inline" onClick={clearError}>
          预览失败: {lastError}
        </p>
      ) : null}
      <PreviewToolbar
        showBoundaries={showBoundaries}
        showLabels={showLabels}
        showGeysers={showGeysers}
        geyserCount={preview?.summary.geysers.length ?? 0}
        onToggleBoundaries={() => setShowBoundaries((current) => !current)}
        onToggleLabels={() => setShowLabels((current) => !current)}
        onToggleGeysers={() => setShowGeysers((current) => !current)}
        onResetView={() => canvasRef.current?.resetView()}
        onExportPng={() => canvasRef.current?.exportPng()}
        onOpenGeyserList={() => setShowGeyserList(true)}
      />
      <div className="preview-canvas-container">
        <PreviewCanvas
          ref={canvasRef}
          preview={preview}
          geysers={geysers}
          showBoundaries={showBoundaries}
          showLabels={showLabels}
          showGeysers={showGeysers}
          onHoverRegionChange={setHoveredRegion}
          onSelectedRegionChange={setSelectedRegion}
          onHoverGeyserChange={setHoverGeyserIndex}
          onSelectedGeyserChange={setSelectedGeyserIndex}
        />
        {showGeyserList && preview ? (
          <GeyserListOverlay
            geysersData={preview.summary.geysers}
            onClose={() => setShowGeyserList(false)}
          />
        ) : null}
      </div>
      <PreviewLegend />
      <PreviewDetails
        preview={preview}
        hoveredRegion={hoveredRegion}
        selectedRegion={selectedRegion}
        hoverGeyserIndex={hoverGeyserIndex}
        selectedGeyserIndex={selectedGeyserIndex}
      />
    </section>
  );
}
