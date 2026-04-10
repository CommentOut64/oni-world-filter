import type { ReactNode } from "react";

import StatusBar from "./StatusBar";
import TopBar from "./TopBar";

interface DesktopShellProps {
  left: ReactNode;
  center: ReactNode;
  right: ReactNode;
}

export default function DesktopShell({ left, center, right }: DesktopShellProps) {
  return (
    <main className="desktop-shell">
      <TopBar title="ONI World Desktop" subtitle="本地批量筛种与按需预览工作台" />
      <section className="desktop-grid">
        <aside className="panel panel-left">{left}</aside>
        <section className="panel panel-center">{center}</section>
        <aside className="panel panel-right">{right}</aside>
      </section>
      <StatusBar />
    </main>
  );
}
