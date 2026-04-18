import { useEffect, useMemo, useState } from "react";
import { zodResolver } from "@hookform/resolvers/zod";
import { FormProvider, useForm } from "react-hook-form";

import type { SearchAnalysisPayload } from "../../lib/contracts";
import { getParameterSpecStaticMax } from "../../lib/searchCatalog";
import { analyzeSearchRequest, formatTauriError } from "../../lib/tauri";
import { usePreviewStore } from "../../state/previewStore";
import { useSearchStore } from "../../state/searchStore";
import CountRuleEditor from "./CountRuleEditor";
import DistanceRuleEditor from "./DistanceRuleEditor";
import GeyserConstraintEditor from "./GeyserConstraintEditor";
import MixingSelector from "./MixingSelector";
import SearchAnalysisHints from "./SearchAnalysisHints";
import SearchActions from "./SearchActions";
import {
  createSearchSchema,
  decodeMixingToLevels,
  encodeMixingFromLevels,
  MIXING_SLOT_COUNT,
  toSearchDraft,
  toSearchFormValues,
  type SearchFormValues,
} from "./searchSchema";
import WorldSelector from "./WorldSelector";

interface SearchPanelProps {
  onSearchStarted?: () => void;
}

export default function SearchPanel({ onSearchStarted }: SearchPanelProps) {
  const worlds = useSearchStore((state) => state.worlds);
  const catalog = useSearchStore((state) => state.catalog);
  const geysers = useSearchStore((state) => state.geysers);
  const draft = useSearchStore((state) => state.draft);
  const isSearching = useSearchStore((state) => state.isSearching);
  const isCancelling = useSearchStore((state) => state.isCancelling);
  const startSearchJob = useSearchStore((state) => state.startSearchJob);
  const cancelSearchJob = useSearchStore((state) => state.cancelSearchJob);
  const clearResults = useSearchStore((state) => state.clearResults);
  const setDraft = useSearchStore((state) => state.setDraft);
  const lastError = useSearchStore((state) => state.lastError);
  const clearError = useSearchStore((state) => state.clearError);
  const clearPreview = usePreviewStore((state) => state.clear);

  const schema = useMemo(() => {
    const worldTypeMax = worlds.length > 0 ? worlds.length - 1 : undefined;
    const mixingMax = getParameterSpecStaticMax(catalog, "mixing") ?? undefined;
    return createSearchSchema({ worldTypeMax, mixingMax });
  }, [catalog, worlds]);

  const methods = useForm<SearchFormValues>({
    resolver: zodResolver(schema),
    mode: "onChange",
    defaultValues: toSearchFormValues(draft),
  });
  const [disabledGeyserKeys, setDisabledGeyserKeys] = useState<Set<string>>(new Set());
  const [disabledMixingSlots, setDisabledMixingSlots] = useState<Set<number>>(new Set());
  const [lastAnalysis, setLastAnalysis] = useState<SearchAnalysisPayload | null>(null);
  const watchWorldType = methods.watch("worldType");
  const watchMixing = methods.watch("mixing");
  const watchCpuMode = methods.watch("cpuMode");

  useEffect(() => {
    let cancelled = false;
    const timer = window.setTimeout(async () => {
      try {
        const analysis = await analyzeSearchRequest({
          jobId: `analyze-profile-${Date.now()}`,
          worldType: Number.isFinite(watchWorldType) ? watchWorldType : draft.worldType,
          seedStart: 0,
          seedEnd: 0,
          mixing: Number.isFinite(watchMixing) ? watchMixing : draft.mixing,
          threads: 0,
          constraints: {
            required: [],
            forbidden: [],
            distance: [],
            count: [],
          },
        });
        if (!cancelled) {
          setDisabledGeyserKeys(new Set(analysis.worldProfile.impossibleGeyserTypes));
          setDisabledMixingSlots(new Set(analysis.worldProfile.disabledMixingSlots));
        }
      } catch {
        if (!cancelled) {
          setDisabledGeyserKeys(new Set());
          setDisabledMixingSlots(new Set());
        }
      }
    }, 150);

    return () => {
      cancelled = true;
      window.clearTimeout(timer);
    };
  }, [draft.mixing, draft.worldType, watchMixing, watchWorldType]);

  useEffect(() => {
    if (disabledMixingSlots.size === 0) {
      return;
    }
    const slotCount = Math.max(catalog?.mixingSlots.length ?? 0, MIXING_SLOT_COUNT);
    const currentMixing = Number.isFinite(watchMixing) ? Math.max(0, Math.trunc(watchMixing)) : 0;
    const nextLevels = decodeMixingToLevels(currentMixing, slotCount);
    let forcedCount = 0;
    for (const slot of disabledMixingSlots) {
      if (slot < 0 || slot >= nextLevels.length) {
        continue;
      }
      if (nextLevels[slot] !== 0) {
        nextLevels[slot] = 0;
        forcedCount += 1;
      }
    }
    if (forcedCount === 0) {
      return;
    }
    const nextMixing = encodeMixingFromLevels(nextLevels);
    methods.setValue("mixing", nextMixing, { shouldValidate: true, shouldDirty: true });
    useSearchStore.setState({
      lastError: `世界参数变更后，已自动关闭 ${forcedCount} 个当前世界禁用的 mixing slot`,
    });
  }, [catalog?.mixingSlots.length, disabledMixingSlots, methods, watchMixing]);

  useEffect(() => {
    if (disabledGeyserKeys.size === 0) {
      return;
    }

    const values = methods.getValues();
    const nextRequired = values.required.filter((item) => !disabledGeyserKeys.has(item.geyser));
    const nextForbidden = values.forbidden.filter((item) => !disabledGeyserKeys.has(item.geyser));
    const nextDistance = values.distance.filter((item) => !disabledGeyserKeys.has(item.geyser));
    const nextCount = values.count.filter((item) => !disabledGeyserKeys.has(item.geyser));

    const removedCount =
      (values.required.length - nextRequired.length) +
      (values.forbidden.length - nextForbidden.length) +
      (values.distance.length - nextDistance.length) +
      (values.count.length - nextCount.length);
    if (removedCount === 0) {
      return;
    }

    methods.setValue("required", nextRequired, { shouldValidate: true, shouldDirty: true });
    methods.setValue("forbidden", nextForbidden, { shouldValidate: true, shouldDirty: true });
    methods.setValue("distance", nextDistance, { shouldValidate: true, shouldDirty: true });
    methods.setValue("count", nextCount, { shouldValidate: true, shouldDirty: true });
    useSearchStore.setState({
      lastError: `世界参数变更后，已自动移除 ${removedCount} 条当前世界不可生成的喷口约束`,
    });
  }, [disabledGeyserKeys, methods]);

  const submit = methods.handleSubmit(async (values) => {
    const nextDraft = toSearchDraft(values);
    setLastAnalysis(null);
    try {
      const analysis = await analyzeSearchRequest({
        jobId: `analyze-${Date.now()}`,
        worldType: nextDraft.worldType,
        seedStart: nextDraft.seedStart,
        seedEnd: nextDraft.seedEnd,
        mixing: nextDraft.mixing,
        threads: nextDraft.threads,
        cpu: nextDraft.cpu,
        constraints: nextDraft.constraints,
      });
      setLastAnalysis(analysis);
      if (analysis.errors.length > 0) {
        useSearchStore.setState({ lastError: analysis.errors[0].message });
        return;
      }
      if (analysis.warnings.length > 0) {
        useSearchStore.setState({ lastError: `[warning] ${analysis.warnings[0].message}` });
      }
    } catch (error) {
      setLastAnalysis(null);
      useSearchStore.setState({ lastError: formatTauriError(error) });
      return;
    }

    setDraft(nextDraft);
    clearPreview();
    const started = await startSearchJob(nextDraft);
    if (started) {
      onSearchStarted?.();
    }
  });

  const copyAsJson = async () => {
    const values = methods.getValues();
    const nextDraft = toSearchDraft(values);
    const text = JSON.stringify(
      {
        worldType: nextDraft.worldType,
        mixing: nextDraft.mixing,
        seedStart: nextDraft.seedStart,
        seedEnd: nextDraft.seedEnd,
        threads: nextDraft.threads,
        cpu: nextDraft.cpu,
        constraints: nextDraft.constraints,
      },
      null,
      2
    );
    try {
      await navigator.clipboard.writeText(text);
    } catch (error) {
      useSearchStore.setState({
        lastError: `复制失败: ${error instanceof Error ? error.message : String(error)}`,
      });
    }
  };

  return (
    <FormProvider {...methods}>
      <form className="search-panel" onSubmit={submit}>
        <header className="search-panel-header">
          <div>
            <h3>搜索参数</h3>
            <p>参数页独占整屏，优先横向平铺常用参数与规则编辑区。</p>
          </div>
          {lastError ? (
            <p className="error-inline" onClick={clearError}>
              参数提示: {lastError}
            </p>
          ) : null}
        </header>

        <section className="search-panel-grid">
          <section className="search-column search-column-main">
            <section className="search-section">
              <header className="search-section-header">
                <h4>世界参数</h4>
                <p>先按世界家族筛选具体世界，再按 DLC 包配置当前世界允许的混搭内容。</p>
              </header>
              <div className="world-parameter-layout">
                <WorldSelector worlds={worlds} />
                <MixingSelector
                  mixingSlots={catalog?.mixingSlots ?? []}
                  disabledMixingSlots={disabledMixingSlots}
                />
              </div>
            </section>

            <section className="search-section">
              <header className="search-section-header">
                <h4>性能参数</h4>
                <p>CPU 模式、线程数与调度策略。</p>
              </header>
              <div className="field-grid">
                <label className="field">
                  <span>CPU 模式</span>
                  <select {...methods.register("cpuMode")}>
                    <option value="balanced">平衡</option>
                    <option value="turbo">极速</option>
                    <option value="custom">自定义</option>
                  </select>
                  {methods.formState.errors.cpuMode ? (
                    <small className="error">{methods.formState.errors.cpuMode.message}</small>
                  ) : null}
                </label>
                <label className="field">
                  <span>线程数</span>
                  <input
                    type="number"
                    placeholder={
                      watchCpuMode === "custom"
                        ? "自定义模式下必填"
                        : "非自定义模式忽略该值"
                    }
                    {...methods.register("threads", { valueAsNumber: true })}
                  />
                  {methods.formState.errors.threads ? (
                    <small className="error">{methods.formState.errors.threads.message}</small>
                  ) : null}
                </label>
              </div>
              <label className="field">
                <span>调度选项</span>
                <div className="field-inline">
                  <label>
                    <input type="checkbox" {...methods.register("cpuAllowSmt")} />
                    允许 SMT
                  </label>
                  <label>
                    <input type="checkbox" {...methods.register("cpuAllowLowPerf")} />
                    允许低性能核心
                  </label>
                </div>
              </label>
            </section>

            <section className="search-section">
              <header className="search-section-header">
                <h4>Seed 范围</h4>
                <p>搜索的起止 seed 号。</p>
              </header>
              <div className="field-grid">
                <label className="field">
                  <span>Seed Start</span>
                  <input type="number" {...methods.register("seedStart", { valueAsNumber: true })} />
                  {methods.formState.errors.seedStart ? (
                    <small className="error">{methods.formState.errors.seedStart.message}</small>
                  ) : null}
                </label>
                <label className="field">
                  <span>Seed End</span>
                  <input type="number" {...methods.register("seedEnd", { valueAsNumber: true })} />
                  {methods.formState.errors.seedEnd ? (
                    <small className="error">{methods.formState.errors.seedEnd.message}</small>
                  ) : null}
                </label>
              </div>
            </section>

            <SearchAnalysisHints analysis={lastAnalysis} />

            <SearchActions
              isSearching={isSearching}
              isCancelling={isCancelling}
              onCancel={() => {
                void cancelSearchJob();
              }}
              onClear={() => {
                clearResults();
                clearPreview();
              }}
              onCopy={() => {
                void copyAsJson();
              }}
            />
          </section>

          <section className="search-column search-column-rules">
            <section className="search-section search-section-rules">
              <header className="search-section-header">
                <h4>喷口约束</h4>
                <p>
                  距离规则：第一栏为最小距离，第二栏为最大距离（从出生点到喷口的直线距离，单位：格）。
                  数量规则：第一栏为最小数量，第二栏为最大数量。
                </p>
              </header>
              <div className="search-rule-grid">
                <GeyserConstraintEditor
                  title="必须包含(required)"
                  type="required"
                  geysers={geysers}
                  disabledGeyserKeys={disabledGeyserKeys}
                />
                <GeyserConstraintEditor
                  title="必须排除(forbidden)"
                  type="forbidden"
                  geysers={geysers}
                  disabledGeyserKeys={disabledGeyserKeys}
                />
                <DistanceRuleEditor geysers={geysers} disabledGeyserKeys={disabledGeyserKeys} />
                <CountRuleEditor geysers={geysers} disabledGeyserKeys={disabledGeyserKeys} />
              </div>
            </section>
          </section>
        </section>
      </form>
    </FormProvider>
  );
}
