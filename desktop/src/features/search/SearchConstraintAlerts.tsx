import React from "react";
import { Alert } from "antd";

import type { SearchConstraintAlertItem } from "./geyserConstraintPresentation.ts";

interface SearchConstraintAlertsProps {
  lastError: string | null;
  items: readonly SearchConstraintAlertItem[];
  onCloseLastError: () => void;
}

export default function SearchConstraintAlerts({
  lastError,
  items,
  onCloseLastError,
}: SearchConstraintAlertsProps) {
  void React;

  if (!lastError && items.length === 0) {
    return null;
  }

  return (
    <>
      {lastError ? (
        <Alert
          className="search-panel-alert"
          type="error"
          showIcon
          closable
          title={`参数提示: ${lastError}`}
          onClose={onCloseLastError}
        />
      ) : null}
      {items.map((item, index) => (
        <Alert
          key={`${item.severity}-${index}-${item.message}`}
          className="search-panel-alert"
          type={item.severity}
          showIcon
          title={item.message}
        />
      ))}
    </>
  );
}
