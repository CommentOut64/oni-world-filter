import React from "react";
import { Button, Select, Typography } from "antd";
import { Controller, useFieldArray, useFormContext, useWatch } from "react-hook-form";

import type { GeyserOption } from "../../lib/contracts";
import { formatGeyserNameByKey } from "../../lib/displayResolvers";
import {
  buildSectionGeyserOptionAvailability,
  findFirstAvailableGeyserForSection,
} from "./geyserConstraintOptions";
import type { SearchFormValues } from "./searchSchema";

interface GeyserConstraintEditorProps {
  title: string;
  type: "required" | "forbidden";
  geysers: GeyserOption[];
  disabledGeyserKeys?: ReadonlySet<string>;
}

export default function GeyserConstraintEditor({
  title,
  type,
  geysers,
  disabledGeyserKeys,
}: GeyserConstraintEditorProps) {
  void React;
  const {
    control,
    formState: { errors },
  } = useFormContext<SearchFormValues>();
  const required = useWatch({ control, name: "required" }) ?? [];
  const forbidden = useWatch({ control, name: "forbidden" }) ?? [];
  const distance = useWatch({ control, name: "distance" }) ?? [];
  const count = useWatch({ control, name: "count" }) ?? [];
  const { fields, append, remove } = useFieldArray({
    control,
    name: type,
  });

  const fieldErrors = errors[type];
  const firstEnabledGeyser = findFirstAvailableGeyserForSection({
    section: type,
    geyserKeys: geysers.map((item) => item.key),
    constraints: {
      required,
      forbidden,
      distance,
      count,
    },
    worldDisabledKeys: disabledGeyserKeys,
  });

  return (
    <section className="constraint-editor">
      <header className="constraint-editor-header">
        <Typography.Text strong>{title}</Typography.Text>
        <Button
          htmlType="button"
          size="small"
          disabled={!firstEnabledGeyser}
          onClick={() => append({ geyser: firstEnabledGeyser })}
        >
          新增
        </Button>
      </header>
      {fields.length === 0 ? <Typography.Text className="hint">暂无规则</Typography.Text> : null}
      {fields.map((field, index) => {
        return (
          <div className="constraint-row" key={field.id}>
            <Controller
              control={control}
              name={`${type}.${index}.geyser` as const}
              render={({ field: controllerField }) => {
                const availability = buildSectionGeyserOptionAvailability({
                  section: type,
                  geyserKeys: geysers.map((item) => item.key),
                  currentValue: controllerField.value ?? "",
                  constraints: {
                    required,
                    forbidden,
                    distance,
                    count,
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
            <Button htmlType="button" size="small" onClick={() => remove(index)}>
              删除
            </Button>
            {fieldErrors?.[index]?.geyser ? (
              <small className="error">{fieldErrors[index]?.geyser?.message}</small>
            ) : null}
          </div>
        );
      })}
    </section>
  );
}
