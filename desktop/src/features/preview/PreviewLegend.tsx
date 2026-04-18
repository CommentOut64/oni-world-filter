export default function PreviewLegend() {
  return (
    <section className="preview-legend">
      <span className="legend-item"><span className="dot region" />区域</span>
      <span className="legend-item"><span className="dot start" />起点</span>
      <span className="legend-item"><span className="dot geyser" />喷口</span>
      <span className="legend-item"><span className="dot focus" />高亮/选中</span>
      <span className="legend-item"><span className="dot boundary" />边界</span>
    </section>
  );
}
