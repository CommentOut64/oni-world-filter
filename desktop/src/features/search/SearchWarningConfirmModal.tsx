import React from "react";
import { Button, Modal, Space, Typography } from "antd";

import type { GeyserOption, SearchAnalysisPayload } from "../../lib/contracts";
import { formatGeyserNameByKey } from "../../lib/displayResolvers";
import { formatSearchWarningProbabilityCopy } from "./searchProbabilityFormat";

interface SearchWarningConfirmModalProps {
  open: boolean;
  analysis: SearchAnalysisPayload | null;
  geysers: readonly GeyserOption[];
  title?: string;
  continueText?: string;
  abandonText?: string;
  children?: React.ReactNode;
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
  title = "搜索前提醒",
  continueText = "继续",
  abandonText = "放弃",
  children,
  onContinue,
  onAbandon,
}: SearchWarningConfirmModalProps) {
  void React;
  if (!open || (!analysis && !children)) {
    return null;
  }
  const resolvedAnalysis = analysis;

  return (
    <Modal
      centered
      open={open}
      getContainer={false}
      title={title}
      onCancel={onAbandon}
      footer={
        <Space>
          <Button onClick={onAbandon}>{abandonText}</Button>
          <Button type="primary" onClick={onContinue}>{continueText}</Button>
        </Space>
      }
    >
      <div className="search-warning-modal-body">
        {children ? (
          children
        ) : resolvedAnalysis ? (
          <>
            <Typography.Paragraph>{formatSearchWarningProbabilityCopy(resolvedAnalysis.predictedBottleneckProbability)}</Typography.Paragraph>
            <Typography.Paragraph>
              主要瓶颈在于：{formatBottlenecks(resolvedAnalysis, geysers)}。
            </Typography.Paragraph>
            <Typography.Paragraph>是否继续搜索？</Typography.Paragraph>
          </>
        ) : null
        }
      </div>
    </Modal>
  );
}
