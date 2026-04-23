import {
  Alert,
  Card,
  Checkbox,
  Flex,
  InputNumber,
  Select,
  Typography,
} from "antd";
import { useEffect, useMemo, useState } from "react";
import { zodResolver } from "@hookform/resolvers/zod";
import { Controller, FormProvider, useForm } from "react-hook-form";

import type { SearchAnalysisPayload } from "../../lib/contracts";
import { getParameterSpecStaticMax } from "../../lib/searchCatalog";
import type { SearchDraft } from "../../state/searchStore";
import {
  analyzeSearchRequest,
  formatTauriError,
  loadPreviewByCoord,
} from "../../lib/tauri";
import type { DesktopThemeMode } from "../../app/antdTheme";
import ThemeModeToggle from "../../components/layout/ThemeModeToggle";
import { usePreviewStore } from "../../state/previewStore";
import { useSearchStore } from "../../state/searchStore";
import CountRuleEditor from "./CountRuleEditor";
import DistanceRuleEditor from "./DistanceRuleEditor";
import GeyserConstraintEditor from "./GeyserConstraintEditor";
import MixingSelector from "./MixingSelector";
import CoordQuickSearch from "./CoordQuickSearch";
import { runCoordPreviewFlow } from "./coordPreviewFlow";
import SearchActions from "./SearchActions";
import SearchWarningConfirmModal from "./SearchWarningConfirmModal";
import { formatAnalysisErrorMessage } from "./searchAnalysisDisplay";
import { shouldShowSearchWarningConfirmation } from "./searchWarningPolicy";
import {
  createSearchSchema,
  toSearchDraft,
  toSearchFormValues,
  type SearchFormValues,
} from "./searchSchema";
import WorldSelector from "./WorldSelector";

interface SearchPanelProps {
  themeMode: DesktopThemeMode;
  onThemeModeChange: (mode: DesktopThemeMode) => void;
  onSearchStarted?: () => void;
  onViewResults?: () => void;
}

export default function SearchPanel({
  themeMode,
  onThemeModeChange,
  onSearchStarted,
  onViewResults,
}: SearchPanelProps) {
  const worlds = useSearchStore((state) => state.worlds);
  const catalog = useSearchStore((state) => state.catalog);
  const geysers = useSearchStore((state) => state.geysers);
  const draft = useSearchStore((state) => state.draft);
  const resultsCount = useSearchStore((state) => state.results.length);
  const isSearching = useSearchStore((state) => state.isSearching);
  const startSearchJob = useSearchStore((state) => state.startSearchJob);
  const openDirectCoordResult = useSearchStore((state) => state.openDirectCoordResult);
  const setDraft = useSearchStore((state) => state.setDraft);
  const lastError = useSearchStore((state) => state.lastError);
  const clearError = useSearchStore((state) => state.clearError);
  const clearPreview = usePreviewStore((state) => state.clear);
  const primeResolvedPreview = usePreviewStore((state) => state.primeResolvedPreview);
  const hasResults = resultsCount > 0 || isSearching;

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
  const [coordInput, setCoordInput] = useState("");
  const [isCoordSubmitting, setIsCoordSubmitting] = useState(false);
  const [disabledGeyserKeys, setDisabledGeyserKeys] = useState<Set<string>>(new Set());
  const [disabledMixingSlots, setDisabledMixingSlots] = useState<Set<number>>(new Set());
  const [pendingWarningConfirmation, setPendingWarningConfirmation] = useState<{
    draft: SearchDraft;
    analysis: SearchAnalysisPayload;
  } | null>(null);
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

  const startSearchWithDraft = async (nextDraft: SearchDraft) => {
    setDraft(nextDraft);
    clearPreview();
    const started = await startSearchJob(nextDraft);
    if (started) {
      onSearchStarted?.();
    }
  };

  const submit = methods.handleSubmit(async (values) => {
    const nextDraft = toSearchDraft(values);
    setPendingWarningConfirmation(null);
    try {
      const analysis = await analyzeSearchRequest({
        jobId: `analyze-${Date.now()}`,
        worldType: nextDraft.worldType,
        seedStart: nextDraft.seedStart,
        seedEnd: nextDraft.seedEnd,
        mixing: nextDraft.mixing,
        cpu: nextDraft.cpu,
        constraints: nextDraft.constraints,
      });
      if (analysis.errors.length > 0) {
        useSearchStore.setState({
          lastError: formatAnalysisErrorMessage(
            analysis.errors[0],
            analysis,
            nextDraft,
            catalog?.mixingSlots ?? [],
            geysers
          ),
        });
        return;
      }
      if (shouldShowSearchWarningConfirmation(analysis)) {
        setPendingWarningConfirmation({ draft: nextDraft, analysis });
        return;
      }
    } catch (error) {
      useSearchStore.setState({ lastError: formatTauriError(error) });
      return;
    }

    await startSearchWithDraft(nextDraft);
  });

  const handleWarningContinue = () => {
    if (!pendingWarningConfirmation) {
      return;
    }
    const nextDraft = pendingWarningConfirmation.draft;
    setPendingWarningConfirmation(null);
    void startSearchWithDraft(nextDraft);
  };

  const handleWarningAbandon = () => {
    setPendingWarningConfirmation(null);
  };

  const handleCoordSubmit = async () => {
    if (isSearching || isCoordSubmitting) {
      return;
    }

    const coord = coordInput.trim();
    setIsCoordSubmitting(true);
    clearError();
    try {
      await runCoordPreviewFlow(
        {
          loadPreviewByCoord: async (rawCoord) =>
            loadPreviewByCoord({
              jobId: `preview-coord-${Date.now()}-${Math.random().toString(36).slice(2, 8)}`,
              coord: rawCoord,
            }),
          openDirectCoordResult,
          primeResolvedPreview,
          setError: (message) => {
            useSearchStore.setState({ lastError: message });
          },
          openResults: () => {
            onViewResults?.();
          },
        },
        coord
      );
    } finally {
      setIsCoordSubmitting(false);
    }
  };

  return (
    <FormProvider {...methods}>
      <section className="search-panel">
        <header className="search-panel-header">
          <div className="search-panel-header-row">
            <Flex vertical gap={4} className="search-panel-title">
              <Typography.Title level={3}>搜索参数</Typography.Title>
            </Flex>
            <div className="search-panel-header-center">
              <CoordQuickSearch
                value={coordInput}
                loading={isCoordSubmitting}
                disabled={isSearching || isCoordSubmitting}
                onChange={(value) => {
                  setCoordInput(value);
                }}
                onSubmit={() => {
                  void handleCoordSubmit();
                }}
              />
            </div>
            <ThemeModeToggle mode={themeMode} onModeChange={onThemeModeChange} />
          </div>
          {lastError ? (
            <Alert
              className="search-panel-alert"
              type="error"
              showIcon
              closable
              title={`参数提示: ${lastError}`}
              onClose={clearError}
            />
          ) : null}
        </header>

        <form className="search-panel-form" onSubmit={submit}>
          <section className="search-panel-grid">
          <section className="search-column search-column-main">
            <Card className="search-section">
              <header className="search-section-header">
                <Typography.Title level={4}>世界参数</Typography.Title>
                <Typography.Paragraph>
                  先按世界家族筛选具体世界，再按 DLC 包配置当前世界允许的混搭内容。
                </Typography.Paragraph>
              </header>
              <div className="world-parameter-layout">
                <WorldSelector worlds={worlds} />
                <MixingSelector
                  mixingSlots={catalog?.mixingSlots ?? []}
                  disabledMixingSlots={disabledMixingSlots}
                />
              </div>
            </Card>

            <Card className="search-section">
              <header className="search-section-header">
                <Typography.Title level={4}>性能参数</Typography.Title>
                <Typography.Paragraph>CPU 模式与调度策略。</Typography.Paragraph>
              </header>
              <div className="field-grid">
                <div className="field">
                  <Typography.Text className="field-label">CPU 模式</Typography.Text>
                  <Controller
                    control={methods.control}
                    name="cpuMode"
                    render={({ field }) => (
                      <Select
                        className="field-control"
                        value={field.value}
                        options={[
                          { label: "平衡", value: "balanced" },
                          { label: "极速", value: "turbo" },
                        ]}
                        onChange={field.onChange}
                      />
                    )}
                  />
                  {methods.formState.errors.cpuMode ? (
                    <small className="error">{methods.formState.errors.cpuMode.message}</small>
                  ) : null}
                </div>
              </div>
              <div className="field">
                <Typography.Text className="field-label">调度选项</Typography.Text>
                <div className="field-inline">
                  <Controller
                    control={methods.control}
                    name="cpuAllowSmt"
                    render={({ field }) => (
                      <Checkbox checked={field.value} onChange={(event) => field.onChange(event.target.checked)}>
                        允许 SMT
                      </Checkbox>
                    )}
                  />
                  <Controller
                    control={methods.control}
                    name="cpuAllowLowPerf"
                    render={({ field }) => (
                      <Checkbox checked={field.value} onChange={(event) => field.onChange(event.target.checked)}>
                        允许低性能核心
                      </Checkbox>
                    )}
                  />
                </div>
              </div>
            </Card>

            <Card className="search-section">
              <header className="search-section-header">
                <Typography.Title level={4}>Seed 范围</Typography.Title>
              </header>
              <div className="field-grid">
                <div className="field">
                  <Typography.Text className="field-label">Seed Start</Typography.Text>
                  <Controller
                    control={methods.control}
                    name="seedStart"
                    render={({ field }) => (
                      <InputNumber
                        className="field-control"
                        value={field.value}
                        style={{ width: "100%" }}
                        onChange={(value) => field.onChange(value ?? undefined)}
                      />
                    )}
                  />
                  {methods.formState.errors.seedStart ? (
                    <small className="error">{methods.formState.errors.seedStart.message}</small>
                  ) : null}
                </div>
                <div className="field">
                  <Typography.Text className="field-label">Seed End</Typography.Text>
                  <Controller
                    control={methods.control}
                    name="seedEnd"
                    render={({ field }) => (
                      <InputNumber
                        className="field-control"
                        value={field.value}
                        style={{ width: "100%" }}
                        onChange={(value) => field.onChange(value ?? undefined)}
                      />
                    )}
                  />
                  {methods.formState.errors.seedEnd ? (
                    <small className="error">{methods.formState.errors.seedEnd.message}</small>
                  ) : null}
                </div>
              </div>
            </Card>

          </section>

          <section className="search-column search-column-rules">
            <Card className="search-section search-section-rules">
              <header className="search-section-header">
                <Typography.Title level={4}>喷口约束</Typography.Title>
                <Typography.Paragraph>
                  距离规则：第一栏为最小距离，第二栏为最大距离（从出生点到喷口的直线距离，单位：格）。
                  数量规则：第一栏为最小数量，第二栏为最大数量。
                </Typography.Paragraph>
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
            </Card>
            <SearchActions
              isSearching={isSearching}
              isBusy={isCoordSubmitting}
              hasResults={hasResults}
              resultsCount={resultsCount}
              onViewResults={() => {
                onViewResults?.();
              }}
            />
          </section>
          </section>
        </form>
      </section>
      <SearchWarningConfirmModal
        open={pendingWarningConfirmation !== null}
        analysis={pendingWarningConfirmation?.analysis ?? null}
        geysers={geysers}
        onContinue={handleWarningContinue}
        onAbandon={handleWarningAbandon}
      />
    </FormProvider>
  );
}
