import { useFieldArray, useFormContext, useWatch } from "react-hook-form";

import type { GeyserOption } from "../../lib/contracts";
import { formatGeyserNameByKey } from "../../lib/displayResolvers";
import {
  buildGeyserOptionAvailability,
  collectSiblingSelectedGeysers,
  findFirstAvailableGeyser,
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
  const {
    control,
    register,
    formState: { errors },
  } = useFormContext<SearchFormValues>();
  const required = useWatch({ control, name: "required" }) ?? [];
  const forbidden = useWatch({ control, name: "forbidden" }) ?? [];
  const { fields, append, remove } = useFieldArray({
    control,
    name: type,
  });

  const fieldErrors = errors[type];
  const currentRows = type === "required" ? required : forbidden;
  const oppositeRows = type === "required" ? forbidden : required;
  const oppositeSelected = collectSiblingSelectedGeysers(oppositeRows);
  const distance = useWatch({ control, name: "distance" }) ?? [];
  const blockers =
    type === "required"
      ? [
          {
            keys: oppositeSelected,
            reason: "已在必须排除中",
          },
        ]
      : [
          {
            keys: oppositeSelected,
            reason: "已在必须包含中",
          },
          {
            keys: collectSiblingSelectedGeysers(distance),
            reason: "已存在距离规则",
          },
        ];
  const firstEnabledGeyser = findFirstAvailableGeyser({
    geyserKeys: geysers.map((item) => item.key),
    blockers,
    worldDisabledKeys: disabledGeyserKeys,
  });

  return (
    <section className="constraint-editor">
      <header>
        <h4>{title}</h4>
        <button
          type="button"
          disabled={!firstEnabledGeyser}
          onClick={() => append({ geyser: firstEnabledGeyser })}
        >
          新增
        </button>
      </header>
      {fields.length === 0 ? <p className="hint">暂无规则</p> : null}
      {fields.map((field, index) => {
        const availability = buildGeyserOptionAvailability({
          geyserKeys: geysers.map((item) => item.key),
          currentValue: currentRows[index]?.geyser ?? "",
          blockers,
          worldDisabledKeys: disabledGeyserKeys,
        });

        return (
          <div className="constraint-row" key={field.id}>
            <select {...register(`${type}.${index}.geyser`)}>
              <option value="">请选择喷口</option>
              {geysers.map((item) => (
                <option key={item.id} value={item.key} disabled={availability[item.key] !== null}>
                  {formatGeyserNameByKey(item.key)}
                  {availability[item.key] ? ` (${availability[item.key]})` : ""}
                </option>
              ))}
            </select>
            <button type="button" onClick={() => remove(index)}>
              删除
            </button>
            {fieldErrors?.[index]?.geyser ? (
              <small className="error">{fieldErrors[index]?.geyser?.message}</small>
            ) : null}
          </div>
        );
      })}
    </section>
  );
}
