import type { ReactNode } from "react";

interface DesktopShellProps {
  children: ReactNode;
  className?: string;
}

export default function DesktopShell({ children, className }: DesktopShellProps) {
  return (
    <main className={className ? `desktop-shell ${className}` : "desktop-shell"}>{children}</main>
  );
}
