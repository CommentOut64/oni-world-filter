import { useMemo } from "react";
import { zodResolver } from "@hookform/resolvers/zod";
import { FormProvider, useForm } from "react-hook-form";

import { getParameterSpecStaticMax } from "../../lib/searchCatalog";
import { usePreviewStore } from "../../state/previewStore";
import { useSearchStore } from "../../state/searchStore";
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

  const submit = methods.handleSubmit(async (values) => {
    const nextDraft = toSearchDraft(values);
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

        <GeyserConstraintEditor title="必须包含(required)" type="required" geysers={geysers} />
        <GeyserConstraintEditor title="必须排除(forbidden)" type="forbidden" geysers={geysers} />
        <DistanceRuleEditor geysers={geysers} />

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
