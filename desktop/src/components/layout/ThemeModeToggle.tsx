import { Segmented } from "antd";

import type { DesktopThemeMode } from "../../app/antdTheme";

interface ThemeModeToggleProps {
  mode: DesktopThemeMode;
  onModeChange: (mode: DesktopThemeMode) => void;
  className?: string;
}

export default function ThemeModeToggle({ mode, onModeChange, className }: ThemeModeToggleProps) {
  return (
    <Segmented<DesktopThemeMode>
      className={className ? `theme-toggle ${className}` : "theme-toggle"}
      value={mode}
      options={[
        { label: "浅色", value: "light" },
        { label: "深色", value: "dark" },
      ]}
      onChange={(value) => onModeChange(value)}
    />
  );
}
