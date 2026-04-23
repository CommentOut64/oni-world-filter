import { App as AntdApp, ConfigProvider } from "antd";
import React, { useEffect, useState } from "react";
import ReactDOM from "react-dom/client";
import App from "./app/App";
import {
  createDesktopTheme,
  getPreferredDesktopThemeMode,
  syncDesktopThemeMode,
  type DesktopThemeMode,
} from "./app/antdTheme";
import "./app/app.css";

function RootApp() {
  const [themeMode, setThemeMode] = useState<DesktopThemeMode>(() =>
    getPreferredDesktopThemeMode(
      typeof window === "undefined" ? undefined : window.matchMedia.bind(window)
    )
  );

  useEffect(() => {
    syncDesktopThemeMode(themeMode, document.documentElement);
  }, [themeMode]);

  return (
    <ConfigProvider theme={createDesktopTheme(themeMode)}>
      <AntdApp>
        <App themeMode={themeMode} onThemeModeChange={setThemeMode} />
      </AntdApp>
    </ConfigProvider>
  );
}

ReactDOM.createRoot(document.getElementById("root") as HTMLElement).render(
  <React.StrictMode>
    <RootApp />
  </React.StrictMode>
);
