import type { ThemeConfig } from "antd";
import { theme } from "antd";

export const DESKTOP_THEME_TOKENS = {
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
} as const;

export const desktopTheme: ThemeConfig = {
  algorithm: theme.darkAlgorithm,
  cssVar: {
    key: "oni-desktop",
  },
  token: {
    ...DESKTOP_THEME_TOKENS,
    borderRadius: 14,
    borderRadiusLG: 18,
    borderRadiusSM: 10,
    boxShadow:
      "0 16px 36px rgba(2, 8, 16, 0.34), 0 2px 10px rgba(4, 12, 22, 0.22)",
    boxShadowSecondary:
      "0 22px 48px rgba(2, 8, 16, 0.42), 0 8px 18px rgba(4, 12, 22, 0.28)",
    controlHeight: 36,
    controlHeightLG: 42,
    controlHeightSM: 30,
    fontFamily: '"Segoe UI", "PingFang SC", "Microsoft YaHei", sans-serif',
  },
  components: {
    Alert: {
      withDescriptionPadding: 14,
    },
    Button: {
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
    },
    Card: {
      bodyPadding: 16,
      borderRadiusLG: 16,
      colorBorderSecondary: "#2d4862",
      colorBgContainer: "#0f1b2d",
      headerBg: "transparent",
      headerHeight: 48,
    },
    Collapse: {
      borderRadiusLG: 14,
      contentBg: "#0b1624",
      headerBg: "#0f1b2d",
    },
    Descriptions: {
      colorTextSecondary: "#9fb4c9",
    },
    Input: {
      activeBg: "#132235",
      colorBgContainer: "#132235",
      colorBorder: "#36556f",
      colorTextPlaceholder: "#71869a",
      hoverBorderColor: "#4a7193",
    },
    InputNumber: {
      activeBg: "#132235",
      colorBgContainer: "#132235",
      colorBorder: "#36556f",
      hoverBorderColor: "#4a7193",
    },
    Modal: {
      borderRadiusLG: 18,
      contentBg: "#101d30",
      headerBg: "transparent",
      titleColor: "#e7f0fa",
    },
    Segmented: {
      itemActiveBg: "#183252",
      itemColor: "#9fb4c9",
      itemHoverBg: "#15273b",
      itemHoverColor: "#f4f8fb",
      itemSelectedBg: "#23456d",
      itemSelectedColor: "#ffffff",
      trackBg: "#0d1828",
    },
    Select: {
      activeBorderColor: "#4a7193",
      activeOutlineColor: "rgba(59, 130, 246, 0.22)",
      colorBgContainer: "#132235",
      colorBorder: "#36556f",
      colorTextPlaceholder: "#71869a",
      optionActiveBg: "#15273b",
      optionSelectedBg: "#183252",
    },
    Table: {
      bodySortBg: "#122238",
      borderColor: "#2d4862",
      colorBgContainer: "#0f1b2d",
      colorFillAlter: "#122238",
      headerBg: "#132235",
      headerColor: "#e7f0fa",
      rowHoverBg: "#13253b",
      rowSelectedBg: "#173557",
      rowSelectedHoverBg: "#1b3c62",
    },
    Tabs: {
      cardBg: "#0d1828",
      itemColor: "#9fb4c9",
      itemHoverColor: "#f4f8fb",
      itemSelectedColor: "#ffffff",
    },
    Tooltip: {
      colorBgSpotlight: "#16273c",
    },
  },
};
