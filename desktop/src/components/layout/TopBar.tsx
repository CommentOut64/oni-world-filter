interface TopBarProps {
  title: string;
  subtitle: string;
}

export default function TopBar({ title, subtitle }: TopBarProps) {
  return (
    <header className="top-bar">
      <div>
        <h1>{title}</h1>
        <p>{subtitle}</p>
      </div>
      <div className="top-bar-tag">Tauri Host + C++ Sidecar</div>
    </header>
  );
}
