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
  const forbidden = useWatch({ control, name: "forbidden" }) ?? [];
  const { fields, append, remove } = useFieldArray({
    control,
    name: "distance",
  });
  const forbiddenSelected = collectSiblingSelectedGeysers(forbidden);
  const firstEnabledGeyser = findFirstAvailableGeyser({
    geyserKeys: geysers.map((item) => item.key),
    blockers: [
      {
        keys: collectSiblingSelectedGeysers(distanceRules),
        reason: "已存在距离规则",
      },
      {
        keys: forbiddenSelected,
        reason: "已在必须排除中",
      },
    ],
    worldDisabledKeys: disabledGeyserKeys,
  });

  return (
    <section className="constraint-editor">
      <header className="constraint-editor-header">
        <Typography.Text strong>距离规则</Typography.Text>
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
        const availability = buildGeyserOptionAvailability({
          geyserKeys: geysers.map((item) => item.key),
          currentValue: distanceRules[index]?.geyser ?? "",
          blockers: [
            {
              keys: collectSiblingSelectedGeysers(distanceRules, index),
              reason: "已存在距离规则",
            },
            {
              keys: forbiddenSelected,
              reason: "已在必须排除中",
            },
          ],
          worldDisabledKeys: disabledGeyserKeys,
        });

        return (
          <div className="distance-row" key={field.id}>
            <Controller
              control={control}
              name={`distance.${index}.geyser` as const}
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
              name={`distance.${index}.minDist` as const}
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
              name={`distance.${index}.maxDist` as const}
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
