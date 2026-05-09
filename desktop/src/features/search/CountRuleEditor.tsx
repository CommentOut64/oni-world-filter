import React from "react";
import { Button, InputNumber, Select, Typography } from "antd";
import { Controller, useFieldArray, useFormContext, useWatch } from "react-hook-form";

import type { GeyserOption } from "../../lib/contracts";
import { formatGeyserNameByKey } from "../../lib/displayResolvers";
import {
  buildSectionGeyserOptionAvailability,
  findFirstAvailableGeyserForSection,
} from "./geyserConstraintOptions";
import {
  COUNT_MAX_SENTINEL,
  type SearchFormValues,
} from "./searchSchema";

interface CountRuleEditorProps {
  geysers: GeyserOption[];
  disabledGeyserKeys?: ReadonlySet<string>;
}

function formatCountUpperBound(value: string | number | undefined): string {
  if (Number(value) === COUNT_MAX_SENTINEL) {
    return "Max";
  }
  return value === undefined ? "" : String(value);
}

function parseCountUpperBound(value: string | undefined): string {
  const normalized = (value ?? "").trim();
  if (/^max$/i.test(normalized)) {
    return String(COUNT_MAX_SENTINEL);
  }
  return normalized;
}

export default function CountRuleEditor({ geysers, disabledGeyserKeys }: CountRuleEditorProps) {
  void React;
  const {
    control,
    formState: { errors },
  } = useFormContext<SearchFormValues>();
  const required = useWatch({ control, name: "required" }) ?? [];
  const forbidden = useWatch({ control, name: "forbidden" }) ?? [];
  const distance = useWatch({ control, name: "distance" }) ?? [];
  const countRules = useWatch({ control, name: "count" }) ?? [];
  const { fields, append, remove } = useFieldArray({
    control,
    name: "count",
  });
  const firstEnabledGeyser = findFirstAvailableGeyserForSection({
    section: "count",
    geyserKeys: geysers.map((item) => item.key),
    constraints: {
      required,
      forbidden,
      distance,
      count: countRules,
    },
    worldDisabledKeys: disabledGeyserKeys,
  });

  return (
    <section className="constraint-editor">
      <header className="constraint-editor-header count-rule-header">
        <Typography.Text strong>必须包含</Typography.Text>
        <Typography.Text className="count-rule-column-label">最小</Typography.Text>
        <Typography.Text className="count-rule-column-label">最大</Typography.Text>
        <span />
        <Button
          htmlType="button"
          size="small"
          disabled={!firstEnabledGeyser}
          onClick={() =>
            append({
              geyser: firstEnabledGeyser,
              minCount: 1,
              maxCount: COUNT_MAX_SENTINEL,
            })
          }
        >
          新增
        </Button>
      </header>
      {fields.length === 0 ? <Typography.Text className="hint">暂无规则</Typography.Text> : null}
      {fields.map((field, index) => {
        return (
          <div className="distance-row count-rule-row" key={field.id}>
            <Controller
              control={control}
              name={`count.${index}.geyser` as const}
              render={({ field: controllerField }) => {
                const availability = buildSectionGeyserOptionAvailability({
                  section: "count",
                  geyserKeys: geysers.map((item) => item.key),
                  currentValue: controllerField.value ?? "",
                  constraints: {
                    required,
                    forbidden,
                    distance,
                    count: countRules,
                  },
                  worldDisabledKeys: disabledGeyserKeys,
                });

                return (
                  <Select
                    className="constraint-select"
                    size="small"
                    placeholder="请选择喷口"
                    value={controllerField.value || undefined}
                    options={geysers.map((item) => ({
                      label: `${formatGeyserNameByKey(item.key)}${
                        availability[item.key] ? ` (${availability[item.key]})` : ""
                      }`,
                      value: item.key,
                      disabled: availability[item.key] !== null,
                    }))}
                    onChange={controllerField.onChange}
                  />
                );
              }}
            />
            <Controller
              control={control}
              name={`count.${index}.minCount` as const}
              render={({ field: controllerField }) => (
                <InputNumber
                  className="constraint-number"
                  size="small"
                  min={1}
                  step={1}
                  value={controllerField.value}
                  onChange={(value) => controllerField.onChange(value ?? 1)}
                />
              )}
            />
            <Controller
              control={control}
              name={`count.${index}.maxCount` as const}
              render={({ field: controllerField }) => (
                <InputNumber
                  className="constraint-number"
                  size="small"
                  controls={false}
                  step={1}
                  value={controllerField.value}
                  formatter={formatCountUpperBound}
                  parser={parseCountUpperBound}
                  onChange={(value) => {
                    if (value === null) {
                      controllerField.onChange(COUNT_MAX_SENTINEL);
                      return;
                    }
                    const nextValue = Number(value);
                    if (!Number.isFinite(nextValue)) {
                      controllerField.onChange(COUNT_MAX_SENTINEL);
                      return;
                    }
                    if (nextValue === COUNT_MAX_SENTINEL) {
                      controllerField.onChange(COUNT_MAX_SENTINEL);
                      return;
                    }
                    controllerField.onChange(nextValue < 1 ? 1 : nextValue);
                  }}
                />
              )}
            />
            <Controller
              control={control}
              name={`count.${index}.maxCount` as const}
              render={({ field: controllerField }) => (
                <Button
                  htmlType="button"
                  className="max-toggle-button"
                  size="small"
                  type={controllerField.value === COUNT_MAX_SENTINEL ? "primary" : "default"}
                  onClick={() => controllerField.onChange(COUNT_MAX_SENTINEL)}
                >
                  Max
                </Button>
              )}
            />
            <Button htmlType="button" size="small" onClick={() => remove(index)}>
              删除
            </Button>
            <div className="distance-errors">
              {errors.count?.[index]?.geyser ? (
                <small className="error">{errors.count[index]?.geyser?.message}</small>
              ) : null}
              {errors.count?.[index]?.minCount ? (
                <small className="error">{errors.count[index]?.minCount?.message}</small>
              ) : null}
              {errors.count?.[index]?.maxCount ? (
                <small className="error">{errors.count[index]?.maxCount?.message}</small>
              ) : null}
            </div>
          </div>
        );
      })}
    </section>
  );
}
