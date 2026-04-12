import { useFieldArray, useFormContext } from "react-hook-form";

import type { GeyserOption } from "../../lib/contracts";
import type { SearchFormValues } from "./searchSchema";

interface DistanceRuleEditorProps {
  geysers: GeyserOption[];
  disabledGeyserKeys?: ReadonlySet<string>;
}

export default function DistanceRuleEditor({ geysers, disabledGeyserKeys }: DistanceRuleEditorProps) {
  const {
    control,
    register,
    formState: { errors },
  } = useFormContext<SearchFormValues>();
  const { fields, append, remove } = useFieldArray({
    control,
    name: "distance",
  });
  const firstEnabledGeyser = geysers.find((item) => !disabledGeyserKeys?.has(item.key))?.key ?? "";

  return (
    <section className="constraint-editor">
      <header>
        <h4>距离规则</h4>
        <button
          type="button"
          onClick={() =>
            append({
              geyser: firstEnabledGeyser,
              minDist: 0,
              maxDist: 80,
            })
          }
        >
          新增
        </button>
      </header>
      {fields.length === 0 ? <p className="hint">暂无规则</p> : null}
      {fields.map((field, index) => (
        <div className="distance-row" key={field.id}>
          <select {...register(`distance.${index}.geyser`)}>
            <option value="">请选择喷口</option>
            {geysers.map((item) => (
              <option key={item.id} value={item.key} disabled={disabledGeyserKeys?.has(item.key)}>
                {item.key}
                {disabledGeyserKeys?.has(item.key) ? " (当前世界不可生成)" : ""}
              </option>
            ))}
          </select>
          <input type="number" step="1" {...register(`distance.${index}.minDist`, { valueAsNumber: true })} />
          <input type="number" step="1" {...register(`distance.${index}.maxDist`, { valueAsNumber: true })} />
          <button type="button" onClick={() => remove(index)}>
            删除
          </button>
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
      ))}
    </section>
  );
}
