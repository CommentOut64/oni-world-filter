#ifndef EMSCRIPTEN

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "App/AppRuntime.hpp"
#include "App/ResultSink.hpp"
#include "Batch/BatchMatcher.hpp"
#include "Batch/BatchSearchService.hpp"
#include "Batch/FilterConfig.hpp"
#include "Batch/SidecarProtocol.hpp"
#include "BatchCpu/CpuOptimization.hpp"
#include "Setting/SettingsCache.hpp"
#include "config.h"

namespace {

static const char *kWorldPrefixes[] = {
    "SNDST-A-",  "OCAN-A-",    "S-FRZ-",     "LUSH-A-",    "FRST-A-",
    "VOLCA-",    "BAD-A-",     "HTFST-A-",   "OASIS-A-",   "CER-A-",
    "CERS-A-",   "PRE-A-",     "PRES-A-",    "V-SNDST-C-", "V-OCAN-C-",
    "V-SWMP-C-", "V-SFRZ-C-",  "V-LUSH-C-",  "V-FRST-C-",  "V-VOLCA-C-",
    "V-BAD-C-",  "V-HTFST-C-", "V-OASIS-C-", "V-CER-C-",   "V-CERS-C-",
    "V-PRE-C-",  "V-PRES-C-",  "SNDST-C-",   "PRE-C-",     "CER-C-",
    "FRST-C-",   "SWMP-C-",    "M-SWMP-C-",  "M-BAD-C-",   "M-FRZ-C-",
    "M-FLIP-C-", "M-RAD-C-",   "M-CERS-C-"};

static thread_local BatchCaptureSink g_batchSink;
static std::mutex g_outputMutex;

void EmitLine(const std::string &line)
{
    std::lock_guard<std::mutex> lock(g_outputMutex);
    std::cout << line << '\n';
    std::cout.flush();
}

bool BuildWorldCode(int worldType, int seed, int mixing, std::string *code)
{
    if (code == nullptr) {
        return false;
    }
    if (worldType < 0 || static_cast<int>(std::size(kWorldPrefixes)) <= worldType) {
        return false;
    }
    *code = kWorldPrefixes[worldType];
    *code += std::to_string(seed);
    *code += "-0-D3-";
    *code += SettingsCache::BinaryToBase36(mixing);
    return true;
}

class PreviewCaptureSink final : public ResultSink
{
public:
    bool RequestResource(uint32_t expectedSize, std::vector<char> &data) override
    {
        data.assign(expectedSize, 0);
        std::ifstream file(SETTING_ASSET_FILEPATH, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        const auto size = file.seekg(0, std::ios::end).tellg();
        if (size != static_cast<std::streamoff>(expectedSize)) {
            return false;
        }
        file.seekg(0).read(data.data(), expectedSize);
        return file.good();
    }

    void OnGeneratedWorldSummary(const GeneratedWorldSummary &summary) override
    {
        m_lastSummary = summary;
    }

    void OnGeneratedWorldPreview(const GeneratedWorldPreview &preview) override
    {
        m_lastPreview = preview;
    }

    const std::optional<GeneratedWorldPreview> &LastPreview() const
    {
        return m_lastPreview;
    }

private:
    std::optional<GeneratedWorldSummary> m_lastSummary;
    std::optional<GeneratedWorldPreview> m_lastPreview;
};

BatchCpu::PlannerInput BuildPlannerInput(const Batch::FilterConfig &cfg,
                                         const BatchCpu::CpuTopology &topology)
{
    BatchCpu::PlannerInput input;
    input.topology = &topology;

    const bool legacyThreadsOnly = !cfg.hasCpuSection && cfg.threads > 0;
    if (legacyThreadsOnly) {
        input.mode = BatchCpu::CpuMode::Custom;
        input.customWorkers = static_cast<uint32_t>(cfg.threads);
        input.customAllowSmt = true;
        input.customAllowLowPerf = true;
        input.customPlacement = BatchCpu::PlacementMode::Preferred;
        return input;
    }

    input.mode = BatchCpu::ParseCpuMode(cfg.cpu.mode);
    input.customWorkers = static_cast<uint32_t>(std::max(0, cfg.cpu.workers));
    input.customAllowSmt = cfg.cpu.allowSmt;
    input.customAllowLowPerf = cfg.cpu.allowLowPerf;
    input.customPlacement = BatchCpu::ParsePlacementMode(cfg.cpu.placement);
    return input;
}

Batch::SearchRequest BuildSearchRequest(const Batch::FilterConfig &cfg,
                                        const BatchCpu::ThreadPolicy &policy,
                                        std::atomic<bool> *cancelRequested)
{
    Batch::SearchRequest request;
    request.seedStart = cfg.seedStart;
    request.seedEnd = cfg.seedEnd;
    request.workerCount = std::max<uint32_t>(1u, policy.workerCount);
    request.chunkSize = cfg.hasCpuSection ? cfg.cpu.chunkSize : 64;
    request.progressInterval = cfg.hasCpuSection ? cfg.cpu.progressInterval : 1000;
    request.sampleWindow = std::chrono::milliseconds(
        cfg.hasCpuSection ? cfg.cpu.sampleWindowMs : 2000);
    request.maxRunDuration = std::chrono::milliseconds(0);
    request.enableAdaptive = cfg.hasCpuSection && cfg.cpu.enableAdaptiveDown;
    request.adaptiveConfig.enabled = cfg.hasCpuSection && cfg.cpu.enableAdaptiveDown;
    request.adaptiveConfig.minWorkers = static_cast<uint32_t>(std::max(1, cfg.cpu.adaptiveMinWorkers));
    request.adaptiveConfig.dropThreshold = cfg.cpu.adaptiveDropThreshold;
    request.adaptiveConfig.consecutiveDropWindows = cfg.cpu.adaptiveDropWindows;
    request.adaptiveConfig.cooldown = std::chrono::milliseconds(cfg.cpu.adaptiveCooldownMs);
    request.cancelRequested = cancelRequested;

    request.initializeWorker = []() {
        auto *runtime = AppRuntime::Instance();
        runtime->SetResultSink(&g_batchSink);
        runtime->SetSkipPolygons(true);
        g_batchSink.SetActive(true);
        runtime->Initialize(0);
    };
    request.applyThreadPlacement = [policy](uint32_t workerIndex, std::string *errorMessage) {
        return BatchCpu::ApplyThreadPlacement(policy, workerIndex, errorMessage);
    };
    request.evaluateSeed = [&cfg](int seed) {
        Batch::SearchSeedEvaluation evaluation;

        auto *runtime = AppRuntime::Instance();
        g_batchSink.Reset();

        std::string code;
        if (!BuildWorldCode(cfg.worldType, seed, cfg.mixing, &code)) {
            evaluation.ok = false;
            evaluation.errorMessage = "invalid worldType";
            return evaluation;
        }

        evaluation.generated = runtime->Generate(code, 0);
        if (!evaluation.generated) {
            return evaluation;
        }

        const auto matchResult = Batch::MatchFilter(cfg, g_batchSink.Data());
        if (!matchResult.Ok()) {
            evaluation.ok = false;
            evaluation.errorMessage = matchResult.errors.empty()
                                          ? "invalid capture for matcher"
                                          : matchResult.errors.front().detail;
            return evaluation;
        }
        evaluation.matched = matchResult.matched;
        if (evaluation.matched) {
            evaluation.capture = g_batchSink.Data();
        }
        return evaluation;
    };

    return request;
}

void RunSearchCommand(const Batch::SidecarSearchRequest &request)
{
    std::vector<Batch::FilterError> errors;
    auto cfg = Batch::BuildFilterConfigFromSidecarSearch(request, &errors);
    if (cfg.seedStart > cfg.seedEnd) {
        errors.push_back(Batch::FilterError{
            .code = Batch::FilterErrorCode::InvalidSeedRange,
            .field = "seedStart/seedEnd",
            .detail = "seedStart must be <= seedEnd",
        });
    }
    if (!errors.empty()) {
        EmitLine(Batch::SerializeFailedEvent(request.jobId, Batch::FormatFilterError(errors.front())));
        return;
    }

    auto topology = BatchCpu::CpuTopologyDetector::Detect();
    auto plannerInput = BuildPlannerInput(cfg, topology);
    auto candidates = BatchCpu::ThreadPolicyPlanner::BuildCandidates(plannerInput);
    if (candidates.empty()) {
        candidates.push_back(BatchCpu::ThreadPolicyPlanner::BuildConservativePolicy(topology));
    }
    const auto selectedPolicy = candidates.front();

    std::atomic<bool> cancelRequested{false};
    auto searchRequest = BuildSearchRequest(cfg, selectedPolicy, &cancelRequested);

    Batch::SearchEventCallbacks callbacks;
    callbacks.onStarted = [&](const Batch::SearchStartedEvent &event) {
        EmitLine(Batch::SerializeStartedEvent(request.jobId, event));
    };
    callbacks.onProgress = [&](const Batch::SearchProgressEvent &event) {
        EmitLine(Batch::SerializeProgressEvent(request.jobId, event));
    };
    callbacks.onMatch = [&](const Batch::SearchMatchEvent &event) {
        EmitLine(Batch::SerializeMatchEvent(request.jobId, event));
    };
    callbacks.onCompleted = [&](const Batch::SearchCompletedEvent &event) {
        EmitLine(Batch::SerializeCompletedEvent(request.jobId, event));
    };
    callbacks.onFailed = [&](const Batch::SearchFailedEvent &event) {
        EmitLine(Batch::SerializeFailedEvent(request.jobId, event));
    };
    callbacks.onCancelled = [&](const Batch::SearchCancelledEvent &event) {
        EmitLine(Batch::SerializeCancelledEvent(request.jobId, event));
    };

    (void)Batch::BatchSearchService::Run(searchRequest, callbacks);
}

void RunPreviewCommand(const Batch::SidecarPreviewRequest &request)
{
    PreviewCaptureSink previewSink;
    auto *runtime = AppRuntime::Instance();
    runtime->SetResultSink(&previewSink);
    runtime->SetSkipPolygons(false);
    runtime->Initialize(0);

    std::string code;
    if (!BuildWorldCode(request.worldType, request.seed, request.mixing, &code)) {
        EmitLine(Batch::SerializeFailedEvent(request.jobId, "invalid worldType"));
        return;
    }
    if (!runtime->Generate(code, 0)) {
        EmitLine(Batch::SerializeFailedEvent(request.jobId, "preview generate failed"));
        return;
    }
    if (!previewSink.LastPreview().has_value()) {
        EmitLine(Batch::SerializeFailedEvent(request.jobId, "preview payload is empty"));
        return;
    }

    EmitLine(Batch::SerializePreviewEvent(request.jobId, request, previewSink.LastPreview().value()));
}

} // namespace

int main()
{
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }

        const auto parsed = Batch::ParseSidecarRequest(line);
        if (!parsed.Ok()) {
            EmitLine(Batch::SerializeFailedEvent("unknown", parsed.error));
            continue;
        }

        switch (parsed.request.command) {
        case Batch::SidecarCommandType::Search:
            RunSearchCommand(parsed.request.search);
            break;
        case Batch::SidecarCommandType::Preview:
            RunPreviewCommand(parsed.request.preview);
            break;
        case Batch::SidecarCommandType::Cancel:
            EmitLine(Batch::SerializeFailedEvent(parsed.request.cancel.jobId,
                                                 "cancel command is not supported in this phase"));
            break;
        default:
            EmitLine(Batch::SerializeFailedEvent("unknown", "unknown command"));
            break;
        }
    }
    return 0;
}

#endif
