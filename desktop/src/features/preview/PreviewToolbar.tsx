import React from "react";
import { Button, Switch, Tooltip, Typography } from "antd";

interface PreviewToolbarProps {
  showBoundaries: boolean;
  showBiomes: boolean;
  showGeysers: boolean;
  geyserCount: number;
  isGeneratingReport: boolean;
  onToggleBoundaries: () => void;
  onToggleBiomes: () => void;
  onToggleGeysers: () => void;
  onResetView: () => void;
  onGenerateReport: () => void;
  onOpenGeyserList: () => void;
}

export default function PreviewToolbar({
  showBoundaries,
  showBiomes,
  showGeysers,
  geyserCount,
  isGeneratingReport,
  onToggleBoundaries,
  onToggleBiomes,
  onToggleGeysers,
  onResetView,
  onGenerateReport,
  onOpenGeyserList,
}: PreviewToolbarProps) {
  void React;
  return (
    <section className="preview-toolbar">
      <Tooltip title="打开当前地图中的喷口列表">
        <Button htmlType="button" onClick={onOpenGeyserList}>
          喷口列表 ({geyserCount})
        </Button>
      </Tooltip>
      <div className="preview-toolbar-toggle">
        <Typography.Text>边界</Typography.Text>
        <Switch checked={showBoundaries} onChange={onToggleBoundaries} />
      </div>
      <div className="preview-toolbar-toggle">
        <Typography.Text>生态</Typography.Text>
        <Switch checked={showBiomes} onChange={onToggleBiomes} />
      </div>
      <div className="preview-toolbar-toggle">
        <Typography.Text>喷口</Typography.Text>
        <Switch checked={showGeysers} onChange={onToggleGeysers} />
      </div>
      <Tooltip title="回到适合当前地图的默认视图">
        <Button htmlType="button" onClick={onResetView}>
          重置视图
        </Button>
      </Tooltip>
      <Tooltip title="生成当前地图的 PDF 报告">
        <Button
          htmlType="button"
          onClick={onGenerateReport}
          loading={isGeneratingReport}
          disabled={isGeneratingReport}
        >
          生成报告
        </Button>
      </Tooltip>
    </section>
  );
}
