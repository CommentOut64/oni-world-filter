import React from "react";
import { Button, Modal, Space, Typography } from "antd";

import type { GeyserOption, SearchAnalysisPayload } from "../../lib/contracts";
import { formatGeyserNameByKey } from "../../lib/displayResolvers";
import { formatSearchWarningProbabilityCopy } from "./searchProbabilityFormat";

interface SearchWarningConfirmModalProps {
  open: boolean;
  analysis: SearchAnalysisPayload | null;
  geysers: readonly GeyserOption[];
  onContinue: () => void;
  onAbandon: () => void;
}

function formatBottlenecks(analysis: SearchAnalysisPayload, geysers: readonly GeyserOption[]): string {
  if (analysis.bottlenecks.length === 0) {
    return "暂无";
  }
  return analysis.bottlenecks
    .map((item) => {
      const matched = geysers.find((geyser) => geyser.key === item);
      return matched ? formatGeyserNameByKey(matched.key) : formatGeyserNameByKey(item);
    })
    .join("、");
}

export default function SearchWarningConfirmModal({
  open,
  analysis,
  geysers,
  onContinue,
  onAbandon,
}: SearchWarningConfirmModalProps) {
  void React;
  if (!open || !analysis) {
    return null;
  }

  return (
    <Modal
      centered
      open={open}
      getContainer={false}
      title="搜索前提醒"
      onCancel={onAbandon}
      footer={
        <Space>
          <Button onClick={onAbandon}>放弃</Button>
          <Button type="primary" onClick={onContinue}>
            继续
          </Button>
        </Space>
      }
    >
      <div className="search-warning-modal-body">
        <Typography.Paragraph>{formatSearchWarningProbabilityCopy(analysis.predictedBottleneckProbability)}</Typography.Paragraph>
        <Typography.Paragraph>
          主要瓶颈在于：{formatBottlenecks(analysis, geysers)}。
        </Typography.Paragraph>
        <Typography.Paragraph>是否继续搜索？</Typography.Paragraph>
      </div>
    </Modal>
  );
}
