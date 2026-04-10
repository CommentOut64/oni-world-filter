export default function PreviewLegend() {
  return (
    <section className="preview-legend">
      <h4>图例</h4>
      <ul>
        <li>
          <span className="dot region" />
          区域填充层
        </li>
        <li>
          <span className="dot start" />
          起点
        </li>
        <li>
          <span className="dot geyser" />
          喷口标记
        </li>
        <li>
          <span className="dot focus" />
          当前高亮/选中
        </li>
        <li>
          <span className="dot boundary" />
          区域边界层
        </li>
      </ul>
    </section>
  );
}
