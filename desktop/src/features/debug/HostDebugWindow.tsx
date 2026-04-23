import React, { useEffect, useState } from "react";
import { Card, Collapse, Typography } from "antd";

import {
  buildHostDebugText,
  readPersistedHostDebugSnapshot,
  subscribeHostDebugSnapshot,
  type HostDebugSnapshot,
} from "../../lib/hostDebugWindow";

export default function HostDebugWindow() {
  void React;
  const [snapshot, setSnapshot] = useState<HostDebugSnapshot>(() => readPersistedHostDebugSnapshot());

  useEffect(() => {
    setSnapshot(readPersistedHostDebugSnapshot());
    return subscribeHostDebugSnapshot((nextSnapshot) => {
      setSnapshot(nextSnapshot);
    });
  }, []);

  return (
    <main className="host-debug-window">
      <Card className="host-debug-header" variant="borderless">
        <Typography.Title level={3}>Host 调试窗口</Typography.Title>
        <Typography.Paragraph>
          这里显示 desktop 宿主实际解析到的 sidecar 路径，以及宿主真正发送给 sidecar 的 payload。
        </Typography.Paragraph>
      </Card>
      <Card className="host-debug-body" variant="borderless">
        <Collapse
          defaultActiveKey={["payload"]}
          items={[
            {
              key: "payload",
              label: "当前调试文本",
              children: <pre className="host-debug-pre">{buildHostDebugText(snapshot)}</pre>,
            },
          ]}
        />
      </Card>
    </main>
  );
}
