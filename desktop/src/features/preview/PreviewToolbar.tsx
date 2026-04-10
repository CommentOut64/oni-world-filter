interface PreviewToolbarProps {
  showBoundaries: boolean;
  showLabels: boolean;
  showGeysers: boolean;
  onToggleBoundaries: () => void;
  onToggleLabels: () => void;
  onToggleGeysers: () => void;
  onResetView: () => void;
  onExportPng: () => void;
}

export default function PreviewToolbar({
  showBoundaries,
  showLabels,
  showGeysers,
  onToggleBoundaries,
  onToggleLabels,
  onToggleGeysers,
  onResetView,
  onExportPng,
}: PreviewToolbarProps) {
  return (
    <section className="preview-toolbar">
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
