# Geyser Constraint Mutual Exclusion Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让喷口约束编辑器在前端直接避免明显冲突与重复选择，同时保留现有表单/后端校验兜底。

**Architecture:** 先在 `searchSchema` 增加 `required/forbidden` 互斥校验，确保提交前就能发现冲突；再抽出一个纯函数模块负责计算“哪些喷口在当前行应该被禁用以及原因”，由 `required`/`forbidden`/`distance`/`count` 三类编辑器复用。UI 层采用“可见但禁用”的方式，不隐藏选项。

**Tech Stack:** React 19、react-hook-form、zod、TypeScript、node:test

---

## Chunk 1: Schema Mutual Exclusion

### Task 1: required/forbidden 冲突测试与实现

**Files:**
- Modify: `desktop/src/features/search/searchSchema.ts`
- Create: `desktop/tests/searchSchema.test.ts`

- [ ] Step 1: 写失败测试，覆盖同一喷口同时出现在 `required` 和 `forbidden`
- [ ] Step 2: 运行 `node --test --experimental-strip-types desktop/tests/searchSchema.test.ts`，确认失败原因正确
- [ ] Step 3: 在 `searchSchema.ts` 增加互斥校验，并把错误挂到冲突项对应字段
- [ ] Step 4: 再次运行同一命令，确认通过

## Chunk 2: Editor Disable Logic

### Task 2: 纯函数测试与实现

**Files:**
- Create: `desktop/src/features/search/geyserConstraintOptions.ts`
- Create: `desktop/tests/geyserConstraintOptions.test.ts`

- [ ] Step 1: 写失败测试，覆盖：
  - `required` 行应禁用 `forbidden` 中已选喷口
  - `distance/count` 行应禁用本组其他行已选喷口
  - 当前行自己的已选值不能被禁用
  - 新增按钮默认值应跳过已禁用项
- [ ] Step 2: 运行 `node --test --experimental-strip-types desktop/tests/geyserConstraintOptions.test.ts`，确认失败
- [ ] Step 3: 写最小实现让测试通过
- [ ] Step 4: 再次运行同一命令确认通过

### Task 3: 接入编辑器

**Files:**
- Modify: `desktop/src/features/search/GeyserConstraintEditor.tsx`
- Modify: `desktop/src/features/search/DistanceRuleEditor.tsx`
- Modify: `desktop/src/features/search/CountRuleEditor.tsx`

- [ ] Step 1: 在三个编辑器里用纯函数计算每一行的禁用项与提示文案
- [ ] Step 2: 让“新增”按钮在无可选喷口时禁用
- [ ] Step 3: 保持当前值可见，不因冲突而从下拉中消失

## Chunk 3: Verification

### Task 4: 验证

**Files:**
- Verify only

- [ ] Step 1: 运行 `node --test --experimental-strip-types desktop/tests/searchSchema.test.ts`
- [ ] Step 2: 运行 `node --test --experimental-strip-types desktop/tests/geyserConstraintOptions.test.ts`
- [ ] Step 3: 运行 `npm run build`（`desktop` 目录）
- [ ] Step 4: 如实记录结果与任何残余风险
