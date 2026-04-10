#ifndef EMSCRIPTEN

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "App/AppRuntime.hpp"
#include "App/ResultSink.hpp"
#include "Batch/BatchMatcher.hpp"
#include "Batch/BatchSearchService.hpp"
#include "Batch/CpuTopology.hpp"
#include "Batch/FilterConfig.hpp"
#include "Batch/SidecarProtocol.hpp"
#include "Batch/ThreadPolicy.hpp"
#include "Batch/ThroughputCalibration.hpp"
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

struct ActiveSearchState {
    std::mutex mutex;
    bool running = false;
    std::string jobId;
    std::shared_ptr<std::atomic<bool>> cancelToken;
    std::thread worker;
};

static ActiveSearchState g_activeSearch;

void EmitLine(const std::string &line)
{
    std::lock_guard<std::mutex> lock(g_outputMutex);
    std::cout << line << '\n';
    std::cout.flush();
}

template<typename Builder>
void EmitBuiltLine(Builder &&builder)
{
    std::lock_guard<std::mutex> lock(g_outputMutex);
    const std::string line = builder();
    std::cout << line << '\n';
    std::cout.flush();
}

void JoinInactiveSearchWorker()
{
    std::thread worker;
    {
        std::lock_guard<std::mutex> lock(g_activeSearch.mutex);
        if (!g_activeSearch.running && g_activeSearch.worker.joinable()) {
            worker = std::move(g_activeSearch.worker);
            g_activeSearch.jobId.clear();
            g_activeSearch.cancelToken.reset();
        }
    }
    if (worker.joinable()) {
        worker.join();
    }
}

void MarkSearchFinished(const std::string &jobId)
{
    std::lock_guard<std::mutex> lock(g_activeSearch.mutex);
    if (g_activeSearch.running && g_activeSearch.jobId == jobId) {
        g_activeSearch.running = false;
    }
}

bool StartSearchWorker(const std::string &jobId,
                       const std::shared_ptr<std::atomic<bool>> &cancelToken,
                       std::thread &&worker,
                       std::string *errorMessage)
{
    JoinInactiveSearchWorker();
    std::lock_guard<std::mutex> lock(g_activeSearch.mutex);
    if (g_activeSearch.running) {
        if (errorMessage != nullptr) {
            *errorMessage = "another search job is still running";
        }
        return false;
    }
    g_activeSearch.running = true;
    g_activeSearch.jobId = jobId;
    g_activeSearch.cancelToken = cancelToken;
    g_activeSearch.worker = std::move(worker);
    return true;
}

bool RequestSearchCancel(const std::string &jobId)
{
    std::lock_guard<std::mutex> lock(g_activeSearch.mutex);
    if (!g_activeSearch.running || !g_activeSearch.cancelToken) {
        return false;
    }
    if (!jobId.empty() && g_activeSearch.jobId != jobId) {
        return false;
    }
    g_activeSearch.cancelToken->store(true, std::memory_order_relaxed);
    return true;
}

void ShutdownSearchWorker()
{
    RequestSearchCancel("");
    std::thread worker;
    {
        std::lock_guard<std::mutex> lock(g_activeSearch.mutex);
        if (g_activeSearch.worker.joinable()) {
            worker = std::move(g_activeSearch.worker);
        }
        g_activeSearch.running = false;
        g_activeSearch.jobId.clear();
        g_activeSearch.cancelToken.reset();
    }
    if (worker.joinable()) {
        worker.join();
    }
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

Batch::SearchRequest BuildSearchRequest(const Batch::FilterConfig &cfg,
                                        const BatchCpu::ThreadPolicy &policy,
                                        std::atomic<bool> *cancelRequested)
{
    Batch::SearchRequest request;
    request.seedStart = cfg.seedStart;
    request.seedEnd = cfg.seedEnd;
    request.workerCount = std::max<uint32_t>(1u, policy.workerCount);
    request.chunkSize = cfg.hasCpuSection ? cfg.cpu.chunkSize : 64;
    const auto totalSeeds = std::max(1, cfg.seedEnd - cfg.seedStart + 1);
    const auto configuredProgressInterval =
        cfg.hasCpuSection ? cfg.cpu.progressInterval : 1000;
    const auto smallRangeProgressInterval = std::max(1, totalSeeds / 20);
    request.progressInterval = std::max(
        1, std::min(configuredProgressInterval, smallRangeProgressInterval));
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

BatchCpu::ThreadPolicy SelectPolicy(const Batch::FilterConfig &cfg)
{
    const auto topology = Batch::DetectCpuTopology();
    const auto policyRequest = Batch::BuildThreadPolicyRequestFromFilter(cfg);
    const auto plannerInput = Batch::BuildPlannerInput(policyRequest, topology);
    const auto candidates = Batch::BuildThreadPolicyCandidates(policyRequest, topology);
    if (candidates.empty()) {
        return BatchCpu::ThreadPolicy{};
    }

    const bool legacyThreadsOnly = !cfg.hasCpuSection && cfg.threads > 0;
    bool enableWarmup = cfg.cpu.enableWarmup && !legacyThreadsOnly &&
                        plannerInput.mode != BatchCpu::CpuMode::Custom &&
                        plannerInput.mode != BatchCpu::CpuMode::Conservative &&
                        candidates.size() > 1;
    if (cfg.seedStart >= cfg.seedEnd) {
        enableWarmup = false;
    }
    if (!enableWarmup) {
        return candidates.front();
    }

    const int warmupEnd = std::min(
        cfg.seedEnd,
        cfg.seedStart + std::max(1, cfg.cpu.warmupSeedCount) - 1);

    Batch::ThroughputCalibrationOptions warmupConfig;
    warmupConfig.enableWarmup = true;
    warmupConfig.totalBudget = std::chrono::milliseconds(cfg.cpu.warmupTotalMs);
    warmupConfig.perCandidateBudget = std::chrono::milliseconds(cfg.cpu.warmupPerCandidateMs);
    warmupConfig.tieToleranceRatio = cfg.cpu.warmupTieTolerance;

    const auto calibration = Batch::SelectThreadPolicyWithWarmup(
        "sidecar-session",
        candidates,
        warmupConfig,
        [&](const BatchCpu::ThreadPolicy &policy, std::chrono::milliseconds budget) {
            auto warmupRequest = BuildSearchRequest(cfg, policy, nullptr);
            warmupRequest.seedEnd = warmupEnd;
            warmupRequest.enableAdaptive = false;
            warmupRequest.adaptiveConfig.enabled = false;
            warmupRequest.maxRunDuration = budget;
            const auto run = Batch::BatchSearchService::Run(warmupRequest);
            return run.throughput;
        });
    return calibration.selectedPolicy;
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

    const auto selectedPolicy = SelectPolicy(cfg);
    auto cancelRequested = std::make_shared<std::atomic<bool>>(false);

    std::thread worker([cfg, request, selectedPolicy, cancelRequested]() {
        try {
            Batch::SearchEventCallbacks callbacks;
            callbacks.onStarted = [&](const Batch::SearchStartedEvent &event) {
                EmitBuiltLine([&]() { return Batch::SerializeStartedEvent(request.jobId, event); });
            };
            callbacks.onProgress = [&](const Batch::SearchProgressEvent &event) {
                EmitBuiltLine([&]() { return Batch::SerializeProgressEvent(request.jobId, event); });
            };
            callbacks.onMatch = [&](const Batch::SearchMatchEvent &event) {
                EmitBuiltLine([&]() { return Batch::SerializeMatchEvent(request.jobId, event); });
            };
            callbacks.onCompleted = [&](const Batch::SearchCompletedEvent &event) {
                EmitBuiltLine([&]() { return Batch::SerializeCompletedEvent(request.jobId, event); });
            };
            callbacks.onFailed = [&](const Batch::SearchFailedEvent &event) {
                EmitBuiltLine([&]() { return Batch::SerializeFailedEvent(request.jobId, event); });
            };
            callbacks.onCancelled = [&](const Batch::SearchCancelledEvent &event) {
                EmitBuiltLine([&]() { return Batch::SerializeCancelledEvent(request.jobId, event); });
            };

            auto searchRequest = BuildSearchRequest(cfg, selectedPolicy, cancelRequested.get());
            (void)Batch::BatchSearchService::Run(searchRequest, callbacks);
        } catch (const std::exception &ex) {
            EmitLine(Batch::SerializeFailedEvent(request.jobId, ex.what()));
        } catch (...) {
            EmitLine(Batch::SerializeFailedEvent(request.jobId, "search thread crashed"));
        }
        MarkSearchFinished(request.jobId);
    });

    std::string startError;
    if (!StartSearchWorker(request.jobId, cancelRequested, std::move(worker), &startError)) {
        if (worker.joinable()) {
            worker.join();
        }
        EmitLine(Batch::SerializeFailedEvent(request.jobId, startError));
    }
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
        JoinInactiveSearchWorker();
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
            if (!RequestSearchCancel(parsed.request.cancel.jobId)) {
                EmitLine(Batch::SerializeFailedEvent(parsed.request.cancel.jobId,
                                                     "job is not running"));
            }
            break;
        default:
            EmitLine(Batch::SerializeFailedEvent("unknown", "unknown command"));
            break;
        }
    }

    ShutdownSearchWorker();
    return 0;
}

#endif
