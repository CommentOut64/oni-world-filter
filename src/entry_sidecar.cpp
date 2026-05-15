#ifndef __EMSCRIPTEN__

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
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
#include "Geyser/GeyserParameterCalculator.hpp"
#include "Geyser/PreviewWorldOffsetPolicy.hpp"
#include "SearchAnalysis/HardValidator.hpp"
#include "SearchAnalysis/SearchCatalog.hpp"
#include "SearchAnalysis/WorldEnvelopeProfile.hpp"
#include "App/SettingsAsset.hpp"
#include "Setting/NativeCoordinate.hpp"
#include "Setting/SettingsCache.hpp"
#include "config.h"

namespace {

static thread_local BatchCaptureSink g_batchSink;
static std::mutex g_outputMutex;
static std::mutex g_previewSessionMutex;

std::string DescribeSettingsAsset()
{
    const auto assetPath = ResolveSettingsAssetPath();
    std::error_code error;
    const bool exists = std::filesystem::exists(assetPath, error);
    std::ostringstream stream;
    stream << assetPath.string() << " exists=" << (exists ? "true" : "false");
    if (exists) {
        stream << " size=" << std::filesystem::file_size(assetPath, error);
    }
    if (error) {
        stream << " error=" << error.message();
    }
    return stream.str();
}

void EmitDiagnostic(const std::string &message)
{
    std::lock_guard<std::mutex> lock(g_outputMutex);
    std::cerr << "[sidecar-diagnostic] " << message << '\n';
    std::cerr.flush();
}

const char *DescribeCommand(Batch::SidecarCommandType command)
{
    switch (command) {
    case Batch::SidecarCommandType::Search:
        return "search";
    case Batch::SidecarCommandType::Preview:
        return "preview";
    case Batch::SidecarCommandType::PreviewGeyserDetails:
        return "preview_geyser_details";
    case Batch::SidecarCommandType::PreviewCoord:
        return "preview_coord";
    case Batch::SidecarCommandType::WorldReport:
        return "world_report";
    case Batch::SidecarCommandType::Cancel:
        return "cancel";
    case Batch::SidecarCommandType::SetSearchActiveWorkers:
        return "set_search_active_workers";
    case Batch::SidecarCommandType::GetSearchCatalog:
        return "get_search_catalog";
    case Batch::SidecarCommandType::AnalyzeSearchRequest:
        return "analyze_search_request";
    default:
        return "unknown";
    }
}

const char *DescribePreviewTarget(Batch::PreviewTarget target)
{
    switch (target) {
    case Batch::PreviewTarget::Primary:
        return "primary";
    case Batch::PreviewTarget::Secondary:
        return "secondary";
    default:
        return "unknown";
    }
}

bool IsPrimaryTarget(Batch::PreviewTarget target)
{
    return target == Batch::PreviewTarget::Primary;
}

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

struct PreviewWorldSession {
    int worldType{};
    int seed{};
    int mixing{};
    std::string coord;
    int primaryPlacementIndex{-1};
    std::optional<int> secondaryPlacementIndex;
    std::optional<GeneratedWorldPreview> primaryPreview;
    std::optional<GeneratedWorldPreview> secondaryPreview;
    std::optional<std::vector<GeyserDetail>> primaryDetails;
    std::optional<std::vector<GeyserDetail>> secondaryDetails;
};

static std::optional<PreviewWorldSession> g_previewSession;

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
    std::shared_ptr<std::atomic<bool>> cancelToken;
    {
        std::lock_guard<std::mutex> lock(g_activeSearch.mutex);
        cancelToken = g_activeSearch.cancelToken;
        if (cancelToken) {
            // stdin EOF 表示宿主已离线，必须先请求取消，再等待搜索线程退出。
            cancelToken->store(true, std::memory_order_relaxed);
        }
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
    char *raw = nullptr;
    size_t rawLength = 0;
    const errno_t envResult = _dupenv_s(&raw, &rawLength, "ONI_DESKTOP_SEARCH_RUNTIME");
    if (envResult == 0 && raw != nullptr) {
        if (const auto parsed = ParseDesktopSearchRuntimeMode(raw); parsed.has_value()) {
            free(raw);
            return parsed.value();
        }
        free(raw);
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

    evaluation.coord = code;
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
    const auto assetPath = ResolveSettingsAssetPath();
    EmitDiagnostic("ReadSettingsBlob path=" + DescribeSettingsAsset());
    std::ifstream file(assetPath, std::ios::binary);
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

bool LoadSettingsForCode(const std::string &code,
                         SettingsCache *settings,
                         std::string *errorMessage)
{
    if (settings == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "settings output is null";
        }
        return false;
    }

    std::string sharedError;
    const auto shared = SharedSettingsCache::GetOrCreate(ReadSettingsBlob, &sharedError);
    if (shared == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = sharedError.empty() ? "failed to load shared settings cache"
                                                : sharedError;
        }
        return false;
    }

    *settings = *shared;
    if (!settings->CoordinateChanged(code, *settings)) {
        if (errorMessage != nullptr) {
            *errorMessage = "parse world code failed";
        }
        return false;
    }
    return true;
}

bool BuildWorldList(SettingsCache &settings,
                    std::vector<World *> &worlds,
                    std::string *errorMessage)
{
    worlds.clear();
    if (settings.cluster == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "cluster is null";
        }
        return false;
    }
    for (auto &worldPlacement : settings.cluster->worldPlacements) {
        auto itr = settings.worlds.find(worldPlacement.world);
        if (itr == settings.worlds.end()) {
            if (errorMessage != nullptr) {
                *errorMessage = "world placement target is missing";
            }
            return false;
        }
        itr->second.locationType = worldPlacement.locationType;
        worlds.push_back(&itr->second);
    }
    if (worlds.size() == 1) {
        worlds[0]->locationType = LocationType::StartWorld;
    }
    return true;
}

struct PreviewPlacementSelection {
    int primaryPlacementIndex{-1};
    std::optional<int> secondaryPlacementIndex;
};

bool ResolvePreviewPlacementSelection(const SettingsCache &settings,
                                      PreviewPlacementSelection *selection,
                                      std::string *errorMessage);

struct GeyserSeedContext {
    int geyserSeed{};
    int worldOffsetX{};
    int worldOffsetY{};
};

bool ResolvePreviewWorldOffset(const SettingsCache &settings,
                               const PreviewPlacementSelection &selection,
                               Batch::PreviewTarget target,
                               GeyserSeedContext *context,
                               std::string *errorMessage)
{
    if (context == nullptr || settings.cluster == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "cluster is null before resolving preview world offset";
        }
        return false;
    }

    (void)selection;
    context->worldOffsetX = 0;
    context->worldOffsetY = 0;

    if (IsPrimaryTarget(target)) {
        PreviewWorldOffsetPolicy::PreviewWorldOffset offset;
        if (!PreviewWorldOffsetPolicy::ResolveLegacyPrimaryWorldOffset(settings, &offset)) {
            if (errorMessage != nullptr) {
                *errorMessage = "legacy primary worldOffset resolution failed";
            }
            return false;
        }
        context->worldOffsetX = offset.x;
        context->worldOffsetY = offset.y;
        return true;
    }

    if (settings.IsSpaceOutEnabled()) {
        if (errorMessage != nullptr) {
            *errorMessage = "authoritative worldOffset is unavailable for current target";
        }
        return false;
    }
    if (errorMessage != nullptr) {
        *errorMessage = "secondary world offset is not mapped for current cluster";
    }
    return false;
}

bool ResolveGeyserSeedContext(int worldType,
                              int seed,
                              int mixing,
                              Batch::PreviewTarget target,
                              GeyserSeedContext *context,
                              std::string *errorMessage)
{
    if (context == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "geyser context output is null";
        }
        return false;
    }

    std::string code;
    if (!BuildWorldCode(worldType, seed, mixing, &code)) {
        if (errorMessage != nullptr) {
            *errorMessage = "invalid worldType";
        }
        return false;
    }

    SettingsCache settings;
    if (!LoadSettingsForCode(code, &settings, errorMessage)) {
        return false;
    }

    PreviewPlacementSelection selection;
    if (!ResolvePreviewPlacementSelection(settings, &selection, errorMessage)) {
        return false;
    }
    if (!IsPrimaryTarget(target) && !selection.secondaryPlacementIndex.has_value()) {
        if (errorMessage != nullptr) {
            *errorMessage = "secondary preview is not available for current seed";
        }
        return false;
    }

    std::vector<World *> worlds;
    if (!BuildWorldList(settings, worlds, errorMessage)) {
        return false;
    }
    settings.DoSubworldMixing(worlds);
    if (settings.cluster == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "cluster is null after mixing";
        }
        return false;
    }

    context->geyserSeed = settings.seed + static_cast<int>(settings.cluster->worldPlacements.size()) - 1;
    return ResolvePreviewWorldOffset(settings, selection, target, context, errorMessage);
}

struct PreviewWorldContext {
    GeneratedWorldSummary summary;
    int geyserSeed{};
    int worldOffsetX{};
    int worldOffsetY{};
};

class PreviewCaptureSink final : public ResultSink
{
public:
    bool RequestResource(uint32_t expectedSize, std::vector<char> &data) override
    {
        data.assign(expectedSize, 0);
        EmitDiagnostic("PreviewCaptureSink::RequestResource expectedSize=" +
                       std::to_string(expectedSize) + " path=" + DescribeSettingsAsset());
        std::ifstream file(ResolveSettingsAssetPath(), std::ios::binary);
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
        m_summaries.push_back(summary);
    }

    void OnGeneratedWorldPreview(const GeneratedWorldPreview &preview) override
    {
        m_previews.push_back(preview);
    }

    const GeneratedWorldPreview *PrimaryPreview() const
    {
        return FindPrimaryGeneratedWorldPreview(m_previews);
    }

    const GeneratedWorldPreview *FindPreviewByTarget(Batch::PreviewTarget target) const
    {
        return FindGeneratedWorldPreviewByPrimaryFlag(m_previews, IsPrimaryTarget(target));
    }

    const GeneratedWorldSummary *FindSummaryByTarget(Batch::PreviewTarget target) const
    {
        for (const auto &summary : m_summaries) {
            if (summary.isPrimary == IsPrimaryTarget(target)) {
                return &summary;
            }
        }
        return nullptr;
    }

private:
    std::vector<GeneratedWorldSummary> m_summaries;
    std::vector<GeneratedWorldPreview> m_previews;
};

bool PreviewSessionMatches(const PreviewWorldSession &session,
                           int worldType,
                           int seed,
                           int mixing)
{
    return session.worldType == worldType &&
           session.seed == seed &&
           session.mixing == mixing;
}

const GeneratedWorldPreview *FindSessionPreview(const PreviewWorldSession &session,
                                                Batch::PreviewTarget target)
{
    const auto &preview = IsPrimaryTarget(target) ? session.primaryPreview : session.secondaryPreview;
    return preview ? &preview.value() : nullptr;
}

const std::optional<std::vector<GeyserDetail>> *FindSessionDetails(
    const PreviewWorldSession &session,
    Batch::PreviewTarget target)
{
    return IsPrimaryTarget(target) ? &session.primaryDetails : &session.secondaryDetails;
}

std::optional<std::vector<GeyserDetail>> *FindSessionDetails(PreviewWorldSession *session,
                                                             Batch::PreviewTarget target)
{
    if (session == nullptr) {
        return nullptr;
    }
    return IsPrimaryTarget(target) ? &session->primaryDetails : &session->secondaryDetails;
}

bool GeneratePreviewSession(const Batch::SidecarPreviewRequest &request,
                            PreviewWorldSession *session,
                            std::string *errorMessage)
{
    if (session == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "preview session output is null";
        }
        return false;
    }

    PreviewCaptureSink previewSink;
    auto *runtime = AppRuntime::Instance();
    runtime->SetResultSink(&previewSink);
    runtime->SetSkipPolygons(false);
    runtime->Initialize(0);

    std::string code;
    if (!BuildWorldCode(request.worldType, request.seed, request.mixing, &code)) {
        if (errorMessage != nullptr) {
            *errorMessage = "invalid worldType";
        }
        return false;
    }

    SettingsCache settings;
    if (!LoadSettingsForCode(code, &settings, errorMessage)) {
        return false;
    }
    PreviewPlacementSelection selection;
    if (!ResolvePreviewPlacementSelection(settings, &selection, errorMessage)) {
        return false;
    }

    std::vector<int> placementIndexes = {selection.primaryPlacementIndex};
    if (selection.secondaryPlacementIndex.has_value()) {
        placementIndexes.push_back(selection.secondaryPlacementIndex.value());
    }

    if (!runtime->GenerateSelectedPlacements(code,
                                             0,
                                             placementIndexes,
                                             selection.primaryPlacementIndex)) {
        if (errorMessage != nullptr) {
            *errorMessage = "preview generate failed";
        }
        return false;
    }

    const GeneratedWorldPreview *primaryPreview =
        previewSink.FindPreviewByTarget(Batch::PreviewTarget::Primary);
    if (primaryPreview == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "preview payload is empty";
        }
        return false;
    }

    session->worldType = request.worldType;
    session->seed = request.seed;
    session->mixing = request.mixing;
    session->primaryPlacementIndex = selection.primaryPlacementIndex;
    session->secondaryPlacementIndex = selection.secondaryPlacementIndex;
    session->coord = std::move(code);
    session->primaryPreview = *primaryPreview;
    session->secondaryPreview.reset();
    session->primaryDetails.reset();
    session->secondaryDetails.reset();

    if (const auto *secondaryPreview = previewSink.FindPreviewByTarget(Batch::PreviewTarget::Secondary)) {
        session->secondaryPreview = *secondaryPreview;
    }
    if (selection.secondaryPlacementIndex.has_value() && !session->secondaryPreview.has_value()) {
        if (errorMessage != nullptr) {
            *errorMessage = "secondary preview payload is missing";
        }
        return false;
    }
    const bool hasSecondaryPreview = session->secondaryPreview.has_value();
    session->primaryPreview->summary.hasSecondaryPreview = hasSecondaryPreview;
    if (session->secondaryPreview.has_value()) {
        session->secondaryPreview->summary.hasSecondaryPreview = true;
    }
    return true;
}

bool ResolvePreviewPlacementSelection(const SettingsCache &settings,
                                      PreviewPlacementSelection *selection,
                                      std::string *errorMessage)
{
    if (selection == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "preview placement selection output is null";
        }
        return false;
    }
    if (settings.cluster == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "cluster is null before resolving preview placements";
        }
        return false;
    }
    if (settings.cluster->worldPlacements.empty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "cluster has no world placements";
        }
        return false;
    }

    int primaryPlacementIndex = -1;
    for (size_t index = 0; index < settings.cluster->worldPlacements.size(); ++index) {
        if (settings.cluster->worldPlacements[index].locationType == LocationType::StartWorld) {
            primaryPlacementIndex = static_cast<int>(index);
            break;
        }
    }
    if (primaryPlacementIndex < 0) {
        const int startWorldIndex = settings.cluster->startWorldIndex;
        if (startWorldIndex < 0 ||
            startWorldIndex >= static_cast<int>(settings.cluster->worldPlacements.size())) {
            if (errorMessage != nullptr) {
                *errorMessage = "cluster startWorldIndex is out of range";
            }
            return false;
        }
        primaryPlacementIndex = startWorldIndex;
    }

    std::vector<int> warpPlacementCandidates;
    for (size_t index = 0; index < settings.cluster->worldPlacements.size(); ++index) {
        if (static_cast<int>(index) == primaryPlacementIndex) {
            continue;
        }
        const auto &placement = settings.cluster->worldPlacements[index];
        const auto itr = settings.worlds.find(placement.world);
        if (itr == settings.worlds.end()) {
            if (errorMessage != nullptr) {
                *errorMessage = "world placement target is missing while resolving preview placements";
            }
            return false;
        }
        if (itr->second.startingBaseTemplate.contains("::bases/warpworld")) {
            warpPlacementCandidates.push_back(static_cast<int>(index));
        }
    }

    selection->primaryPlacementIndex = primaryPlacementIndex;
    selection->secondaryPlacementIndex.reset();
    if (warpPlacementCandidates.size() == 1) {
        selection->secondaryPlacementIndex = warpPlacementCandidates.front();
    }
    return true;
}

bool ResolvePreviewWorldContext(const PreviewWorldSession &session,
                                Batch::PreviewTarget target,
                                PreviewWorldContext *context,
                                std::string *errorMessage)
{
    if (context == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "preview world context output is null";
        }
        return false;
    }

    const GeneratedWorldPreview *preview = FindSessionPreview(session, target);
    if (preview == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "secondary preview is not available for current seed";
        }
        return false;
    }

    GeyserSeedContext geyserContext;
    if (!ResolveGeyserSeedContext(session.worldType,
                                  session.seed,
                                  session.mixing,
                                  target,
                                  &geyserContext,
                                  errorMessage)) {
        return false;
    }

    context->summary = preview->summary;
    context->geyserSeed = geyserContext.geyserSeed;
    context->worldOffsetX = geyserContext.worldOffsetX;
    context->worldOffsetY = geyserContext.worldOffsetY;
    return true;
}

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
    request.evaluateSeed = [cfg, runtimeMode](int seed) {
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
    EmitDiagnostic("RunSearchCommand jobId=" + request.jobId +
                   " worldType=" + std::to_string(request.worldType) +
                   " seedStart=" + std::to_string(request.seedStart) +
                   " seedEnd=" + std::to_string(request.seedEnd) +
                   " mixing=" + std::to_string(request.mixing));
    std::string errorMessage;
    const auto settings = SharedSettingsCache::GetOrCreate(ReadSettingsBlob, &errorMessage);
    if (!settings) {
        const std::string message =
            errorMessage.empty() ? "failed to load shared settings cache" : errorMessage;
        EmitLine(Batch::SerializeFailedEvent(request.jobId, message));
        return;
    }
    std::vector<Batch::FilterError> errors;
    auto cfg = Batch::BuildFilterConfigFromSidecarSearch(request, &errors, settings.get());
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
            EmitDiagnostic("search worker started jobId=" + request.jobId);
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
            EmitDiagnostic("search worker exception jobId=" + request.jobId + " message=" + ex.what());
            EmitLine(Batch::SerializeFailedEvent(request.jobId, ex.what()));
        } catch (...) {
            EmitDiagnostic("search worker unknown exception jobId=" + request.jobId);
            EmitLine(Batch::SerializeFailedEvent(request.jobId, "search thread crashed"));
        }
        EmitDiagnostic("search worker finished jobId=" + request.jobId);
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
    EmitDiagnostic("RunPreviewCommand jobId=" + request.jobId +
                   " worldType=" + std::to_string(request.worldType) +
                   " seed=" + std::to_string(request.seed) +
                   " mixing=" + std::to_string(request.mixing) +
                   " target=" + DescribePreviewTarget(request.target));

    {
        std::lock_guard<std::mutex> lock(g_previewSessionMutex);
        if (g_previewSession &&
            PreviewSessionMatches(*g_previewSession, request.worldType, request.seed, request.mixing)) {
            if (const auto *cachedPreview = FindSessionPreview(*g_previewSession, request.target)) {
                EmitLine(Batch::SerializePreviewEvent(request.jobId, request, *cachedPreview));
                return;
            }
            EmitLine(Batch::SerializeFailedEvent(request.jobId,
                                                 "secondary preview is not available for current seed"));
            return;
        }
    }

    PreviewWorldSession session;
    std::string errorMessage;
    if (!GeneratePreviewSession(request, &session, &errorMessage)) {
        EmitLine(Batch::SerializeFailedEvent(request.jobId,
                                             errorMessage.empty() ? "preview generate failed"
                                                                  : errorMessage));
        return;
    }

    const GeneratedWorldPreview *targetPreview = FindSessionPreview(session, request.target);
    if (targetPreview == nullptr) {
        EmitLine(Batch::SerializeFailedEvent(request.jobId,
                                             "secondary preview is not available for current seed"));
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_previewSessionMutex);
        g_previewSession = session;
    }
    EmitLine(Batch::SerializePreviewEvent(request.jobId, request, *targetPreview));
}

void RunPreviewCoordCommand(const Batch::SidecarPreviewCoordRequest &request)
{
    EmitDiagnostic("RunPreviewCoordCommand jobId=" + request.jobId + " coord=" + request.coord);
    NativeCoordinate::NativeCoordinateResolution resolved;
    if (!NativeCoordinate::ResolveNativeCoordinate(request.coord, &resolved)) {
        EmitLine(Batch::SerializeFailedEvent(
            request.jobId,
            "invalid native coord; trailing mixing code must be 0 or 5-char base36"));
        return;
    }

    Batch::SidecarPreviewRequest previewRequest;
    previewRequest.worldType = resolved.worldType;
    previewRequest.seed = resolved.seed;
    previewRequest.mixing = resolved.mixing;
    previewRequest.target = Batch::PreviewTarget::Primary;

    PreviewWorldSession session;
    std::string errorMessage;
    if (!GeneratePreviewSession(previewRequest, &session, &errorMessage)) {
        EmitLine(Batch::SerializeFailedEvent(request.jobId,
                                             errorMessage.empty() ? "preview generate failed"
                                                                  : errorMessage));
        return;
    }
    if (!session.primaryPreview.has_value()) {
        EmitLine(Batch::SerializeFailedEvent(request.jobId, "preview payload is empty"));
        return;
    }
    GeneratedWorldPreview responsePreview = *session.primaryPreview;
    responsePreview.summary.hasSecondaryPreview = session.secondaryPreview.has_value();
    {
        std::lock_guard<std::mutex> lock(g_previewSessionMutex);
        g_previewSession = session;
    }

    Batch::SidecarPreviewRequest resolvedRequest;
    resolvedRequest.jobId = request.jobId;
    resolvedRequest.worldType = resolved.worldType;
    resolvedRequest.seed = resolved.seed;
    resolvedRequest.mixing = resolved.mixing;
    EmitLine(Batch::SerializePreviewEvent(request.jobId,
                                          resolvedRequest,
                                          responsePreview,
                                          &resolved.code));
}

void RunPreviewGeyserDetailsCommand(const Batch::SidecarPreviewGeyserDetailsRequest &request)
{
    EmitDiagnostic("RunPreviewGeyserDetailsCommand jobId=" + request.jobId +
                   " worldType=" + std::to_string(request.worldType) +
                   " seed=" + std::to_string(request.seed) +
                   " mixing=" + std::to_string(request.mixing) +
                   " target=" + DescribePreviewTarget(request.target));

    PreviewWorldSession session;
    bool hasMatchingSession = false;
    {
        std::lock_guard<std::mutex> lock(g_previewSessionMutex);
        if (g_previewSession &&
            PreviewSessionMatches(*g_previewSession, request.worldType, request.seed, request.mixing)) {
            session = *g_previewSession;
            hasMatchingSession = true;
        }
    }

    if (!hasMatchingSession) {
        Batch::SidecarPreviewRequest previewRequest;
        previewRequest.worldType = request.worldType;
        previewRequest.seed = request.seed;
        previewRequest.mixing = request.mixing;
        previewRequest.target = request.target;

        std::string errorMessage;
        if (!GeneratePreviewSession(previewRequest, &session, &errorMessage)) {
            EmitLine(Batch::SerializeFailedEvent(request.jobId,
                                                 errorMessage.empty() ? "preview generate failed"
                                                                      : errorMessage));
            return;
        }

        {
            std::lock_guard<std::mutex> lock(g_previewSessionMutex);
            g_previewSession = session;
        }
    }

    if (const auto *cachedDetails = FindSessionDetails(session, request.target);
        cachedDetails != nullptr && cachedDetails->has_value()) {
        EmitLine(Batch::SerializePreviewGeyserDetailsEvent(request.jobId,
                                                           request,
                                                           cachedDetails->value()));
        return;
    }

    PreviewWorldContext context;
    std::string errorMessage;
    if (!ResolvePreviewWorldContext(session, request.target, &context, &errorMessage)) {
        EmitLine(Batch::SerializeFailedEvent(request.jobId,
                                             errorMessage.empty() ? "resolve preview world context failed"
                                                                  : errorMessage));
        return;
    }

    const auto details =
        GeyserCalc::BuildGeyserDetails(context.geyserSeed,
                                       context.summary.worldSize.y,
                                       context.summary.geysers,
                                       context.worldOffsetX,
                                       context.worldOffsetY);

    {
        std::lock_guard<std::mutex> lock(g_previewSessionMutex);
        if (g_previewSession &&
            PreviewSessionMatches(*g_previewSession, request.worldType, request.seed, request.mixing)) {
            if (auto *detailSlot = FindSessionDetails(&*g_previewSession, request.target);
                detailSlot != nullptr) {
                *detailSlot = details;
            }
        }
    }
    EmitLine(Batch::SerializePreviewGeyserDetailsEvent(request.jobId, request, details));
}

void RunGetWorldReportCommand(const Batch::SidecarWorldReportRequest &request)
{
    EmitDiagnostic("RunGetWorldReportCommand jobId=" + request.jobId +
                   " worldType=" + std::to_string(request.worldType) +
                   " seed=" + std::to_string(request.seed) +
                   " mixing=" + std::to_string(request.mixing));
    Batch::SidecarPreviewRequest previewRequest;
    previewRequest.worldType = request.worldType;
    previewRequest.seed = request.seed;
    previewRequest.mixing = request.mixing;
    previewRequest.target = Batch::PreviewTarget::Primary;

    PreviewWorldSession session;
    std::string errorMessage;
    if (!GeneratePreviewSession(previewRequest, &session, &errorMessage)) {
        EmitLine(Batch::SerializeFailedEvent(request.jobId,
                                             errorMessage.empty() ? "world report generate failed"
                                                                  : errorMessage));
        return;
    }
    if (!session.primaryPreview.has_value()) {
        EmitLine(Batch::SerializeFailedEvent(request.jobId, "world report payload is empty"));
        return;
    }

    std::string code = session.coord;

    GeyserSeedContext geyserContext;
    if (!ResolveGeyserSeedContext(request.worldType,
                                  request.seed,
                                  request.mixing,
                                  Batch::PreviewTarget::Primary,
                                  &geyserContext,
                                  &errorMessage)) {
        EmitLine(Batch::SerializeFailedEvent(request.jobId,
                                             errorMessage.empty() ? "resolve geyser seed failed"
                                                                  : errorMessage));
        return;
    }

    const auto report =
        GeyserCalc::BuildWorldReportData(*session.primaryPreview,
                                         geyserContext.geyserSeed,
                                         request.mixing,
                                         code,
                                         geyserContext.worldOffsetX,
                                         geyserContext.worldOffsetY);
    EmitLine(Batch::SerializeWorldReportEvent(request.jobId, request, report));
}

void RunGetSearchCatalogCommand(const Batch::SidecarGetSearchCatalogRequest &request)
{
    EmitDiagnostic("RunGetSearchCatalogCommand jobId=" + request.jobId);
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
    analysisRequest.constraints.requiredTraits = request.constraints.requiredTraits;
    analysisRequest.constraints.forbiddenTraits = request.constraints.forbiddenTraits;
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
    EmitDiagnostic("RunAnalyzeSearchCommand jobId=" + request.jobId +
                   " worldType=" + std::to_string(request.worldType) +
                   " seedStart=" + std::to_string(request.seedStart) +
                   " seedEnd=" + std::to_string(request.seedEnd) +
                   " mixing=" + std::to_string(request.mixing));
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
                                !request.constraints.requiredTraits.empty() ||
                                !request.constraints.forbiddenTraits.empty() ||
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
    EmitDiagnostic("process started settingsAsset=" + DescribeSettingsAsset());
    std::string line;
    while (std::getline(std::cin, line)) {
        JoinInactiveSearchWorker();
        if (line.empty()) {
            continue;
        }
        EmitDiagnostic("stdin line received bytes=" + std::to_string(line.size()));

        const auto parsed = Batch::ParseSidecarRequest(line);
        if (!parsed.Ok()) {
            EmitDiagnostic("parse request failed error=" + parsed.error);
            EmitLine(Batch::SerializeFailedEvent("unknown", parsed.error));
            continue;
        }
        EmitDiagnostic(std::string("dispatch command=") + DescribeCommand(parsed.request.command));

        try {
            switch (parsed.request.command) {
            case Batch::SidecarCommandType::Search:
                RunSearchCommand(parsed.request.search);
                break;
            case Batch::SidecarCommandType::Preview:
                RunPreviewCommand(parsed.request.preview);
                break;
            case Batch::SidecarCommandType::PreviewGeyserDetails:
                RunPreviewGeyserDetailsCommand(parsed.request.previewGeyserDetails);
                break;
            case Batch::SidecarCommandType::PreviewCoord:
                RunPreviewCoordCommand(parsed.request.previewCoord);
                break;
            case Batch::SidecarCommandType::WorldReport:
                RunGetWorldReportCommand(parsed.request.worldReport);
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
        } catch (const std::exception &ex) {
            EmitDiagnostic(std::string("command exception message=") + ex.what());
            EmitLine(Batch::SerializeFailedEvent("unknown", ex.what()));
        } catch (...) {
            EmitDiagnostic("command unknown exception");
            EmitLine(Batch::SerializeFailedEvent("unknown", "sidecar command crashed"));
        }
    }

    EmitDiagnostic("stdin closed; shutting down");
    ShutdownSearchWorker();
    EmitDiagnostic("process exiting normally");
    return 0;
}

#endif
