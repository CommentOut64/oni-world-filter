import React from "react";
import { Button, InputNumber, Select, Typography } from "antd";
import { Controller, useFieldArray, useFormContext, useWatch } from "react-hook-form";

import type { GeyserOption } from "../../lib/contracts";
import { formatGeyserNameByKey } from "../../lib/displayResolvers";
import {
  buildGeyserOptionAvailability,
  collectSiblingSelectedGeysers,
  findFirstAvailableGeyser,
} from "./geyserConstraintOptions";
import type { SearchFormValues } from "./searchSchema";

interface CountRuleEditorProps {
  geysers: GeyserOption[];
  disabledGeyserKeys?: ReadonlySet<string>;
}

export default function CountRuleEditor({ geysers, disabledGeyserKeys }: CountRuleEditorProps) {
  void React;
  const {
    control,
    formState: { errors },
  } = useFormContext<SearchFormValues>();
  const countRules = useWatch({ control, name: "count" }) ?? [];
  const { fields, append, remove } = useFieldArray({
    control,
    name: "count",
  });
  const firstEnabledGeyser = findFirstAvailableGeyser({
    geyserKeys: geysers.map((item) => item.key),
    blockers: [
      {
        keys: collectSiblingSelectedGeysers(countRules),
        reason: "已存在数量规则",
      },
    ],
    worldDisabledKeys: disabledGeyserKeys,
  });

  return (
    <section className="constraint-editor">
      <header className="constraint-editor-header">
        <Typography.Text strong>数量规则</Typography.Text>
        <Button
          htmlType="button"
          size="small"
          disabled={!firstEnabledGeyser}
          onClick={() =>
            append({
              geyser: firstEnabledGeyser,
              minCount: 0,
              maxCount: 1,
            })
          }
        >
          新增
        </Button>
      </header>
      {fields.length === 0 ? <Typography.Text className="hint">暂无规则</Typography.Text> : null}
      {fields.map((field, index) => {
        const availability = buildGeyserOptionAvailability({
          geyserKeys: geysers.map((item) => item.key),
          currentValue: countRules[index]?.geyser ?? "",
          blockers: [
            {
              keys: collectSiblingSelectedGeysers(countRules, index),
              reason: "已存在数量规则",
            },
          ],
          worldDisabledKeys: disabledGeyserKeys,
        });

        return (
          <div className="distance-row" key={field.id}>
            <Controller
              control={control}
              name={`count.${index}.geyser` as const}
              render={({ field: controllerField }) => (
                <Select
                  className="constraint-select"
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
              )}
            />
            <Controller
              control={control}
              name={`count.${index}.minCount` as const}
              render={({ field: controllerField }) => (
                <InputNumber
                  className="constraint-number"
                  min={0}
                  step={1}
                  value={controllerField.value}
                  onChange={(value) => controllerField.onChange(value ?? 0)}
                />
              )}
            />
            <Controller
              control={control}
              name={`count.${index}.maxCount` as const}
              render={({ field: controllerField }) => (
                <InputNumber
                  className="constraint-number"
                  min={0}
                  step={1}
                  value={controllerField.value}
                  onChange={(value) => controllerField.onChange(value ?? 0)}
                />
              )}
            />
            <Button htmlType="button" onClick={() => remove(index)}>
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
