import React from "react";
import { Badge, Button, Flex, Tag, Typography } from "antd";

import { useSearchStore } from "../../state/searchStore";
import { openHostDebugWindow } from "../../lib/hostDebugWindow";

export default function ResultToolbar() {
  void React;
  const isSearching = useSearchStore((state) => state.isSearching);
  const resultsCount = useSearchStore((state) => state.results.length);
  const selectedSeed = useSearchStore((state) => state.selectedSeed);
  const lastSubmittedRequest = useSearchStore((state) => state.lastSubmittedRequest);
  const lastHostDebugMessages = useSearchStore((state) => state.lastHostDebugMessages);

  async function handleOpenHostDebugWindow(): Promise<void> {
    await openHostDebugWindow({
      request: lastSubmittedRequest,
      messages: lastHostDebugMessages,
    });
  }

  return (
    <section className="result-toolbar">
      <Flex wrap gap={12} align="center">
        <Tag variant="filled" color="blue">
          结果总数: {resultsCount.toLocaleString()}
        </Tag>
        <Badge status={isSearching ? "processing" : "default"} text={`状态: ${isSearching ? "流式接收中" : "已停止"}`} />
        <Tag variant="filled" color="cyan">
          当前选中: {selectedSeed === null ? "-" : selectedSeed}
        </Tag>
        <Button htmlType="button" onClick={() => void handleOpenHostDebugWindow()}>
          打开调试窗口
        </Button>
      </Flex>
      <Typography.Text type="secondary">
        结果列表和预览联动逻辑保持不变，当前只替换展示层。
      </Typography.Text>
    </section>
  );
}
