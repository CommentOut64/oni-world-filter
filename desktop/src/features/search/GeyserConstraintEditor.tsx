import { useFieldArray, useFormContext } from "react-hook-form";

import type { GeyserOption } from "../../lib/contracts";
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
  const { fields, append, remove } = useFieldArray({
    control,
    name: type,
  });

  const fieldErrors = errors[type];
  const firstEnabledGeyser = geysers.find((item) => !disabledGeyserKeys?.has(item.key))?.key ?? "";

  return (
    <section className="constraint-editor">
      <header>
        <h4>{title}</h4>
        <button type="button" onClick={() => append({ geyser: firstEnabledGeyser })}>
          新增
        </button>
      </header>
      {fields.length === 0 ? <p className="hint">暂无规则</p> : null}
      {fields.map((field, index) => (
        <div className="constraint-row" key={field.id}>
          <select {...register(`${type}.${index}.geyser`)}>
            <option value="">请选择喷口</option>
            {geysers.map((item) => (
              <option key={item.id} value={item.key} disabled={disabledGeyserKeys?.has(item.key)}>
                {item.key}
                {disabledGeyserKeys?.has(item.key) ? " (当前世界不可生成)" : ""}
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
