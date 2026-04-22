#ifndef __EMSCRIPTEN__

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
#include "Batch/DesktopSearchRuntimeMode.hpp"
#include "Batch/FilterConfig.hpp"
#include "Batch/SidecarProtocol.hpp"
#include "Batch/ThreadPolicy.hpp"
#include "BatchCpu/CpuOptimization.hpp"
#include "SearchAnalysis/HardValidator.hpp"
#include "SearchAnalysis/SearchCatalog.hpp"
#include "SearchAnalysis/WorldEnvelopeProfile.hpp"
#include "Setting/SettingsCache.hpp"
#include "config.h"

namespace {

static thread_local BatchCaptureSink g_batchSink;
static std::mutex g_outputMutex;

struct ActiveSearchState {
    std::mutex mutex;
    bool running = false;
    std::string jobId;
    std::shared_ptr<std::atomic<bool>> cancelToken;
    std::shared_ptr<std::atomic<int>> activeWorkerCap;
    BatchCpu::CompiledSearchCpuPlan cpuPlan{};
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
            g_activeSearch.activeWorkerCap.reset();
            g_activeSearch.cpuPlan = {};
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
                       const std::shared_ptr<std::atomic<int>> &activeWorkerCap,
                       const BatchCpu::CompiledSearchCpuPlan &cpuPlan,
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
    g_activeSearch.activeWorkerCap = activeWorkerCap;
    g_activeSearch.cpuPlan = cpuPlan;
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

bool RequestSearchActiveWorkers(const std::string &jobId, int activeWorkers)
{
    std::lock_guard<std::mutex> lock(g_activeSearch.mutex);
    if (!g_activeSearch.running || !g_activeSearch.activeWorkerCap) {
        return false;
    }
    if (!jobId.empty() && g_activeSearch.jobId != jobId) {
        return false;
    }
    const int activePhysicalCoreCap = Batch::ResolveActivePhysicalCoreCapFromWorkerLimit(
        g_activeSearch.cpuPlan,
        activeWorkers);
    g_activeSearch.activeWorkerCap->store(activePhysicalCoreCap, std::memory_order_relaxed);
    return true;
}

void ShutdownSearchWorker()
{
    std::thread worker;
    {
        std::lock_guard<std::mutex> lock(g_activeSearch.mutex);
        if (g_activeSearch.worker.joinable()) {
            worker = std::move(g_activeSearch.worker);
        }
        g_activeSearch.running = false;
        g_activeSearch.jobId.clear();
        g_activeSearch.cancelToken.reset();
        g_activeSearch.activeWorkerCap.reset();
        g_activeSearch.cpuPlan = {};
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
    const auto &worldPrefixes = SearchAnalysis::GetWorldPrefixes();
    if (worldType < 0 || static_cast<int>(worldPrefixes.size()) <= worldType) {
        return false;
    }
    *code = worldPrefixes[static_cast<size_t>(worldType)];
    *code += std::to_string(seed);
    *code += "-0-D3-";
    *code += SettingsCache::BinaryToBase36(mixing);
    return true;
}

DesktopSearchRuntimeMode ResolveDesktopSearchRuntimeMode()
{
    const char *raw = std::getenv("ONI_DESKTOP_SEARCH_RUNTIME");
    if (raw != nullptr) {
        if (const auto parsed = ParseDesktopSearchRuntimeMode(raw); parsed.has_value()) {
            return parsed.value();
        }
    }
    return DesktopSearchRuntimeMode::Optimized;
}

void PrepareSearchRuntime(AppRuntime *runtime,
                          DesktopSearchRuntimeMode mode,
                          const Batch::FilterConfig &cfg)
{
    runtime->SetResultSink(&g_batchSink);
    runtime->SetSkipPolygons(true);
    g_batchSink.SetActive(true);
    if (mode == DesktopSearchRuntimeMode::Optimized) {
        std::string code;
        if (!BuildWorldCode(cfg.worldType, cfg.seedStart, cfg.mixing, &code)) {
            throw std::runtime_error("invalid worldType");
        }
        if (!runtime->PrepareSearchWorker(code)) {
            throw std::runtime_error("prepare search worker failed");
        }
        return;
    }
    runtime->Initialize(0);
}

Batch::SearchSeedEvaluation EvaluateSearchSeed(const Batch::FilterConfig &cfg,
                                               int seed,
                                               DesktopSearchRuntimeMode mode)
{
    Batch::SearchSeedEvaluation evaluation;

    auto *runtime = AppRuntime::Instance();
    runtime->SetResultSink(&g_batchSink);
    runtime->SetSkipPolygons(true);
    g_batchSink.SetActive(true);
    g_batchSink.Reset();

    std::string code;
    if (!BuildWorldCode(cfg.worldType, seed, cfg.mixing, &code)) {
        evaluation.ok = false;
        evaluation.errorMessage = "invalid worldType";
        return evaluation;
    }

    if (mode == DesktopSearchRuntimeMode::Optimized) {
        if (!runtime->ResetSearchSeed(code)) {
            evaluation.ok = false;
            evaluation.errorMessage = "reset search seed failed";
            return evaluation;
        }
        evaluation.generated = runtime->GeneratePrepared(0);
    } else {
        runtime->Initialize(0);
        evaluation.generated = runtime->Generate(code, 0);
    }
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
}

bool ReadSettingsBlob(std::vector<char> &data, std::string *errorMessage)
{
    std::ifstream file(SETTING_ASSET_FILEPATH, std::ios::binary);
    if (!file.is_open()) {
        if (errorMessage != nullptr) {
            *errorMessage = "failed to open settings asset blob";
        }
        return false;
    }
    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    if (size <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = "settings asset blob is empty";
        }
        return false;
    }
    file.seekg(0, std::ios::beg);
    data.resize(static_cast<size_t>(size));
    if (!file.read(data.data(), size)) {
        if (errorMessage != nullptr) {
            *errorMessage = "failed to read settings asset blob";
        }
        return false;
    }
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
                                        const Batch::CompiledSearchCpuRuntime &runtime,
                                        std::atomic<bool> *cancelRequested,
                                        std::atomic<int> *activeWorkerCap)
{
    const auto runtimeMode = ResolveDesktopSearchRuntimeMode();
    Batch::SearchRequest request;
    request.seedStart = cfg.seedStart;
    request.seedEnd = cfg.seedEnd;
    const auto totalSeeds = std::max(1, cfg.seedEnd - cfg.seedStart + 1);
    const auto smallRangeProgressInterval = std::max(1, totalSeeds / 20);
    request.cpuPlan = runtime.cpuPlan;
    request.cpuGovernorConfig = runtime.cpuGovernorConfig;
    request.progressInterval = std::max(1, std::min(request.progressInterval, smallRangeProgressInterval));
    request.cancelRequested = cancelRequested;
    request.activeWorkerCap = activeWorkerCap;

    request.initializeWorker = [cfg, runtimeMode]() {
        auto *runtime = AppRuntime::Instance();
        PrepareSearchRuntime(runtime, runtimeMode, cfg);
    };
    request.evaluateSeed = [&cfg, runtimeMode](int seed) {
        return EvaluateSearchSeed(cfg, seed, runtimeMode);
    };

    return request;
}

Batch::CompiledSearchCpuRuntime CompileSearchRuntime(const Batch::FilterConfig &cfg)
{
    const auto topology = Batch::DetectCpuTopologyFacts();
    return Batch::CompileSearchCpuRuntime(cfg, topology);
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

    const auto runtimePlan = CompileSearchRuntime(cfg);
    auto cancelRequested = std::make_shared<std::atomic<bool>>(false);
    auto activeWorkerCap = std::make_shared<std::atomic<int>>(0);

    std::thread worker([cfg, request, runtimePlan, cancelRequested, activeWorkerCap]() {
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

            auto searchRequest = BuildSearchRequest(cfg,
                                                    runtimePlan,
                                                    cancelRequested.get(),
                                                    activeWorkerCap.get());
            (void)Batch::BatchSearchService::Run(searchRequest, callbacks);
        } catch (const std::exception &ex) {
            EmitLine(Batch::SerializeFailedEvent(request.jobId, ex.what()));
        } catch (...) {
            EmitLine(Batch::SerializeFailedEvent(request.jobId, "search thread crashed"));
        }
        MarkSearchFinished(request.jobId);
    });

    std::string startError;
    if (!StartSearchWorker(request.jobId,
                           cancelRequested,
                           activeWorkerCap,
                           runtimePlan.cpuPlan,
                           std::move(worker),
                           &startError)) {
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

void RunGetSearchCatalogCommand(const Batch::SidecarGetSearchCatalogRequest &request)
{
    std::string errorMessage;
    const auto settings = SharedSettingsCache::GetOrCreate(ReadSettingsBlob, &errorMessage);
    if (!settings) {
        const std::string message =
            errorMessage.empty() ? "failed to load shared settings cache" : errorMessage;
        EmitLine(Batch::SerializeFailedEvent(request.jobId, message));
        return;
    }

    const auto catalog = SearchAnalysis::BuildSearchCatalog(*settings);
    EmitLine(Batch::SerializeSearchCatalogEvent(request.jobId, catalog));
}

SearchAnalysis::SearchAnalysisRequest BuildAnalysisRequest(
    const Batch::SidecarAnalyzeSearchRequest &request)
{
    SearchAnalysis::SearchAnalysisRequest analysisRequest;
    analysisRequest.jobId = request.jobId;
    analysisRequest.worldType = request.worldType;
    analysisRequest.seedStart = request.seedStart;
    analysisRequest.seedEnd = request.seedEnd;
    analysisRequest.mixing = request.mixing;
    analysisRequest.cpu.hasValue = request.cpu.hasValue;
    analysisRequest.cpu.mode = request.cpu.mode;
    analysisRequest.cpu.allowSmt = request.cpu.allowSmt;
    analysisRequest.cpu.allowLowPerf = request.cpu.allowLowPerf;
    analysisRequest.cpu.placement = request.cpu.placement;

    analysisRequest.constraints.required = request.constraints.required;
    analysisRequest.constraints.forbidden = request.constraints.forbidden;
    analysisRequest.constraints.distance.reserve(request.constraints.distance.size());
    for (const auto &item : request.constraints.distance) {
        analysisRequest.constraints.distance.push_back(SearchAnalysis::DistanceConstraint{
            .geyserId = item.geyserId,
            .minDist = item.minDist,
            .maxDist = item.maxDist,
        });
    }
    analysisRequest.constraints.count.reserve(request.constraints.count.size());
    for (const auto &item : request.constraints.count) {
        analysisRequest.constraints.count.push_back(SearchAnalysis::CountConstraint{
            .geyserId = item.geyserId,
            .minCount = item.minCount,
            .maxCount = item.maxCount,
        });
    }
    return analysisRequest;
}

void RunAnalyzeSearchCommand(const Batch::SidecarAnalyzeSearchRequest &request)
{
    std::string errorMessage;
    const auto settings = SharedSettingsCache::GetOrCreate(ReadSettingsBlob, &errorMessage);
    if (!settings) {
        const std::string message =
            errorMessage.empty() ? "failed to load shared settings cache" : errorMessage;
        EmitLine(Batch::SerializeFailedEvent(request.jobId, message));
        return;
    }
    const auto catalog = SearchAnalysis::BuildSearchCatalog(*settings);
    const auto analysisRequest = BuildAnalysisRequest(request);
    const bool hasConstraints = !request.constraints.required.empty() ||
                                !request.constraints.forbidden.empty() ||
                                !request.constraints.distance.empty() ||
                                !request.constraints.count.empty();
    const auto profile = SearchAnalysis::CompileWorldEnvelopeProfile(*settings,
                                                                     request.worldType,
                                                                     request.mixing,
                                                                     SearchAnalysis::WorldEnvelopeCompileOptions{
                                                                         .includeSpatialEnvelopes =
                                                                             hasConstraints,
                                                                     });
    const auto result = SearchAnalysis::RunSearchAnalysis(analysisRequest, catalog, &profile);
    EmitLine(Batch::SerializeSearchAnalysisEvent(request.jobId, result));
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
        case Batch::SidecarCommandType::SetSearchActiveWorkers:
            if (!RequestSearchActiveWorkers(parsed.request.setSearchActiveWorkers.jobId,
                                            parsed.request.setSearchActiveWorkers.activeWorkers)) {
                EmitLine(Batch::SerializeFailedEvent(parsed.request.setSearchActiveWorkers.jobId,
                                                     "job is not running"));
            }
            break;
        case Batch::SidecarCommandType::GetSearchCatalog:
            RunGetSearchCatalogCommand(parsed.request.getSearchCatalog);
            break;
        case Batch::SidecarCommandType::AnalyzeSearchRequest:
            RunAnalyzeSearchCommand(parsed.request.analyze);
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
