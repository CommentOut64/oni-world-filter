import { useFieldArray, useFormContext } from "react-hook-form";

import type { GeyserOption } from "../../lib/contracts";
import type { SearchFormValues } from "./searchSchema";

interface CountRuleEditorProps {
  geysers: GeyserOption[];
}

export default function CountRuleEditor({ geysers }: CountRuleEditorProps) {
  const {
    control,
    register,
    formState: { errors },
  } = useFormContext<SearchFormValues>();
  const { fields, append, remove } = useFieldArray({
    control,
    name: "count",
  });

  return (
    <section className="constraint-editor">
      <header>
        <h4>数量规则</h4>
        <button
          type="button"
          onClick={() =>
            append({
              geyser: geysers[0]?.key ?? "",
              minCount: 0,
              maxCount: 1,
            })
          }
        >
          新增
        </button>
      </header>
      {fields.length === 0 ? <p className="hint">暂无规则</p> : null}
      {fields.map((field, index) => (
        <div className="distance-row" key={field.id}>
          <select {...register(`count.${index}.geyser`)}>
            <option value="">请选择喷口</option>
            {geysers.map((item) => (
              <option key={item.id} value={item.key}>
                {item.key}
              </option>
            ))}
          </select>
          <input type="number" step="1" {...register(`count.${index}.minCount`, { valueAsNumber: true })} />
          <input type="number" step="1" {...register(`count.${index}.maxCount`, { valueAsNumber: true })} />
          <button type="button" onClick={() => remove(index)}>
            删除
          </button>
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
      ))}
    </section>
  );
}
