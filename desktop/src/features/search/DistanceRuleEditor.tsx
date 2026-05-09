import React from "react";
import { Button, InputNumber, Select, Typography } from "antd";
import { Controller, useFieldArray, useFormContext, useWatch } from "react-hook-form";

import type { GeyserOption } from "../../lib/contracts";
import { formatGeyserNameByKey } from "../../lib/displayResolvers";
import { sortGeyserOptionsByAvailability } from "../../lib/geyserOrdering.ts";
import {
  buildSectionGeyserOptionAvailability,
  findFirstAvailableGeyserForSection,
} from "./geyserConstraintOptions";
import type { SearchFormValues } from "./searchSchema";

interface DistanceRuleEditorProps {
  geysers: GeyserOption[];
  disabledGeyserKeys?: ReadonlySet<string>;
}

export default function DistanceRuleEditor({ geysers, disabledGeyserKeys }: DistanceRuleEditorProps) {
  void React;
  const {
    control,
    formState: { errors },
  } = useFormContext<SearchFormValues>();
  const distanceRules = useWatch({ control, name: "distance" }) ?? [];
  const required = useWatch({ control, name: "required" }) ?? [];
  const forbidden = useWatch({ control, name: "forbidden" }) ?? [];
  const count = useWatch({ control, name: "count" }) ?? [];
  const { fields, append, remove } = useFieldArray({
    control,
    name: "distance",
  });
  const baseAvailability = buildSectionGeyserOptionAvailability({
    section: "distance",
    geyserKeys: geysers.map((item) => item.key),
    constraints: {
      required,
      forbidden,
      distance: distanceRules,
      count,
    },
    worldDisabledKeys: disabledGeyserKeys,
  });
  const orderedBaseGeysers = sortGeyserOptionsByAvailability(geysers, baseAvailability);
  const firstEnabledGeyser = findFirstAvailableGeyserForSection({
    section: "distance",
    geyserKeys: orderedBaseGeysers.map((item) => item.key),
    constraints: {
      required,
      forbidden,
      distance: distanceRules,
      count,
    },
    worldDisabledKeys: disabledGeyserKeys,
  });

  return (
    <section className="constraint-editor">
      <header className="constraint-editor-header distance-rule-header">
        <Typography.Text strong>
          距离规则
          <Typography.Text type="secondary" style={{ fontSize: "0.85em", fontWeight: "normal" }}>
            （单位：格）
          </Typography.Text>
        </Typography.Text>
        <Typography.Text className="distance-rule-column-label">最小</Typography.Text>
        <Typography.Text className="distance-rule-column-label">最大</Typography.Text>
        <Button
          htmlType="button"
          size="small"
          disabled={!firstEnabledGeyser}
          onClick={() =>
            append({
              geyser: firstEnabledGeyser,
              minDist: 0,
              maxDist: 80,
            })
          }
        >
          新增
        </Button>
      </header>
      {fields.length === 0 ? <Typography.Text className="hint">暂无规则</Typography.Text> : null}
      {fields.map((field, index) => {
        return (
          <div className="distance-row distance-rule-row" key={field.id}>
            <Controller
              control={control}
              name={`distance.${index}.geyser` as const}
              render={({ field: controllerField }) => {
                const availability = buildSectionGeyserOptionAvailability({
                  section: "distance",
                  geyserKeys: geysers.map((item) => item.key),
                  currentValue: controllerField.value ?? "",
                  constraints: {
                    required,
                    forbidden,
                    distance: distanceRules,
                    count,
                  },
                  worldDisabledKeys: disabledGeyserKeys,
                });
                const orderedOptions = sortGeyserOptionsByAvailability(geysers, availability);

                return (
                  <Select
                    className="constraint-select"
                    size="small"
                    placeholder="请选择喷口"
                    value={controllerField.value || undefined}
                    options={orderedOptions.map((item) => ({
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
              name={`distance.${index}.minDist` as const}
              render={({ field: controllerField }) => (
                <InputNumber
                  className="constraint-number"
                  size="small"
                  min={0}
                  step={1}
                  value={controllerField.value}
                  onChange={(value) => controllerField.onChange(value ?? 0)}
                />
              )}
            />
            <Controller
              control={control}
              name={`distance.${index}.maxDist` as const}
              render={({ field: controllerField }) => (
                <InputNumber
                  className="constraint-number"
                  size="small"
                  min={0}
                  step={1}
                  value={controllerField.value}
                  onChange={(value) => controllerField.onChange(value ?? 0)}
                />
              )}
            />
            <Button htmlType="button" size="small" onClick={() => remove(index)}>
              删除
            </Button>
            <div className="distance-errors">
              {errors.distance?.[index]?.geyser ? (
                <small className="error">{errors.distance[index]?.geyser?.message}</small>
              ) : null}
              {errors.distance?.[index]?.minDist ? (
                <small className="error">{errors.distance[index]?.minDist?.message}</small>
              ) : null}
              {errors.distance?.[index]?.maxDist ? (
                <small className="error">{errors.distance[index]?.maxDist?.message}</small>
              ) : null}
            </div>
          </div>
        );
      })}
    </section>
  );
}
