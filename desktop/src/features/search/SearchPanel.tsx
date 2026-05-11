import {
  Card,
  Checkbox,
  Flex,
  InputNumber,
  Select,
  Typography,
} from "antd";
import { useEffect, useMemo, useRef, useState } from "react";
import { zodResolver } from "@hookform/resolvers/zod";
import { Controller, FormProvider, useForm } from "react-hook-form";

import type { SearchAnalysisPayload } from "../../lib/contracts";
import { FALLBACK_SEARCH_CATALOG, getParameterSpecStaticMax } from "../../lib/searchCatalog";
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
import { validateNativeCoordInput } from "./nativeCoordValidation";
import SearchActions from "./SearchActions";
import SearchConstraintAlerts from "./SearchConstraintAlerts";
import SearchWarningConfirmModal from "./SearchWarningConfirmModal";
import { buildWorldConstraintAlertItems } from "./geyserConstraintPresentation.ts";
import { formatAnalysisErrorMessage } from "./searchAnalysisDisplay";
import { shouldShowSearchWarningConfirmation } from "./searchWarningPolicy";
import {
  COUNT_MAX_SENTINEL,
  createSearchSchema,
  getDefaultAllowLowPerfForCpuMode,
  resolveCountAutoMax,
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

async function loadWorldProfile(worldType: number, mixing: number): Promise<SearchAnalysisPayload["worldProfile"]> {
  const analysis = await analyzeSearchRequest({
    jobId: `analyze-profile-${Date.now()}`,
    worldType,
    seedStart: 0,
    seedEnd: 0,
    mixing,
    constraints: {
      required: [],
      forbidden: [],
      distance: [],
      count: [],
    },
  });
  return analysis.worldProfile;
}

function hasCountAutoMax(draft: SearchDraft): boolean {
  return draft.constraints.count.some((item) => item.maxCount === COUNT_MAX_SENTINEL);
}

function hasCompletePossibleMaxMap(
  draft: SearchDraft,
  possibleMaxCountByType: Record<string, number>
): boolean {
  return draft.constraints.count.every((item) => {
    if (item.maxCount !== COUNT_MAX_SENTINEL) {
      return true;
    }
    const possibleMax = possibleMaxCountByType[item.geyser];
    return Number.isInteger(possibleMax) && possibleMax >= 0;
  });
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
  const [isSearchSubmitting, setIsSearchSubmitting] = useState(false);
  const [disabledGeyserKeys, setDisabledGeyserKeys] = useState<Set<string>>(new Set());
  const [disabledMixingSlots, setDisabledMixingSlots] = useState<Set<number>>(new Set());
  const [possibleMaxCountByType, setPossibleMaxCountByType] = useState<Record<string, number>>({});
  const [pendingWarningConfirmation, setPendingWarningConfirmation] = useState<{
    uiDraft: SearchDraft;
    submitDraft: SearchDraft;
    analysis: SearchAnalysisPayload;
  } | null>(null);
  const watchWorldType = methods.watch("worldType");
  const watchMixing = methods.watch("mixing");
  const watchCpuMode = methods.watch("cpuMode");
  const watchRequired = methods.watch("required") ?? [];
  const watchForbidden = methods.watch("forbidden") ?? [];
  const watchDistance = methods.watch("distance") ?? [];
  const watchCount = methods.watch("count") ?? [];
  const previousCpuModeRef = useRef(watchCpuMode);
  const worldConstraintAlerts = useMemo(
    () =>
      buildWorldConstraintAlertItems({
        constraints: {
          required: watchRequired,
          forbidden: watchForbidden,
          distance: watchDistance,
          count: watchCount,
        },
        disabledGeyserKeys,
      }),
    [disabledGeyserKeys, watchCount, watchDistance, watchForbidden, watchRequired]
  );

  useEffect(() => {
    const previousCpuMode = previousCpuModeRef.current;
    previousCpuModeRef.current = watchCpuMode;
    if (watchCpuMode === "turbo" || previousCpuMode === "turbo") {
      methods.setValue("cpuAllowLowPerf", getDefaultAllowLowPerfForCpuMode(watchCpuMode), {
        shouldDirty: true,
        shouldValidate: true,
      });
    }
  }, [methods, watchCpuMode]);

  useEffect(() => {
    let cancelled = false;
    const timer = window.setTimeout(async () => {
      try {
        const worldProfile = await loadWorldProfile(
          Number.isFinite(watchWorldType) ? watchWorldType : draft.worldType,
          Number.isFinite(watchMixing) ? watchMixing : draft.mixing
        );
        if (!cancelled) {
          setDisabledGeyserKeys(new Set(worldProfile.impossibleGeyserTypes));
          setDisabledMixingSlots(new Set(worldProfile.disabledMixingSlots));
          setPossibleMaxCountByType(worldProfile.possibleMaxCountByType);
        }
      } catch {
        if (!cancelled) {
          setDisabledGeyserKeys(new Set());
          setDisabledMixingSlots(new Set());
          setPossibleMaxCountByType({});
        }
      }
    }, 150);

    return () => {
      cancelled = true;
      window.clearTimeout(timer);
    };
  }, [draft.mixing, draft.worldType, watchMixing, watchWorldType]);

  const startSearchWithDraft = async (uiDraft: SearchDraft, submitDraft: SearchDraft) => {
    setDraft(uiDraft);
    clearPreview();
    const started = await startSearchJob(submitDraft);
    setDraft(uiDraft);
    if (started) {
      onSearchStarted?.();
    }
  };

  const submit = methods.handleSubmit(async (values) => {
    const uiDraft = toSearchDraft(values);
    let nextDraft = uiDraft;
    setPendingWarningConfirmation(null);
    setIsSearchSubmitting(true);
    try {
      if (hasCountAutoMax(uiDraft)) {
        let resolvedPossibleMaxCountByType = possibleMaxCountByType;
        if (!hasCompletePossibleMaxMap(uiDraft, resolvedPossibleMaxCountByType)) {
          const worldProfile = await loadWorldProfile(uiDraft.worldType, uiDraft.mixing);
          resolvedPossibleMaxCountByType = worldProfile.possibleMaxCountByType;
          setDisabledGeyserKeys(new Set(worldProfile.impossibleGeyserTypes));
          setDisabledMixingSlots(new Set(worldProfile.disabledMixingSlots));
          setPossibleMaxCountByType(worldProfile.possibleMaxCountByType);
        }
        nextDraft = resolveCountAutoMax(uiDraft, resolvedPossibleMaxCountByType);
      }
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
        setPendingWarningConfirmation({ uiDraft, submitDraft: nextDraft, analysis });
        return;
      }
    } catch (error) {
      useSearchStore.setState({ lastError: formatTauriError(error) });
      return;
    } finally {
      setIsSearchSubmitting(false);
    }

    await startSearchWithDraft(uiDraft, nextDraft);
  });

  const handleWarningContinue = () => {
    if (!pendingWarningConfirmation) {
      return;
    }
    const nextUiDraft = pendingWarningConfirmation.uiDraft;
    const nextSubmitDraft = pendingWarningConfirmation.submitDraft;
    setPendingWarningConfirmation(null);
    void startSearchWithDraft(nextUiDraft, nextSubmitDraft);
  };

  const handleWarningAbandon = () => {
    setPendingWarningConfirmation(null);
  };

  const handleCoordSubmit = async () => {
    if (isSearching || isCoordSubmitting) {
      return;
    }

    const coord = coordInput.trim();
    const coordError = validateNativeCoordInput(
      coord,
      (worlds.length > 0 ? worlds : FALLBACK_SEARCH_CATALOG.worlds).map((item) => item.code)
    );
    if (coordError) {
      useSearchStore.setState({ lastError: coordError });
      return;
    }
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
          <SearchConstraintAlerts
            lastError={lastError}
            items={worldConstraintAlerts}
            onCloseLastError={clearError}
          />
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
                      <Checkbox
                        checked={watchCpuMode === "turbo" ? getDefaultAllowLowPerfForCpuMode(watchCpuMode) : field.value}
                        disabled={watchCpuMode === "turbo"}
                        onChange={(event) => field.onChange(event.target.checked)}
                      >
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
                    “必须包含”表示该喷口在整张地图里的总出现次数范围，“距离规则”表示至少有一个目标喷口落在指定范围内。
                </Typography.Paragraph>
              </header>
              <div className="search-rule-grid">
                <CountRuleEditor geysers={geysers} disabledGeyserKeys={disabledGeyserKeys} />
                <GeyserConstraintEditor
                  title="必须排除"
                  type="forbidden"
                  geysers={geysers}
                  disabledGeyserKeys={disabledGeyserKeys}
                />
                <DistanceRuleEditor geysers={geysers} disabledGeyserKeys={disabledGeyserKeys} />
              </div>
            </Card>
            <SearchActions
              isSearching={isSearching}
              isBusy={isCoordSubmitting || isSearchSubmitting}
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
