import { useFieldArray, useFormContext } from "react-hook-form";

import type { GeyserOption } from "../../lib/contracts";
import type { SearchFormValues } from "./searchSchema";

interface GeyserConstraintEditorProps {
  title: string;
  type: "required" | "forbidden";
  geysers: GeyserOption[];
}

export default function GeyserConstraintEditor({
  title,
  type,
  geysers,
}: GeyserConstraintEditorProps) {
  const {
    control,
    register,
    formState: { errors },
  } = useFormContext<SearchFormValues>();
  const { fields, append, remove } = useFieldArray({
    control,
    name: type,
  });

  const fieldErrors = errors[type];

  return (
    <section className="constraint-editor">
      <header>
        <h4>{title}</h4>
        <button type="button" onClick={() => append({ geyser: geysers[0]?.key ?? "" })}>
          新增
        </button>
      </header>
      {fields.length === 0 ? <p className="hint">暂无规则</p> : null}
      {fields.map((field, index) => (
        <div className="constraint-row" key={field.id}>
          <select {...register(`${type}.${index}.geyser`)}>
            <option value="">请选择喷口</option>
            {geysers.map((item) => (
              <option key={item.id} value={item.key}>
                {item.key}
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
      ))}
    </section>
  );
}
