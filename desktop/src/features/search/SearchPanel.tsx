import { useEffect, useMemo, useState } from "react";
import { zodResolver } from "@hookform/resolvers/zod";
import { FormProvider, useForm } from "react-hook-form";

import { getParameterSpecStaticMax } from "../../lib/searchCatalog";
import { analyzeSearchRequest, formatTauriError } from "../../lib/tauri";
import { usePreviewStore } from "../../state/previewStore";
import { useSearchStore } from "../../state/searchStore";
import CountRuleEditor from "./CountRuleEditor";
import DistanceRuleEditor from "./DistanceRuleEditor";
import GeyserConstraintEditor from "./GeyserConstraintEditor";
import MixingSelector from "./MixingSelector";
import SearchActions from "./SearchActions";
import { createSearchSchema, toSearchDraft, toSearchFormValues, type SearchFormValues } from "./searchSchema";
import WorldSelector from "./WorldSelector";

export default function SearchPanel() {
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
  const watchWorldType = methods.watch("worldType");
  const watchMixing = methods.watch("mixing");

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
        }
      } catch {
        if (!cancelled) {
          setDisabledGeyserKeys(new Set());
        }
      }
    }, 150);

    return () => {
      cancelled = true;
      window.clearTimeout(timer);
    };
  }, [draft.mixing, draft.worldType, watchMixing, watchWorldType]);

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
      if (analysis.errors.length > 0) {
        useSearchStore.setState({ lastError: analysis.errors[0].message });
        return;
      }
      if (analysis.warnings.length > 0) {
        useSearchStore.setState({ lastError: `[warning] ${analysis.warnings[0].message}` });
      }
    } catch (error) {
      useSearchStore.setState({ lastError: formatTauriError(error) });
      return;
    }

    setDraft(nextDraft);
    clearPreview();
    await startSearchJob(nextDraft);
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
        <h3>搜索参数</h3>
        <WorldSelector worlds={worlds} />
        <MixingSelector />
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
      </form>
    </FormProvider>
  );
}
