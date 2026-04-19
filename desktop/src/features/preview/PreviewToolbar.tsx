interface PreviewToolbarProps {
  showBoundaries: boolean;
  showLabels: boolean;
  showGeysers: boolean;
  geyserCount: number;
  onToggleBoundaries: () => void;
  onToggleLabels: () => void;
  onToggleGeysers: () => void;
  onResetView: () => void;
  onExportPng: () => void;
  onOpenGeyserList: () => void;
}

export default function PreviewToolbar({
  showBoundaries,
  showLabels,
  showGeysers,
  geyserCount,
  onToggleBoundaries,
  onToggleLabels,
  onToggleGeysers,
  onResetView,
  onExportPng,
  onOpenGeyserList,
}: PreviewToolbarProps) {
  return (
    <section className="preview-toolbar">
      <button type="button" onClick={onOpenGeyserList}>
        喷口列表 ({geyserCount})
      </button>
      <button type="button" className={showBoundaries ? "active" : ""} onClick={onToggleBoundaries}>
        边界
      </button>
      <button type="button" className={showLabels ? "active" : ""} onClick={onToggleLabels}>
        标签
      </button>
      <button type="button" className={showGeysers ? "active" : ""} onClick={onToggleGeysers}>
        喷口
      </button>
      <button type="button" onClick={onResetView}>
        重置视图
      </button>
      <button type="button" onClick={onExportPng}>
        导出 PNG
      </button>
    </section>
  );
}
