import React from "react";
import { Button, Select, Typography } from "antd";
import { Controller, useFieldArray, useFormContext, useWatch } from "react-hook-form";

import type { TraitMeta } from "../../lib/contracts";
import { TRAIT_DISPLAY_NAMES } from "../../lib/traitDisplayNames.ts";
import type { SearchFormValues } from "./searchSchema";
import { buildTraitOptionStates, findFirstAvailableTrait } from "./traitConstraintRules.ts";

interface TraitConstraintEditorProps {
  traits: TraitMeta[];
  disabled?: boolean;
  disabledReason?: string | null;
  availableTraitIds?: ReadonlySet<string> | null;
}

function formatTraitLabel(trait: TraitMeta): string {
  return TRAIT_DISPLAY_NAMES[trait.id] ?? trait.name ?? trait.id;
}

export default function TraitConstraintEditor({
  traits,
  disabled = false,
  disabledReason = null,
  availableTraitIds = null,
}: TraitConstraintEditorProps) {
  void React;
  const {
    control,
    formState: { errors },
  } = useFormContext<SearchFormValues>();
  const traitRules = useWatch({ control, name: "traitRules" }) ?? [];
  const { fields, append, remove } = useFieldArray({
    control,
    name: "traitRules",
  });

  const selectedTraitIdsByRow = traitRules.map((item) => item?.traitId ?? "");
  const selectedTraitIds = new Set(
    selectedTraitIdsByRow.filter((item): item is string => item.length > 0)
  );
  const firstAvailableTraitId = findFirstAvailableTrait(
    traits,
    selectedTraitIds,
    availableTraitIds
  );

  return (
    <section className="constraint-editor">
      <header className="constraint-editor-header">
        <Typography.Text strong>主星特质</Typography.Text>
        <Button
          htmlType="button"
          size="small"
          disabled={disabled || !firstAvailableTraitId}
          onClick={() =>
            append({
              traitId: firstAvailableTraitId ?? "",
              mode: "required",
            })
          }
        >
          新增
        </Button>
      </header>
      {disabledReason ? <Typography.Text className="hint">{disabledReason}</Typography.Text> : null}
      {fields.length === 0 && !disabledReason ? (
        <Typography.Text className="hint">暂无规则</Typography.Text>
      ) : null}
      {fields.map((field, index) => {
        return (
          <div className="distance-row trait-rule-row" key={field.id}>
            <Controller
              control={control}
              name={`traitRules.${index}.traitId` as const}
              render={({ field: controllerField }) => {
                const currentValue = controllerField.value ?? "";
                const optionStates = buildTraitOptionStates(
                  traits,
                  selectedTraitIdsByRow,
                  index,
                  availableTraitIds
                );
                const options = traits.map((trait, optionIndex) => ({
                  label: formatTraitLabel(trait),
                  value: trait.id,
                  disabled: optionStates[optionIndex]?.disabled ?? false,
                }));

                return (
                  <Select
                    className="constraint-select"
                    size="small"
                    placeholder="请选择特质"
                    disabled={disabled}
                    value={currentValue || undefined}
                    options={options}
                    onChange={controllerField.onChange}
                  />
                );
              }}
            />
            <Controller
              control={control}
              name={`traitRules.${index}.mode` as const}
              render={({ field: controllerField }) => (
                <Select
                  size="small"
                  disabled={disabled}
                  value={controllerField.value}
                  options={[
                    { label: "必须包含", value: "required" },
                    { label: "必须排除", value: "forbidden" },
                  ]}
                  onChange={controllerField.onChange}
                />
              )}
            />
            <Button htmlType="button" size="small" onClick={() => remove(index)}>
              删除
            </Button>
            <div className="distance-errors">
              {errors.traitRules?.[index]?.traitId ? (
                <small className="error">{errors.traitRules[index]?.traitId?.message}</small>
              ) : null}
              {errors.traitRules?.[index]?.mode ? (
                <small className="error">{errors.traitRules[index]?.mode?.message}</small>
              ) : null}
            </div>
          </div>
        );
      })}
    </section>
  );
}
