import React from "react";
import { Card, Tag } from "antd";

export default function PreviewLegend() {
  void React;
  return (
    <Card size="small" className="preview-legend-card">
      <section className="preview-legend">
        <Tag className="legend-item" variant="filled">
          <span className="dot region" />
          区域
        </Tag>
        <Tag className="legend-item" variant="filled">
          <span className="dot start" />
          起点
        </Tag>
        <Tag className="legend-item" variant="filled">
          <span className="dot geyser" />
          喷口
        </Tag>
        <Tag className="legend-item" variant="filled">
          <span className="dot focus" />
          高亮/选中
        </Tag>
        <Tag className="legend-item" variant="filled">
          <span className="dot boundary" />
          边界
        </Tag>
      </section>
    </Card>
  );
}
