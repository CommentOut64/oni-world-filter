export interface PreviewPalette {
  background: string;
  boundary: string;
  regionHover: string;
  regionStrokeHover: string;
  regionSelected: string;
  regionStrokeSelected: string;
  startMarker: string;
  geyserMarker: string;
  geyserHover: string;
  geyserSelected: string;
  label: string;
  worldBorder: string;
}

// 每种 ZoneType 的填充色，从旧前端 asset/zones.png sprite 贴图提取
// 索引与 C++ ZoneType 枚举一一对应
const ZONE_FILL: string[] = [
  "#69777f", // 0  苔原
  "#1a1a2e", // 1  水晶洞穴（贴图纯黑，提亮以区分背景）
  "#7d8765", // 2  湿地
  "#987c50", // 3  砂岩
  "#6b6b47", // 4  丛林
  "#d73518", // 5  岩浆
  "#58433f", // 6  油质
  "#141e28", // 7  太空（贴图纯黑，提亮以区分背景）
  "#8e6459", // 8  海洋
  "#9d4d73", // 9  铁锈
  "#658258", // 10 森林
  "#39af25", // 11 辐射
  "#a4a406", // 12 沼泽
  "#958782", // 13 荒地
  "#1e2830", // 14 火箭内部（贴图纯黑，提亮以区分背景）
  "#c2a34e", // 15 金属
  "#92837e", // 16 岩漠
  "#ecaa2f", // 17 海牛
  "#6a7982", // 18 冰窟
  "#616f78", // 19 冷池
  "#85939f", // 20 花蜜
  "#7e9267", // 21 花园
  "#7e7c8f", // 22 寒羽
  "#88946f", // 23 险沼
];

export const previewPalette: PreviewPalette = {
  background: "#0f1726",
  boundary: "rgba(182, 213, 238, 0.45)",
  regionHover: "rgba(251, 191, 36, 0.22)",
  regionStrokeHover: "rgba(251, 191, 36, 0.8)",
  regionSelected: "rgba(59, 130, 246, 0.25)",
  regionStrokeSelected: "rgba(59, 130, 246, 0.9)",
  startMarker: "#22c55e",
  geyserMarker: "#facc15",
  geyserHover: "#fb923c",
  geyserSelected: "#f97316",
  label: "rgba(230, 244, 255, 0.92)",
  worldBorder: "rgba(103, 149, 183, 0.5)",
};

export function zoneFillColor(zoneType: number): string {
  const base = ZONE_FILL[zoneType] ?? "#69777f";
  return `${base}B3`;
}
