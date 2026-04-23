import React from "react";
import { Button, Input } from "antd";

interface CoordQuickSearchProps {
  value: string;
  loading: boolean;
  disabled: boolean;
  onChange: (value: string) => void;
  onSubmit: () => void;
}

export default function CoordQuickSearch({
  value,
  loading,
  disabled,
  onChange,
  onSubmit,
}: CoordQuickSearchProps) {
  void React;

  return (
    <section className="coord-quick-search">
      <Input
        className="coord-quick-search-input"
        value={value}
        disabled={disabled}
        placeholder="标准坐标码"
        onChange={(event) => onChange(event.target.value)}
        onPressEnter={() => {
          if (!disabled) {
            onSubmit();
          }
        }}
      />
      <Button
        htmlType="button"
        type="primary"
        className="coord-quick-search-button"
        loading={loading}
        disabled={disabled}
        onClick={onSubmit}
      >
        搜索
      </Button>
    </section>
  );
}
