export interface PreviewPalette {
  background: string;
  boundary: string;
  regionHover: string;
  regionStrokeHover: string;
  startMarker: string;
  geyserMarker: string;
  geyserHover: string;
  geyserSelected: string;
  label: string;
  worldBorder: string;
}

const REGION_FILL = [
  "#2f5d7f",
  "#356f70",
  "#4f6f3a",
  "#805d48",
  "#645086",
  "#905163",
  "#5b7288",
  "#7c8a3d",
];

export const previewPalette: PreviewPalette = {
  background: "#0f1726",
  boundary: "rgba(182, 213, 238, 0.45)",
  regionHover: "rgba(251, 191, 36, 0.22)",
  regionStrokeHover: "rgba(251, 191, 36, 0.8)",
  startMarker: "#22c55e",
  geyserMarker: "#facc15",
  geyserHover: "#fb923c",
  geyserSelected: "#f97316",
  label: "rgba(230, 244, 255, 0.92)",
  worldBorder: "rgba(103, 149, 183, 0.5)",
};

export function zoneFillColor(zoneType: number): string {
  const base = REGION_FILL[Math.abs(zoneType) % REGION_FILL.length];
  return `${base}B3`;
}
