import React from "react";
import { Button, Switch, Tooltip, Typography } from "antd";

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
        <Typography.Text>标签</Typography.Text>
        <Switch checked={showLabels} onChange={onToggleLabels} />
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
      <Tooltip title="导出当前视图为 PNG 图片">
        <Button htmlType="button" onClick={onExportPng}>
          导出 PNG
        </Button>
      </Tooltip>
    </section>
  );
}
