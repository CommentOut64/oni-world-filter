import React, { useEffect, useRef, useState } from "react";
import { Alert, Typography } from "antd";

import type { DesktopThemeMode } from "../../app/antdTheme";
import ThemeModeToggle from "../../components/layout/ThemeModeToggle";
import { formatTauriError } from "../../lib/tauri.ts";
import { usePreviewStore } from "../../state/previewStore";
import { useSearchStore } from "../../state/searchStore";
import PreviewCanvas, { type PreviewCanvasHandle } from "./PreviewCanvas";
import PreviewDetails from "./PreviewDetails";
import GeyserParameterPopover, { type GeyserParameterAnchor } from "./GeyserParameterPopover";
import GeyserListOverlay from "./GeyserListOverlay";
import PreviewLegend from "./PreviewLegend";
import PreviewToolbar from "./PreviewToolbar";

interface PreviewPaneProps {
  themeMode: DesktopThemeMode;
  onThemeModeChange: (mode: DesktopThemeMode) => void;
}

export default function PreviewPane({ themeMode, onThemeModeChange }: PreviewPaneProps) {
  void React;
  const selectedSeed = useSearchStore((state) => state.selectedSeed);
  const results = useSearchStore((state) => state.results);
  const geysers = useSearchStore((state) => state.geysers);

  const previewSessionKey = usePreviewStore((state) => state.activeKey);
  const loadByMatch = usePreviewStore((state) => state.loadByMatch);
  const preview = usePreviewStore((state) => state.activePreview);
  const activeGeyserDetailsStatus = usePreviewStore((state) => state.activeGeyserDetailsStatus);
  const activeGeyserDetails = usePreviewStore((state) => state.activeGeyserDetails);
  const activeGeyserDetailsError = usePreviewStore((state) => state.activeGeyserDetailsError);
  const isLoading = usePreviewStore((state) => state.isLoading);
  const lastError = usePreviewStore((state) => state.lastError);
  const clearError = usePreviewStore((state) => state.clearError);
  const canvasRef = useRef<PreviewCanvasHandle | null>(null);
  const previewCanvasContainerRef = useRef<HTMLDivElement | null>(null);

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
  const [selectedGeyserAnchor, setSelectedGeyserAnchor] = useState<GeyserParameterAnchor | null>(null);
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

  useEffect(() => {
    if (preview && selectedGeyserIndex !== null) {
      return;
    }
    setSelectedGeyserAnchor(null);
  }, [preview, selectedGeyserIndex]);

  const handleExportPng = () => {
    usePreviewStore.setState({ lastError: null });
    void canvasRef.current?.exportPng().catch((error) => {
      usePreviewStore.setState({
        lastError: `导出 PNG 失败: ${formatTauriError(error)}`,
      });
    });
  };

  const selectedGeyser =
    selectedGeyserIndex === null ? null : preview?.summary.geysers[selectedGeyserIndex] ?? null;
  const selectedGeyserDetail =
    selectedGeyserIndex === null
      ? null
      : activeGeyserDetails.find((detail) => detail.index === selectedGeyserIndex) ?? null;

  const handleRetrySelectedGeyserDetails = () => {
    if (!selectedMatch) {
      return;
    }
    void loadByMatch(selectedMatch);
  };

  const previewCanvasContainer = previewCanvasContainerRef.current;

  return (
    <section className="preview-pane">
      <header className="preview-pane-header">
        <Typography.Title level={3}>地图预览</Typography.Title>
        <ThemeModeToggle mode={themeMode} onModeChange={onThemeModeChange} />
      </header>
      {isLoading ? (
        <Alert className="preview-pane-alert" type="info" showIcon title="预览加载中..." />
      ) : null}
      {lastError ? (
        <Alert
          className="preview-pane-alert"
          type="error"
          showIcon
          closable
          title={`预览失败: ${lastError}`}
          onClose={clearError}
        />
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
        onExportPng={handleExportPng}
        onOpenGeyserList={() => setShowGeyserList(true)}
      />
      <div className="preview-canvas-container" ref={previewCanvasContainerRef}>
        <PreviewCanvas
          ref={canvasRef}
          themeMode={themeMode}
          sessionKey={previewSessionKey}
          preview={preview}
          geysers={geysers}
          showBoundaries={showBoundaries}
          showLabels={showLabels}
          showGeysers={showGeysers}
          selectedGeyserIndex={selectedGeyserIndex}
          onHoverRegionChange={setHoveredRegion}
          onSelectedRegionChange={setSelectedRegion}
          onHoverGeyserChange={setHoverGeyserIndex}
          onSelectedGeyserChange={setSelectedGeyserIndex}
          onSelectedGeyserAnchorChange={setSelectedGeyserAnchor}
        />
        <PreviewLegend />
        <GeyserParameterPopover
          anchor={selectedGeyserAnchor}
          popupContainer={previewCanvasContainer}
          geyser={selectedGeyser}
          geyserDetail={selectedGeyserDetail}
          geyserDetailsStatus={activeGeyserDetailsStatus}
          geyserDetailsError={activeGeyserDetailsError}
          onClose={() => {
            setSelectedGeyserIndex(null);
            setSelectedGeyserAnchor(null);
          }}
          onRetry={handleRetrySelectedGeyserDetails}
        />
        {showGeyserList && preview ? (
          <GeyserListOverlay
            geysersData={preview.summary.geysers}
            geyserDetails={activeGeyserDetails}
            geyserDetailsStatus={activeGeyserDetailsStatus}
            popupContainer={previewCanvasContainer}
            onClose={() => setShowGeyserList(false)}
          />
        ) : null}
      </div>
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
