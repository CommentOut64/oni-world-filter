import React from "react";
import { Button, Space } from "antd";

interface SearchActionsProps {
  isSearching: boolean;
  isCancelling: boolean;
  onCancel: () => void;
  onClear: () => void;
  onCopy: () => void;
}

export default function SearchActions({
  isSearching,
  isCancelling,
  onCancel,
  onClear,
  onCopy,
}: SearchActionsProps) {
  void React;
  return (
    <section className="search-actions">
      <Space.Compact block>
        <Button htmlType="submit" type="primary" loading={isSearching} disabled={isSearching}>
          {isSearching ? "搜索进行中..." : "开始搜索"}
        </Button>
        <Button onClick={onCancel} disabled={!isSearching || isCancelling} loading={isCancelling}>
          {isCancelling ? "取消中..." : "取消搜索"}
        </Button>
        <Button onClick={onClear}>清空结果</Button>
        <Button onClick={onCopy}>复制协议 JSON</Button>
      </Space.Compact>
    </section>
  );
}
