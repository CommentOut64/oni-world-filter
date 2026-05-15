import test from "node:test";
import assert from "node:assert/strict";
import { createElement } from "react";
import { renderToStaticMarkup } from "react-dom/server";

import SearchConstraintAlerts from "../src/features/search/SearchConstraintAlerts.tsx";
import { buildWorldConstraintAlertItems } from "../src/features/search/geyserConstraintPresentation.ts";

test("buildWorldConstraintAlertItems returns blocking error for unsupported required, count and distance geysers", () => {
  const alerts = buildWorldConstraintAlertItems({
    constraints: {
      required: [{ geyser: "steam" }],
      forbidden: [],
      distance: [{ geyser: "hot_water", minDist: 0, maxDist: 80 }],
      count: [{ geyser: "salt_water", minCount: 1, maxCount: 2 }],
      requiredTraits: [],
      forbiddenTraits: [],
    },
    disabledGeyserKeys: new Set(["steam", "hot_water", "salt_water"]),
  });

  assert.deepEqual(alerts, [
    {
      severity: "error",
      message: "当前世界不会生成这些喷口：低温蒸汽喷孔、清水泉、盐水泉。请修改相关条件后再开始搜索。",
    },
  ]);
});

test("buildWorldConstraintAlertItems returns warning for redundant forbidden geysers", () => {
  const alerts = buildWorldConstraintAlertItems({
    constraints: {
      required: [],
      forbidden: [{ geyser: "steam" }],
      distance: [],
      count: [],
      requiredTraits: [],
      forbiddenTraits: [],
    },
    disabledGeyserKeys: new Set(["steam"]),
  });

  assert.deepEqual(alerts, [
    {
      severity: "warning",
      message: "当前世界已经天然排除了这些喷口：低温蒸汽喷孔。相关“必须排除”条件可以考虑删除。",
    },
  ]);
});

test("SearchConstraintAlerts renders antd Alert blocks for last error and world warnings", () => {
  const markup = renderToStaticMarkup(
    createElement(SearchConstraintAlerts, {
      lastError: "世界参数无效",
      items: [
        {
          severity: "warning",
          message: "当前世界已经天然排除了这些喷口：低温蒸汽喷孔。相关“必须排除”条件可以考虑删除。",
        },
      ],
      onCloseLastError: () => undefined,
    })
  );

  assert.match(markup, /ant-alert/);
  assert.match(markup, /参数提示: 世界参数无效/);
  assert.match(markup, /当前世界已经天然排除了这些喷口：低温蒸汽喷孔/);
});
