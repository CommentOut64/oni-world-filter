import type { WorldReportViewModel } from "./reportTypes.ts";
import { REPORT_PRINT_CSS } from "./reportCss.ts";

function escapeHtml(value: string): string {
  return value
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#39;");
}

function renderSummaryCard(label: string, value: string, fullWidth = false): string {
  return `
    <section class="card${fullWidth ? " card--full" : ""}">
      <p class="label">${escapeHtml(label)}</p>
      <p class="value">${escapeHtml(value)}</p>
    </section>
  `;
}

export function buildWorldReportHtml(viewModel: WorldReportViewModel, mapImageDataUrl: string): string {
  const geyserRows = viewModel.geyserRows
    .map(
      (row) => `
        <tr>
          <td>${escapeHtml(row.name)}</td>
          <td class="geyser-coord">${escapeHtml(row.coord)}</td>
          <td>${escapeHtml(row.temperature)}</td>
          <td>${escapeHtml(row.eruptionRate)}</td>
          <td>${escapeHtml(row.averageYield)}</td>
          <td>${escapeHtml(row.eruptionWindow)}</td>
          <td>${escapeHtml(row.activeWindow)}</td>
        </tr>
      `
    )
    .join("");

  return `<!DOCTYPE html>
<html lang="zh-CN">
  <head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <title>坐标地图报告</title>
    <style>${REPORT_PRINT_CSS}</style>
  </head>
  <body>
    <main class="report">
      <section class="page page--summary">
        <header class="hero">
          <h1>坐标地图报告</h1>
          <p>用于导出世界分类、具体世界、完整坐标、混搭摘要、固定宽度地图和喷口参数。</p>
        </header>

        <section class="summary-grid">
          ${renderSummaryCard("世界分类", viewModel.worldCategoryLabel)}
          ${renderSummaryCard("具体世界", viewModel.worldName)}
          ${renderSummaryCard("随机种子", viewModel.seedLabel)}
          ${renderSummaryCard("世界尺寸", viewModel.worldSizeLabel)}
          ${renderSummaryCard("起点坐标", viewModel.startLabel)}
          ${renderSummaryCard("完整坐标", viewModel.coord)}
          ${renderSummaryCard("DLC 混搭", viewModel.mixingSummary, true)}
        </section>

        <section class="map-card">
          <p class="label">地图截图</p>
          <div class="map-frame">
            <img class="map-image" src="${escapeHtml(mapImageDataUrl)}" alt="坐标地图截图" />
          </div>
        </section>
      </section>

      <section class="page">
        <header>
          <h2 class="section-title">喷口详细参数</h2>
          <p class="section-desc">包含喷口名称、地图坐标、温度、喷发速率、平均产量与活动窗口。</p>
        </header>

        <section class="table-wrap">
          <table>
            <thead>
              <tr>
                <th>喷口名称</th>
                <th>地图坐标</th>
                <th>温度</th>
                <th>喷发速率</th>
                <th>平均产量</th>
                <th>喷发窗口</th>
                <th>活跃窗口</th>
              </tr>
            </thead>
            <tbody>${geyserRows}</tbody>
          </table>
        </section>
      </section>
    </main>
  </body>
</html>`;
}
