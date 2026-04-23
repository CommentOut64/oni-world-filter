import type { ThemeConfig } from "antd";
import { theme } from "antd";

export type DesktopThemeMode = "light" | "dark";
type MatchMediaFn = (query: string) => { matches: boolean };
type ThemeTarget = Pick<HTMLElement, "setAttribute">;

const DESKTOP_THEME_TOKENS = {
  dark: {
    colorBgBase: "#08111b",
    colorBgLayout: "#0b1624",
    colorBgContainer: "#0f1b2d",
    colorBgElevated: "#132235",
    colorBorder: "#28445f",
    colorText: "#e7f0fa",
    colorTextSecondary: "#9fb4c9",
    colorPrimary: "#3b82f6",
    colorInfo: "#38bdf8",
    colorSuccess: "#22c55e",
    colorWarning: "#f59e0b",
    colorError: "#f87171",
  },
  light: {
    colorBgBase: "#f5f5f7",
    colorBgLayout: "#ececef",
    colorBgContainer: "#ffffff",
    colorBgElevated: "#ffffff",
    colorBorder: "#d4d4d8",
    colorText: "#18181b",
    colorTextSecondary: "#52525b",
    colorPrimary: "#2563eb",
    colorInfo: "#0369a1",
    colorSuccess: "#15803d",
    colorWarning: "#b45309",
    colorError: "#b91c1c",
  },
} as const;

const COMMON_TOKENS = {
  borderRadius: 14,
  borderRadiusLG: 18,
  borderRadiusSM: 10,
  controlHeight: 36,
  controlHeightLG: 42,
  controlHeightSM: 30,
  fontFamily: '"Segoe UI", "PingFang SC", "Microsoft YaHei", sans-serif',
} as const;

export function getPreferredDesktopThemeMode(matchMediaFn?: MatchMediaFn): DesktopThemeMode {
  if (!matchMediaFn) {
    return "dark";
  }

  return matchMediaFn("(prefers-color-scheme: dark)").matches ? "dark" : "light";
}

export function syncDesktopThemeMode(mode: DesktopThemeMode, target?: ThemeTarget) {
  target?.setAttribute("data-theme", mode);
}

export function createDesktopTheme(mode: DesktopThemeMode): ThemeConfig {
  const isDark = mode === "dark";

  return {
    algorithm: isDark ? theme.darkAlgorithm : theme.defaultAlgorithm,
    cssVar: {
      key: "oni-desktop",
    },
    token: {
      ...DESKTOP_THEME_TOKENS[mode],
      ...COMMON_TOKENS,
      boxShadow: isDark
        ? "0 16px 36px rgba(2, 8, 16, 0.34), 0 2px 10px rgba(4, 12, 22, 0.22)"
        : "0 12px 28px rgba(15, 17, 21, 0.08), 0 2px 6px rgba(15, 17, 21, 0.06)",
      boxShadowSecondary: isDark
        ? "0 22px 48px rgba(2, 8, 16, 0.42), 0 8px 18px rgba(4, 12, 22, 0.28)"
        : "0 18px 40px rgba(15, 17, 21, 0.10), 0 6px 14px rgba(15, 17, 21, 0.08)",
    },
    components: {
      Alert: {
        withDescriptionPadding: 14,
      },
      Button: isDark
        ? {
            borderRadius: 10,
            contentFontSize: 13,
            contentFontSizeLG: 14,
            controlHeight: 36,
            defaultBorderColor: "#36556f",
            defaultBg: "#14263a",
            defaultColor: "#e7f0fa",
            defaultHoverBg: "#19324b",
            defaultHoverBorderColor: "#4a7193",
            defaultHoverColor: "#ffffff",
            primaryShadow: "none",
          }
        : {
            borderRadius: 10,
            contentFontSize: 13,
            contentFontSizeLG: 14,
            controlHeight: 36,
            defaultBorderColor: "#d4d4d8",
            defaultBg: "#ffffff",
            defaultColor: "#18181b",
            defaultHoverBg: "#f4f4f5",
            defaultHoverBorderColor: "#a1a1aa",
            defaultHoverColor: "#09090b",
            primaryShadow: "none",
          },
      Card: isDark
        ? {
            bodyPadding: 16,
            borderRadiusLG: 16,
            colorBorderSecondary: "#2d4862",
            colorBgContainer: "#0f1b2d",
            headerBg: "transparent",
            headerHeight: 48,
          }
        : {
            bodyPadding: 16,
            borderRadiusLG: 16,
            colorBorderSecondary: "#e4e4e7",
            colorBgContainer: "#ffffff",
            headerBg: "transparent",
            headerHeight: 48,
          },
      Collapse: isDark
        ? {
            borderRadiusLG: 14,
            contentBg: "#0b1624",
            headerBg: "#0f1b2d",
          }
        : {
            borderRadiusLG: 14,
            contentBg: "#fafafa",
            headerBg: "#ffffff",
          },
      Descriptions: {
        colorTextSecondary: DESKTOP_THEME_TOKENS[mode].colorTextSecondary,
      },
      Input: isDark
        ? {
            activeBg: "#132235",
            colorBgContainer: "#132235",
            colorBorder: "#36556f",
            colorTextPlaceholder: "#71869a",
            hoverBorderColor: "#4a7193",
          }
        : {
            activeBg: "#ffffff",
            colorBgContainer: "#ffffff",
            colorBorder: "#d4d4d8",
            colorTextPlaceholder: "#a1a1aa",
            hoverBorderColor: "#71717a",
          },
      InputNumber: isDark
        ? {
            activeBg: "#132235",
            colorBgContainer: "#132235",
            colorBorder: "#36556f",
            hoverBorderColor: "#4a7193",
          }
        : {
            activeBg: "#ffffff",
            colorBgContainer: "#ffffff",
            colorBorder: "#d4d4d8",
            hoverBorderColor: "#71717a",
          },
      Modal: isDark
        ? {
            borderRadiusLG: 18,
            contentBg: "#101d30",
            headerBg: "transparent",
            titleColor: "#e7f0fa",
          }
        : {
            borderRadiusLG: 18,
            contentBg: "#ffffff",
            headerBg: "transparent",
            titleColor: "#18181b",
          },
      Segmented: isDark
        ? {
            itemActiveBg: "#183252",
            itemColor: "#9fb4c9",
            itemHoverBg: "#15273b",
            itemHoverColor: "#f4f8fb",
            itemSelectedBg: "#23456d",
            itemSelectedColor: "#ffffff",
            trackBg: "#0d1828",
          }
        : {
            itemActiveBg: "#e4e4e7",
            itemColor: "#52525b",
            itemHoverBg: "#f4f4f5",
            itemHoverColor: "#18181b",
            itemSelectedBg: "#ffffff",
            itemSelectedColor: "#09090b",
            trackBg: "#ececef",
          },
      Select: isDark
        ? {
            activeBorderColor: "#4a7193",
            activeOutlineColor: "rgba(59, 130, 246, 0.22)",
            colorBgContainer: "#132235",
            colorBorder: "#36556f",
            colorTextPlaceholder: "#71869a",
            optionActiveBg: "#15273b",
            optionSelectedBg: "#183252",
          }
        : {
            activeBorderColor: "#71717a",
            activeOutlineColor: "rgba(37, 99, 235, 0.16)",
            colorBgContainer: "#ffffff",
            colorBorder: "#d4d4d8",
            colorTextPlaceholder: "#a1a1aa",
            optionActiveBg: "#f4f4f5",
            optionSelectedBg: "#e4ebf7",
          },
      Table: isDark
        ? {
            bodySortBg: "#122238",
            borderColor: "#2d4862",
            colorBgContainer: "#0f1b2d",
            colorFillAlter: "#122238",
            headerBg: "#132235",
            headerColor: "#e7f0fa",
            rowHoverBg: "#13253b",
            rowSelectedBg: "#173557",
            rowSelectedHoverBg: "#1b3c62",
          }
        : {
            bodySortBg: "#f4f4f5",
            borderColor: "#e4e4e7",
            colorBgContainer: "#ffffff",
            colorFillAlter: "#fafafa",
            headerBg: "#f4f4f5",
            headerColor: "#18181b",
            rowHoverBg: "#f4f4f5",
            rowSelectedBg: "#e4ebf7",
            rowSelectedHoverBg: "#d6e0f3",
          },
      Tabs: isDark
        ? {
            cardBg: "#0d1828",
            itemColor: "#9fb4c9",
            itemHoverColor: "#f4f8fb",
            itemSelectedColor: "#ffffff",
          }
        : {
            cardBg: "#fafafa",
            itemColor: "#52525b",
            itemHoverColor: "#18181b",
            itemSelectedColor: "#09090b",
          },
      Tooltip: {
        colorBgSpotlight: isDark ? "#16273c" : "#27272a",
      },
    },
  };
}
