import React, { useMemo } from "react";
import { Card, Checkbox, Collapse, Select, Tag, Typography } from "antd";
import { useFormContext } from "react-hook-form";

import type { MixingSlotMeta } from "../../lib/contracts";
import { formatMixingSlotName } from "../../lib/displayResolvers";
import { FALLBACK_MIXING_SLOTS } from "../../lib/searchCatalog";
import {
  applyChildMode,
  applyPackageMode,
  getPackageMode,
  groupMixingSlots,
  isSlotEnabled,
  levelToUiMode,
  type MixingPackageGroup,
  type MixingUiMode,
} from "./worldParameterUi";
import {
  decodeMixingToLevels,
  encodeMixingFromLevels,
  MIXING_SLOT_COUNT,
  type SearchFormValues,
} from "./searchSchema";

interface MixingSelectorProps {
  mixingSlots: MixingSlotMeta[];
  disabledMixingSlots?: ReadonlySet<number>;
}

interface DisplaySlot extends MixingSlotMeta {
  displayName: string;
}

function formatMixingSlotDescription(slot: MixingSlotMeta): string | null {
  const description = slot.description.trim();
  if (!description) {
    return null;
  }
  if (description === slot.path || description === slot.name) {
    return null;
  }
  if (/^DLC\d+_ID$/i.test(description)) {
    return null;
  }
  if (description.startsWith("STRINGS.")) {
    return null;
  }
  return description;
}

function buildDisplaySlots(mixingSlots: MixingSlotMeta[]): DisplaySlot[] {
  const sourceSlots = mixingSlots.length > 0 ? mixingSlots : FALLBACK_MIXING_SLOTS;
  return [...sourceSlots]
    .sort((lhs, rhs) => lhs.slot - rhs.slot)
    .map((item) => ({
      ...item,
      displayName: formatMixingSlotName(item),
    }));
}

function getUngroupedSlots(
  groups: readonly MixingPackageGroup[],
  displaySlots: readonly DisplaySlot[]
): DisplaySlot[] {
  const groupedSlots = new Set<number>();
  for (const group of groups) {
    groupedSlots.add(group.packageSlot.slot);
    for (const child of group.children) {
      groupedSlots.add(child.slot);
    }
  }
  return displaySlots.filter((slot) => !groupedSlots.has(slot.slot));
}

function renderModeLabel(mode: MixingUiMode): string {
  switch (mode) {
    case "guaranteed":
      return "确保";
    case "normal":
      return "可能";
    default:
      return "禁用";
  }
}

function ModeSelect({
  value,
  onChange,
  disabled,
}: {
  value: MixingUiMode;
  onChange: (mode: MixingUiMode) => void;
  disabled?: boolean;
}) {
  void React;
  return (
    <Select
      size="small"
      value={value}
      options={[
        { label: "可能", value: "normal" },
        { label: "确保", value: "guaranteed" },
      ]}
      onChange={(nextValue) => {
        onChange(nextValue as MixingUiMode);
      }}
      onClick={(event) => event.stopPropagation()}
      disabled={disabled}
    />
  );
}

export default function MixingSelector({ mixingSlots, disabledMixingSlots }: MixingSelectorProps) {
  void React;
  const {
    watch,
    register,
    setValue,
    formState: { errors },
  } = useFormContext<SearchFormValues>();
  const mixingValue = watch("mixing");
  const displaySlots = useMemo(() => buildDisplaySlots(mixingSlots), [mixingSlots]);
  const groups = useMemo(() => groupMixingSlots(displaySlots), [displaySlots]);
  const ungroupedSlots = useMemo(() => getUngroupedSlots(groups, displaySlots), [displaySlots, groups]);
  const slotCount = Math.max(displaySlots.length, MIXING_SLOT_COUNT);
  const normalizedMixing = Number.isFinite(mixingValue) ? Math.max(0, Math.trunc(mixingValue)) : 0;
  const levels = decodeMixingToLevels(normalizedMixing, slotCount);

  const commitLevels = (nextLevels: readonly number[]) => {
    const nextMixing = encodeMixingFromLevels(nextLevels);
    setValue("mixing", nextMixing, { shouldValidate: true, shouldDirty: true });
  };

  return (
    <>
      <input type="hidden" {...register("mixing", { valueAsNumber: true })} />

      <section className="mixing-package-section">
        <header className="mixing-package-header">
          <Typography.Title level={5}>世界混搭</Typography.Title>
          <Typography.Paragraph>
            先勾选 DLC 包启用混搭，再按生态选择可能或确保；未勾选即为禁用。
          </Typography.Paragraph>
        </header>

        <div className="mixing-package-list mixing-package-stack">
          {groups.map((group) => {
            const packageLevel = levels[group.packageSlot.slot] ?? 0;
            const packageMode = getPackageMode(levels, group);
            const packageEnabled = isSlotEnabled(packageLevel);
            const packageDisabled = disabledMixingSlots?.has(group.packageSlot.slot) ?? false;
            const packageCheckboxDisabled = packageDisabled && !packageEnabled;
            const packageDescription = formatMixingSlotDescription(group.packageSlot);

            return (
              <Card
                key={`${group.packageSlot.slot}-${group.packageSlot.path}`}
                className={`mixing-package-card${packageEnabled ? " active" : ""}${
                  packageDisabled ? " disabled" : ""
                }`}
                size="small"
              >
                <div className="mixing-package-card-body">
                  <div className="mixing-package-title-row">
                    <div className="mixing-package-card-header">
                      <Checkbox
                        className="mixing-package-toggle"
                        checked={packageEnabled}
                        disabled={packageCheckboxDisabled}
                        onChange={(event) => {
                          const nextLevels = applyPackageMode(
                            levels,
                            group,
                            event.target.checked ? "normal" : "off"
                          );
                          commitLevels(nextLevels);
                        }}
                      >
                        {group.packageSlot.displayName}
                      </Checkbox>
                      {!packageEnabled ? (
                        <Tag className="mixing-mode-badge">{renderModeLabel(packageMode)}</Tag>
                      ) : null}
                    </div>

                    {packageDescription || packageDisabled ? (
                      <Typography.Paragraph className="mixing-package-description">
                        {packageDescription ?? ""}
                        {packageDisabled ? `${packageDescription ? " " : ""}当前世界不可用。` : ""}
                      </Typography.Paragraph>
                    ) : null}
                  </div>

                  {group.children.length > 0 ? (
                    <div className="mixing-child-list">
                      {group.children.map((child) => {
                        const childLevel = levels[child.slot] ?? 0;
                        const childMode = levelToUiMode(childLevel);
                        const childEnabled = isSlotEnabled(childLevel);
                        const slotDisabled = disabledMixingSlots?.has(child.slot) ?? false;
                        const childDisabled = slotDisabled || !packageEnabled;
                        const childCheckboxDisabled = slotDisabled ? !childEnabled : !packageEnabled;
                        const childDescription = formatMixingSlotDescription(child);
                        const disabledReason =
                          slotDisabled
                            ? "当前世界不可用"
                            : !packageEnabled
                              ? "请先启用对应 DLC 包"
                              : "";

                        return (
                          <div key={`${child.slot}-${child.path}`} className="mixing-child-row">
                            <Checkbox
                              className="mixing-child-toggle"
                              checked={childEnabled}
                              disabled={childCheckboxDisabled}
                              onChange={(event) => {
                                commitLevels(
                                  applyChildMode(
                                    levels,
                                    child.slot,
                                    event.target.checked ? "normal" : "off"
                                  )
                                );
                              }}
                            >
                              <span className="mixing-child-name">
                                {child.displayName}
                              </span>
                            </Checkbox>

                            {childEnabled ? (
                              <ModeSelect
                                value={childMode === "off" ? "normal" : childMode}
                                disabled={childDisabled}
                                onChange={(mode) => {
                                  commitLevels(applyChildMode(levels, child.slot, mode));
                                }}
                              />
                            ) : (
                              <Tag className="mixing-mode-badge">{renderModeLabel(childMode)}</Tag>
                            )}

                            {childDescription ? (
                              <Typography.Paragraph className="mixing-child-description">
                                {childDescription}
                              </Typography.Paragraph>
                            ) : null}
                            {disabledReason ? (
                              <Typography.Text className="mixing-disabled-note">
                                {disabledReason}
                              </Typography.Text>
                            ) : null}
                          </div>
                        );
                      })}
                    </div>
                  ) : (
                    <Typography.Text className="hint mixing-package-empty">
                      该 DLC 包没有额外可选槽位，只有包本身开关。
                    </Typography.Text>
                  )}
                </div>
              </Card>
            );
          })}
        </div>

        {ungroupedSlots.length > 0 ? (
          <Card className="advanced-debug-panel" size="small">
            <header>
              <Typography.Title level={5}>未分类槽位</Typography.Title>
              <Typography.Paragraph>
                这些槽位暂未归入 DLC 包分组，仍保留只读展示以避免信息丢失。
              </Typography.Paragraph>
            </header>
            <ul className="mixing-ungrouped-list">
              {ungroupedSlots.map((slot) => (
                <li key={`${slot.slot}-${slot.path}`}>
                  {slot.displayName}
                </li>
              ))}
            </ul>
          </Card>
        ) : null}

        <Collapse
          className="advanced-debug-panel mixing-advanced-collapse"
          ghost
          items={[
            {
              key: "advanced",
              label: "高级信息",
              children: (
                <div className="advanced-debug-content">
                  <p>Mixing 编码值：{normalizedMixing}</p>
                  <p>当前目录槽位数：{displaySlots.length}</p>
                  <p>当前禁用槽位数：{disabledMixingSlots?.size ?? 0}</p>
                  {disabledMixingSlots && disabledMixingSlots.size > 0 ? (
                    <ul className="mixing-ungrouped-list">
                      {displaySlots
                        .filter((slot) => disabledMixingSlots.has(slot.slot))
                        .map((slot) => (
                          <li key={`disabled-${slot.slot}`}>
                            {slot.displayName}
                          </li>
                        ))}
                    </ul>
                  ) : null}
                </div>
              ),
            },
          ]}
        />

        {errors.mixing ? <small className="error">{errors.mixing.message}</small> : null}
      </section>
    </>
  );
}
