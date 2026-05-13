import React, { useEffect, useRef, useState } from "react";
import { Alert, Segmented, message, Typography } from "antd";

import type { DesktopThemeMode } from "../../app/antdTheme";
import type { PreviewTarget } from "../../lib/contracts.ts";
import ThemeModeToggle from "../../components/layout/ThemeModeToggle";
import { formatTauriError } from "../../lib/tauri.ts";
import { usePreviewStore } from "../../state/previewStore";
import { previewBaseKey } from "../../state/previewStoreState.ts";
import { useSearchStore } from "../../state/searchStore";
import { findCategoryForWorld } from "../search/worldParameterUi.ts";
import { exportWorldReport } from "../report/exportWorldReport.ts";
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
  const catalog = useSearchStore((state) => state.catalog);
  const worlds = useSearchStore((state) => state.worlds);

  const previewSessionKey = usePreviewStore((state) => state.activeKey);
  const activeTarget = usePreviewStore((state) => state.activeTarget);
  const loadByMatch = usePreviewStore((state) => state.loadByMatch);
  const setActiveTarget = usePreviewStore((state) => state.setActiveTarget);
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
  const [isGeneratingReport, setIsGeneratingReport] = useState(false);

  const selectedMatch =
    selectedSeed === null
      ? null
      : results.find((item) => item.seed === selectedSeed) ?? null;
  const selectedWorldCategory = selectedMatch
    ? findCategoryForWorld(worlds, selectedMatch.worldType)
    : null;
  const isMoonletResult = selectedWorldCategory === "moonletCluster";
  const selectedMatchBaseKey = selectedMatch
    ? `${selectedMatch.worldType}:${selectedMatch.seed}:${selectedMatch.mixing}`
    : null;
  const hasSecondaryPreview = Boolean(
    selectedMatchBaseKey &&
      previewBaseKey(previewSessionKey) === selectedMatchBaseKey &&
      preview?.summary.hasSecondaryPreview
  );
  const showWorldSwitch = Boolean(isMoonletResult || hasSecondaryPreview);
  const secondarySwitchDisabled = !hasSecondaryPreview;

  useEffect(() => {
    if (!selectedMatch) {
      return;
    }
    void loadByMatch(selectedMatch, "primary");
  }, [selectedMatch, loadByMatch]);

  useEffect(() => {
    setHoveredRegion(null);
    setSelectedRegion(null);
    setHoverGeyserIndex(null);
    setSelectedGeyserIndex(null);
    setSelectedGeyserAnchor(null);
    setShowGeyserList(false);
  }, [activeTarget]);

  useEffect(() => {
    if (preview && selectedGeyserIndex !== null) {
      return;
    }
    setSelectedGeyserAnchor(null);
  }, [preview, selectedGeyserIndex]);

  const handleGenerateReport = () => {
    if (!selectedMatch) {
      return;
    }
    if (activeTarget !== "primary") {
      usePreviewStore.setState({
        lastError: "副星预览不支持生成报告，请先切回主星。",
      });
      return;
    }
    usePreviewStore.setState({ lastError: null });
    setIsGeneratingReport(true);
    void (async () => {
      try {
        const exported = await exportWorldReport({
          match: selectedMatch,
          geysers,
          catalog,
          themeMode,
        });
        if (exported) {
          void message.success("报告已生成。");
        }
      } catch (error) {
        usePreviewStore.setState({
          lastError: `生成报告失败: ${formatTauriError(error)}`,
        });
      } finally {
        setIsGeneratingReport(false);
      }
    })();
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
    void loadByMatch(selectedMatch, activeTarget);
  };
  const worldSwitchOptions: Array<{
    label: string;
    value: PreviewTarget;
    disabled?: boolean;
  }> = [
    { label: "主星", value: "primary" },
    { label: "副星", value: "secondary", disabled: secondarySwitchDisabled },
  ];

  const previewCanvasContainer = previewCanvasContainerRef.current;

  return (
    <section className="preview-pane">
      <header className="preview-pane-header">
        <div className="preview-pane-title-row">
          <Typography.Title level={3}>地图预览</Typography.Title>
          {showWorldSwitch ? (
            <Segmented<PreviewTarget>
              className="theme-toggle preview-world-switch"
              value={activeTarget}
              options={worldSwitchOptions}
              onChange={(value) => {
                if (value === "primary") {
                  setActiveTarget("primary");
                  return;
                }
                if (!selectedMatch) {
                  return;
                }
                void loadByMatch(selectedMatch, "secondary");
              }}
            />
          ) : null}
        </div>
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
          title={`操作失败: ${lastError}`}
          onClose={clearError}
        />
      ) : null}
      <PreviewToolbar
        showBoundaries={showBoundaries}
        showLabels={showLabels}
        showGeysers={showGeysers}
        geyserCount={preview?.summary.geysers.length ?? 0}
        isGeneratingReport={isGeneratingReport}
        onToggleBoundaries={() => setShowBoundaries((current) => !current)}
        onToggleLabels={() => setShowLabels((current) => !current)}
        onToggleGeysers={() => setShowGeysers((current) => !current)}
        onResetView={() => canvasRef.current?.resetView()}
        onGenerateReport={handleGenerateReport}
        onOpenGeyserList={() => setShowGeyserList(true)}
      />
      <div className="preview-canvas-container" ref={previewCanvasContainerRef}>
        <PreviewCanvas
          ref={canvasRef}
          themeMode={themeMode}
          sessionKey={previewSessionKey}
          preview={preview}
          geysers={geysers}
          geyserPopoverEnabled={activeTarget === "primary"}
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
            activeTarget={activeTarget}
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
