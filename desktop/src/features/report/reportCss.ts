export const REPORT_PRINT_CSS = `
@page {
  size: A4 portrait;
  margin: 12mm;
}

:root {
  color-scheme: light;
  print-color-adjust: exact;
  -webkit-print-color-adjust: exact;
  font-family: "Microsoft YaHei", "Noto Sans SC", sans-serif;
  color: #1f2937;
}

* {
  box-sizing: border-box;
}

body {
  margin: 0;
  print-color-adjust: exact;
  -webkit-print-color-adjust: exact;
}

.report {
  width: 100%;
}

.page {
  min-height: calc(297mm - 24mm);
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.page--summary {
  break-after: page;
}

.hero {
  padding: 0 0 4px;
}

.hero h1 {
  margin: 0 0 4px;
  font-size: 22px;
}

.hero p {
  margin: 0;
  font-size: 12px;
  color: #5b6470;
}

.summary-grid {
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: 8px;
}

.card {
  border: 1px solid #d8c9b5;
  border-radius: 10px;
  padding: 8px 10px;
}

.card--full {
  grid-column: 1 / -1;
}

.label {
  margin: 0 0 6px;
  font-size: 11px;
  font-weight: 700;
  color: #7c5d3b;
  letter-spacing: 0.06em;
}

.value {
  margin: 0;
  font-size: 13px;
  line-height: 1.35;
  word-break: break-word;
}

.map-card {
  display: flex;
  flex-direction: column;
  gap: 6px;
  min-height: 0;
  flex: 1 1 auto;
}

.map-frame {
  display: flex;
  align-items: center;
  justify-content: center;
  min-height: 0;
  flex: 1 1 auto;
}

.map-image {
  display: block;
  width: 100%;
  border: 1px solid #d9d4cc;
  max-height: 172mm;
  object-fit: contain;
}

.section-title {
  margin: 0;
  font-size: 20px;
  color: #12303a;
}

.section-desc {
  margin: 4px 0 0;
  font-size: 12px;
  color: #5b6470;
}

.table-wrap {
  border: 1px solid #d8c9b5;
  border-radius: 14px;
  overflow: hidden;
  background: rgba(255, 252, 247, 0.94);
}

table {
  width: 100%;
  border-collapse: collapse;
}

thead {
  background: #e6d5bf;
}

th,
td {
  padding: 10px 8px;
  border-bottom: 1px solid #eadfce;
  font-size: 11px;
  text-align: left;
  vertical-align: top;
}

th {
  color: #533c23;
  font-weight: 700;
}

.geyser-coord {
  white-space: nowrap;
}

tbody tr:nth-child(even) {
  background: rgba(243, 234, 220, 0.5);
}
`;
