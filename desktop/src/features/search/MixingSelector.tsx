import { useMemo } from "react";
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
      return "保证";
    case "normal":
      return "普通";
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
  return (
    <select
      value={value}
      onChange={(event) => {
        onChange(event.target.value as MixingUiMode);
      }}
      onClick={(event) => event.stopPropagation()}
      disabled={disabled}
    >
      <option value="normal">普通</option>
      <option value="guaranteed">保证</option>
    </select>
  );
}

export default function MixingSelector({ mixingSlots, disabledMixingSlots }: MixingSelectorProps) {
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
          <h5>世界混搭</h5>
          <p>按 DLC 包理解混搭内容。勾选后可选普通或保证，未勾选即为禁用。</p>
        </header>

        <div className="mixing-package-list">
          {groups.map((group) => {
            const packageLevel = levels[group.packageSlot.slot] ?? 0;
            const packageMode = getPackageMode(levels, group);
            const packageEnabled = isSlotEnabled(packageLevel);
            const packageDisabled = disabledMixingSlots?.has(group.packageSlot.slot) ?? false;

            return (
              <details
                key={`${group.packageSlot.slot}-${group.packageSlot.path}`}
                className={`mixing-package-card${packageEnabled ? " active" : ""}${
                  packageDisabled ? " disabled" : ""
                }`}
                open={packageEnabled}
              >
                <summary className="mixing-package-card-header">
                  <label
                    className="mixing-package-toggle"
                    onClick={(event) => event.stopPropagation()}
                  >
                    <input
                      type="checkbox"
                      checked={packageEnabled}
                      disabled={packageDisabled}
                      onChange={(event) => {
                        const nextLevels = applyPackageMode(
                          levels,
                          group,
                          event.target.checked ? "normal" : "off"
                        );
                        commitLevels(nextLevels);
                      }}
                    />
                    <span>{group.packageSlot.displayName}</span>
                  </label>
                  {packageEnabled ? (
                    <ModeSelect
                      value={packageMode === "off" ? "normal" : packageMode}
                      disabled={packageDisabled}
                      onChange={(mode) => {
                        commitLevels(applyPackageMode(levels, group, mode));
                      }}
                    />
                  ) : (
                    <span className="mixing-mode-badge">{renderModeLabel(packageMode)}</span>
                  )}
                </summary>

                <p className="mixing-package-description">
                  {group.packageSlot.description || group.packageSlot.path}
                  {packageDisabled ? " 当前世界不可用。" : ""}
                </p>

                {group.children.length > 0 ? (
                  <div className="mixing-child-list">
                    {group.children.map((child) => {
                      const childLevel = levels[child.slot] ?? 0;
                      const childMode = levelToUiMode(childLevel);
                      const childEnabled = isSlotEnabled(childLevel);
                      const childDisabled = (disabledMixingSlots?.has(child.slot) ?? false) || !packageEnabled;
                      const disabledReason =
                        disabledMixingSlots?.has(child.slot) ?? false
                          ? "当前世界不可用"
                          : !packageEnabled
                            ? "请先启用对应 DLC 包"
                            : "";

                      return (
                        <div key={`${child.slot}-${child.path}`} className="mixing-child-row">
                          <label className="mixing-child-toggle">
                            <input
                              type="checkbox"
                              checked={childEnabled}
                              disabled={childDisabled}
                              onChange={(event) => {
                                commitLevels(
                                  applyChildMode(
                                    levels,
                                    child.slot,
                                    event.target.checked ? "normal" : "off"
                                  )
                                );
                              }}
                            />
                            <span className="mixing-child-name">
                              [{child.slot}] {child.displayName}
                            </span>
                          </label>

                          {childEnabled ? (
                            <ModeSelect
                              value={childMode === "off" ? "normal" : childMode}
                              disabled={childDisabled}
                              onChange={(mode) => {
                                commitLevels(applyChildMode(levels, child.slot, mode));
                              }}
                            />
                          ) : (
                            <span className="mixing-mode-badge">{renderModeLabel(childMode)}</span>
                          )}

                          <span className="mixing-child-description">
                            {child.description || child.path}
                          </span>
                          {disabledReason ? (
                            <span className="mixing-disabled-note">{disabledReason}</span>
                          ) : null}
                        </div>
                      );
                    })}
                  </div>
                ) : (
                  <p className="hint">该 DLC 包没有额外可选槽位，只有包本身开关。</p>
                )}
              </details>
            );
          })}
        </div>

        {ungroupedSlots.length > 0 ? (
          <section className="advanced-debug-panel">
            <header>
              <h5>未分类槽位</h5>
              <p>这些槽位暂未归入 DLC 包分组，仍保留只读展示以避免信息丢失。</p>
            </header>
            <ul className="mixing-ungrouped-list">
              {ungroupedSlots.map((slot) => (
                <li key={`${slot.slot}-${slot.path}`}>
                  [{slot.slot}] {slot.displayName}
                </li>
              ))}
            </ul>
          </section>
        ) : null}

        <details className="advanced-debug-panel">
          <summary>高级信息</summary>
          <div className="advanced-debug-content">
            <p>Mixing 编码值：{normalizedMixing}</p>
            <p>当前目录槽位数：{displaySlots.length}</p>
            <p>当前禁用槽位数：{disabledMixingSlots?.size ?? 0}</p>
            {disabledMixingSlots && disabledMixingSlots.size > 0 ? (
              <ul className="mixing-ungrouped-list">
                {displaySlots
                  .filter((slot) => disabledMixingSlots.has(slot.slot))
                  .map((slot) => (
                    <li key={`disabled-${slot.slot}`}>[{slot.slot}] {slot.displayName}</li>
                  ))}
              </ul>
            ) : null}
          </div>
        </details>

        {errors.mixing ? <small className="error">{errors.mixing.message}</small> : null}
      </section>
    </>
  );
}
