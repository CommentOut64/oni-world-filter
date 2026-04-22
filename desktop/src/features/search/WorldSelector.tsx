import React, { useEffect, useMemo, useState } from "react";
import { Segmented, Select, Typography } from "antd";
import { useFormContext } from "react-hook-form";

import type { WorldOption } from "../../lib/contracts";
import { WORLD_DISPLAY_NAMES } from "../../lib/displayNames";
import type { SearchFormValues } from "./searchSchema";
import {
  findCategoryForWorld,
  getCategoryForWorld,
  groupWorldsByCategory,
  isWorldTypeVisibleInCategory,
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
  void React;
  const {
    clearErrors,
    register,
    setValue,
    watch,
    formState: { errors },
  } = useFormContext<SearchFormValues>();
  const selectedWorldType = watch("worldType");
  const groupedWorlds = useMemo(() => groupWorldsByCategory(worlds), [worlds]);
  const selectedCategory = useMemo(
    () => findCategoryForWorld(worlds, selectedWorldType),
    [selectedWorldType, worlds]
  );
  const [activeCategory, setActiveCategory] = useState<WorldCategory>(
    selectedCategory ?? getCategoryForWorld(worlds, selectedWorldType)
  );

  useEffect(() => {
    if (selectedCategory) {
      setActiveCategory(selectedCategory);
    }
  }, [selectedCategory]);

  const visibleWorlds = groupedWorlds[activeCategory];
  const visibleSelectedWorldType = useMemo(
    () =>
      isWorldTypeVisibleInCategory(worlds, selectedWorldType, activeCategory)
        ? selectedWorldType
        : undefined,
    [activeCategory, selectedWorldType, worlds]
  );

  return (
    <>
      <input type="hidden" {...register("worldType", { valueAsNumber: true })} />

      <section className="world-selector-compact">
        <div className="world-selector-row">
          <div className="world-selector-field">
            <Typography.Text className="field-label">世界分类</Typography.Text>
            <Segmented
              className="world-category-tabs"
              value={activeCategory}
              options={WORLD_CATEGORY_OPTIONS.map((category) => ({
                label: category.label,
                value: category.id,
                title: category.description,
              }))}
              onChange={(value) => {
                const nextCategory = value as WorldCategory;
                setActiveCategory(nextCategory);
                if (!isWorldTypeVisibleInCategory(worlds, selectedWorldType, nextCategory)) {
                  setValue("worldType", undefined as never, {
                    shouldDirty: true,
                    shouldValidate: false,
                  });
                  clearErrors("worldType");
                }
              }}
            />
          </div>

          <div className="world-selector-field">
            <Typography.Text className="field-label">具体世界</Typography.Text>
            <Select
              className="world-select"
              placeholder="请选择具体世界"
              value={visibleSelectedWorldType}
              options={visibleWorlds.map((world) => ({
                label: getWorldDisplayLabel(world),
                value: world.id,
              }))}
              notFoundContent="当前分类下暂无可选世界"
              onChange={(value) => {
                setValue("worldType", Number(value), {
                  shouldDirty: true,
                  shouldValidate: true,
                });
              }}
            />
          </div>
        </div>
        {errors.worldType ? <small className="error">{errors.worldType.message}</small> : null}
      </section>
    </>
  );
}
