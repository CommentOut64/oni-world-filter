import { useEffect, useMemo, useState } from "react";
import { useFormContext } from "react-hook-form";

import type { WorldOption } from "../../lib/contracts";
import { WORLD_DISPLAY_NAMES } from "../../lib/displayNames";
import type { SearchFormValues } from "./searchSchema";
import {
  getCategoryForWorld,
  groupWorldsByCategory,
  WORLD_CATEGORY_OPTIONS,
  type WorldCategory,
} from "./worldParameterUi";

interface WorldSelectorProps {
  worlds: WorldOption[];
}

function getWorldDisplayLabel(world: WorldOption): string {
  const displayName = WORLD_DISPLAY_NAMES[world.code];
  if (!displayName) {
    return world.code;
  }
  return `${displayName.zh} (${displayName.en})`;
}

export default function WorldSelector({ worlds }: WorldSelectorProps) {
  const {
    register,
    setValue,
    watch,
    formState: { errors },
  } = useFormContext<SearchFormValues>();
  const selectedWorldType = watch("worldType");
  const groupedWorlds = useMemo(() => groupWorldsByCategory(worlds), [worlds]);
  const derivedCategory = useMemo(
    () => getCategoryForWorld(worlds, selectedWorldType),
    [selectedWorldType, worlds]
  );
  const [activeCategory, setActiveCategory] = useState<WorldCategory>(derivedCategory);

  useEffect(() => {
    setActiveCategory(derivedCategory);
  }, [derivedCategory]);

  const visibleWorlds = groupedWorlds[activeCategory];

  return (
    <>
      <input type="hidden" {...register("worldType", { valueAsNumber: true })} />

      <section className="world-selector-compact">
        <div className="world-category-tabs">
          {WORLD_CATEGORY_OPTIONS.map((category) => {
            const isActive = activeCategory === category.id;
            return (
              <button
                key={category.id}
                type="button"
                className={`world-category-tab${isActive ? " active" : ""}`}
                title={category.description}
                onClick={() => {
                  setActiveCategory(category.id);
                }}
              >
                {category.label}
              </button>
            );
          })}
        </div>

        <select
          className="world-select"
          value={selectedWorldType}
          onChange={(event) => {
            setValue("worldType", Number(event.target.value), {
              shouldDirty: true,
              shouldValidate: true,
            });
          }}
        >
          {visibleWorlds.length === 0 ? (
            <option disabled>当前分类下暂无可选世界</option>
          ) : (
            visibleWorlds.map((world) => (
              <option key={world.id} value={world.id}>
                {getWorldDisplayLabel(world)}
              </option>
            ))
          )}
        </select>
        {errors.worldType ? <small className="error">{errors.worldType.message}</small> : null}
      </section>
    </>
  );
}
