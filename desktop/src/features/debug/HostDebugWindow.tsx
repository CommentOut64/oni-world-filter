import { useEffect, useState } from "react";

import {
  buildHostDebugText,
  readPersistedHostDebugSnapshot,
  subscribeHostDebugSnapshot,
  type HostDebugSnapshot,
} from "../../lib/hostDebugWindow";

export default function HostDebugWindow() {
  const [snapshot, setSnapshot] = useState<HostDebugSnapshot>(() => readPersistedHostDebugSnapshot());

  useEffect(() => {
    setSnapshot(readPersistedHostDebugSnapshot());
    return subscribeHostDebugSnapshot((nextSnapshot) => {
      setSnapshot(nextSnapshot);
    });
  }, []);

  return (
    <main className="host-debug-window">
      <header className="host-debug-header">
        <div>
          <h1>Host 调试窗口</h1>
          <p>这里显示 desktop 宿主实际解析到的 sidecar 路径，以及宿主真正发送给 sidecar 的 payload。</p>
        </div>
      </header>
      <section className="host-debug-body">
        <pre className="host-debug-pre">{buildHostDebugText(snapshot)}</pre>
      </section>
    </main>
  );
}
