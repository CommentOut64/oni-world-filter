import type { DesktopThemeMode } from "../../app/antdTheme";

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

// 每种 ZoneType 的填充色，基于游戏原始色相提亮以适配预览
// 索引与 C++ ZoneType 枚举一一对应
const ZONE_FILL: string[] = [
  "#a8b4bc", // 0  苔原
  "#4a4a6e", // 1  水晶洞穴
  "#9da87e", // 2  湿地
  "#b89a6a", // 3  砂岩
  "#8e8e62", // 4  丛林
  "#e85540", // 5  岩浆
  "#7e6860", // 6  油质
  "#3a5068", // 7  太空
  "#b08478", // 8  海洋
  "#be6e94", // 9  铁锈
  "#82a06e", // 10 森林
  "#50cc3e", // 11 辐射
  "#c4c420", // 12 沼泽
  "#d4b872", // 13 荒地
  "#4a5e6e", // 14 火箭内部
  "#daba62", // 15 金属
  "#9a9ea0", // 16 岩漠
  "#f0be48", // 17 海牛
  "#78c8e4", // 18 冰窟
  "#5a72ac", // 19 冷池
  "#e4d8a8", // 20 花蜜
  "#98b07e", // 21 花园
  "#9a98ac", // 22 寒羽
  "#a0ae84", // 23 险沼
];

const PREVIEW_PALETTE_BY_MODE: Record<DesktopThemeMode, PreviewPalette> = {
  dark: {
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
  },
  light: {
    background: "#f4f4f6",
    boundary: "rgba(120, 125, 135, 0.55)",
    regionHover: "rgba(180, 83, 9, 0.18)",
    regionStrokeHover: "rgba(180, 83, 9, 0.78)",
    regionSelected: "rgba(37, 99, 235, 0.16)",
    regionStrokeSelected: "rgba(37, 99, 235, 0.78)",
    startMarker: "#15803d",
    geyserMarker: "#ca8a04",
    geyserHover: "#ea580c",
    geyserSelected: "#c2410c",
    label: "rgba(55, 58, 65, 0.88)",
    worldBorder: "rgba(120, 125, 135, 0.55)",
  },
};

export function createPreviewPalette(mode: DesktopThemeMode): PreviewPalette {
  return PREVIEW_PALETTE_BY_MODE[mode];
}

export function zoneFillColor(zoneType: number, mode: DesktopThemeMode = "dark"): string {
  const base = ZONE_FILL[zoneType] ?? "#69777f";
  const alpha = mode === "light" ? "E6" : "CC";
  return `${base}${alpha}`;
}
