import { useFieldArray, useFormContext, useWatch } from "react-hook-form";

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
  const {
    control,
    register,
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
      <header>
        <h4>距离规则</h4>
        <button
          type="button"
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
        </button>
      </header>
      {fields.length === 0 ? <p className="hint">暂无规则</p> : null}
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
            <select {...register(`distance.${index}.geyser`)}>
              <option value="">请选择喷口</option>
              {geysers.map((item) => (
                <option key={item.id} value={item.key} disabled={availability[item.key] !== null}>
                  {formatGeyserNameByKey(item.key)}
                  {availability[item.key] ? ` (${availability[item.key]})` : ""}
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
        );
      })}
    </section>
  );
}
