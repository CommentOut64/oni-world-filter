import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

import { formatNativeDisplayMessage, shouldIgnoreSidecarStderr } from "../src/lib/tauri.ts";

const APP_TSX = readFileSync(new URL("../src/app/App.tsx", import.meta.url), "utf8");
const CMAKE_LISTS = readFileSync(new URL("../../src/CMakeLists.txt", import.meta.url), "utf8");
const SEARCH_CONSTRAINT_ALERTS_TSX = readFileSync(
  new URL("../src/features/search/SearchConstraintAlerts.tsx", import.meta.url),
  "utf8"
);
const PREVIEW_PANE_TSX = readFileSync(
  new URL("../src/features/preview/PreviewPane.tsx", import.meta.url),
  "utf8"
);

test("shouldIgnoreSidecarStderr ignores recoverable template placement diagnostics", () => {
  assert.equal(
    shouldIgnoreSidecarStderr("error ApplyTemplateRules:430 can not place all templates"),
    true
  );
  assert.equal(
    shouldIgnoreSidecarStderr(
      "error SpawnStartingTemplate:94 start location should not overlap bounds."
    ),
    true
  );
  assert.equal(
    shouldIgnoreSidecarStderr("error ApplyTemplateRules:409 the site is already used."),
    true
  );
  assert.equal(
    shouldIgnoreSidecarStderr(
      "error ApplyTemplateRules:404 override placement is wrong, rule: start_template."
    ),
    true
  );
});

test("shouldIgnoreSidecarStderr ignores sidecar diagnostic progress lines", () => {
  assert.equal(
    shouldIgnoreSidecarStderr(
      "[sidecar-diagnostic] search worker started jobId=search-1777015977549-mdntey"
    ),
    true
  );
});

test("shouldIgnoreSidecarStderr keeps real stderr errors visible", () => {
  assert.equal(shouldIgnoreSidecarStderr("sidecar 进程异常退出(code=Some(-1073741819))"), false);
});

test("formatNativeDisplayMessage localizes common sidecar runtime failures", () => {
  assert.equal(
    formatNativeDisplayMessage("authoritative worldOffset is unavailable for current target"),
    "当前版本缺少可验证的 worldOffset 数据，已停止返回喷口参数以避免错误结果。"
  );
  assert.equal(
    formatNativeDisplayMessage("secondary preview is not available for current seed"),
    "当前种子没有可用的副星预览。"
  );
});

test("sidecar C++ target uses UTF-8 execution charset instead of GBK", () => {
  assert.match(CMAKE_LISTS, /add_compile_options\(\"\/utf-8\"\)/);
  assert.doesNotMatch(CMAKE_LISTS, /execution-charset:GBK/);
});

test("frontend Alerts use title instead of deprecated message prop", () => {
  assert.doesNotMatch(APP_TSX, /<Alert[\s\S]*\bmessage=/);
  assert.doesNotMatch(SEARCH_CONSTRAINT_ALERTS_TSX, /<Alert[\s\S]*\bmessage=/);
  assert.doesNotMatch(PREVIEW_PANE_TSX, /<Alert[\s\S]*\bmessage=/);

  assert.match(APP_TSX, /<Alert[\s\S]*\btitle=/);
  assert.match(SEARCH_CONSTRAINT_ALERTS_TSX, /<Alert[\s\S]*\btitle=/);
  assert.match(PREVIEW_PANE_TSX, /<Alert[\s\S]*\btitle=/);
});
