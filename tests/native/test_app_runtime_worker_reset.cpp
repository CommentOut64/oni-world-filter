#include <fstream>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <atomic>
#include <thread>
#include <utility>
#include <vector>

#include "App/AppRuntime.hpp"
#include "Batch/BatchSearchService.hpp"
#include "Batch/CpuTopology.hpp"
#include "BatchCpu/SearchCpuPlan.hpp"
#include "SearchAnalysis/SearchCatalog.hpp"
#include "config.h"

namespace {

bool Expect(bool condition, const char *message, int &failures)
{
    if (condition) {
        return true;
    }
    std::cerr << "[FAIL] " << message << std::endl;
    ++failures;
    return false;
}

std::string PreviewText(const std::string &value)
{
    constexpr size_t kPreviewLimit = 240;
    if (value.size() <= kPreviewLimit) {
        return value;
    }
    std::ostringstream builder;
    builder << value.substr(0, kPreviewLimit) << "...[" << value.size() << " chars]";
    return builder.str();
}

bool ExpectEqual(const std::string &lhs,
                 const std::string &rhs,
                 const char *message,
                 int &failures)
{
    if (lhs == rhs) {
        return true;
    }
    std::cerr << "[FAIL] " << message << std::endl;
    std::cerr << "  lhs: " << PreviewText(lhs) << std::endl;
    std::cerr << "  rhs: " << PreviewText(rhs) << std::endl;
    ++failures;
    return false;
}

std::string BuildWorldCode(int worldType, int seed, int mixing)
{
    const auto &worldPrefixes = SearchAnalysis::GetWorldPrefixes();
    if (worldType < 0 || static_cast<int>(worldPrefixes.size()) <= worldType) {
        return {};
    }
    std::ostringstream builder;
    builder << worldPrefixes[static_cast<size_t>(worldType)] << seed << "-0-D3-"
            << SettingsCache::BinaryToBase36(static_cast<uint32_t>(mixing));
    return builder.str();
}

BatchCpu::CpuTopologyFacts BuildNonSmtTopology(int physicalCoreCount)
{
    BatchCpu::CpuTopologyFacts topology;
    topology.detectionSucceeded = true;
    topology.isHeterogeneous = false;
    topology.diagnostics = "app runtime worker reset topology";
    topology.physicalCoresBySystemOrder.reserve(static_cast<size_t>(physicalCoreCount));
    for (int index = 0; index < physicalCoreCount; ++index) {
        BatchCpu::PhysicalCoreFacts core;
        core.physicalCoreIndex = static_cast<uint32_t>(index);
        core.group = 0;
        core.efficiencyClass = 0;
        core.isHighPerformance = true;
        core.logicalThreads.push_back({
            .logicalIndex = static_cast<uint32_t>(index),
            .group = 0,
            .isPrimaryThread = true,
        });
        topology.physicalCoresBySystemOrder.push_back(std::move(core));
    }
    return topology;
}

BatchCpu::CompiledSearchCpuPlan BuildBalancedPlan(int physicalCoreCount)
{
    BatchCpu::CpuPolicySpec spec;
    spec.mode = BatchCpu::CpuMode::Balanced;
    spec.allowSmt = false;
    spec.allowLowPerf = true;
    spec.binding = BatchCpu::PlacementMode::None;
    return BatchCpu::CompileSearchCpuPlan(BuildNonSmtTopology(physicalCoreCount), spec);
}

BatchCpu::CompiledSearchCpuPlan BuildActualBalancedPlan()
{
    BatchCpu::CpuPolicySpec spec;
    spec.mode = BatchCpu::CpuMode::Balanced;
    spec.allowSmt = false;
    spec.allowLowPerf = false;
    spec.binding = BatchCpu::PlacementMode::None;
    return BatchCpu::CompileSearchCpuPlan(Batch::DetectCpuTopologyFacts(), spec);
}

class RecordingSink final : public ResultSink
{
public:
    bool RequestResource(uint32_t expectedSize, std::vector<char> &data) override
    {
        data.assign(expectedSize, 0);
        std::ifstream file(SETTING_TEST_ASSET_FILEPATH, std::ios::binary);
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
        summaries.push_back(summary);
    }

    void OnGeneratedWorldPreview(const GeneratedWorldPreview &preview) override
    {
        previews.push_back(preview);
    }

    void Reset()
    {
        summaries.clear();
        previews.clear();
    }

    std::vector<GeneratedWorldSummary> summaries;
    std::vector<GeneratedWorldPreview> previews;
};

thread_local BatchCaptureSink g_batchSearchSink;

bool ReadAssetBlob(std::vector<char> &data)
{
    data.assign(SETTING_ASSET_FILESIZE, 0);
    std::ifstream file(SETTING_TEST_ASSET_FILEPATH, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    const auto size = file.seekg(0, std::ios::end).tellg();
    if (size != static_cast<std::streamoff>(SETTING_ASSET_FILESIZE)) {
        return false;
    }
    file.seekg(0).read(data.data(), SETTING_ASSET_FILESIZE);
    return file.good();
}

bool LoadSettingsFromSharedCopy(SettingsCache &settings)
{
    std::string error;
    auto shared = SharedSettingsCache::GetOrCreate(
        [](std::vector<char> &data, std::string *errorMessage) {
            if (ReadAssetBlob(data)) {
                return true;
            }
            if (errorMessage != nullptr) {
                *errorMessage = "failed to read asset blob";
            }
            return false;
        },
        &error);
    if (!error.empty() || shared == nullptr) {
        return false;
    }
    settings = *shared;
    return true;
}

bool BuildWorldList(SettingsCache &settings, std::vector<World *> &worlds)
{
    worlds.clear();
    if (settings.cluster == nullptr) {
        return false;
    }
    for (auto &worldPlacement : settings.cluster->worldPlacements) {
        auto itr = settings.worlds.find(worldPlacement.world);
        if (itr == settings.worlds.end()) {
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

std::string EncodeMinMax(const MinMax &value)
{
    std::ostringstream builder;
    builder << std::fixed << std::setprecision(3) << value.min << ',' << value.max;
    return builder.str();
}

std::string FingerprintWorldRuntime(const World &world)
{
    std::ostringstream builder;
    builder << static_cast<int>(world.locationType) << '|';
    builder << EncodeMinMax(world.startingPositionHorizontal2) << '|';
    builder << EncodeMinMax(world.startingPositionVertical2) << '|';
    builder << "features=";
    for (const auto *feature : world.globalFeatures2) {
        builder << feature->type << ',';
    }
    builder << "|subworlds=";
    for (const auto *subworld : world.subworldFiles2) {
        builder << subworld->name << ',';
    }
    builder << "|filters=";
    for (const auto *filter : world.unknownCellsAllowedSubworlds2) {
        builder << static_cast<int>(filter->command) << ':';
        builder << filter->tag << ':';
        for (const auto &subworldName : filter->subworldNames) {
            builder << subworldName << ',';
        }
        builder << ';';
    }
    builder << "|rules=";
    for (const auto *rule : world.worldTemplateRules2) {
        builder << rule->ruleId << ',';
        for (const auto &name : rule->names) {
            builder << name << ',';
        }
        builder << ';';
    }
    builder << "|mixing=";
    for (const auto &mixingSubworld : world.mixingSubworlds) {
        builder << mixingSubworld.name << ':' << mixingSubworld.minCount << ':' << mixingSubworld.maxCount
                << ',';
    }
    return builder.str();
}

std::string FingerprintTags(const std::set<std::string> &tags)
{
    std::ostringstream builder;
    for (const auto &tag : tags) {
        builder << tag << ',';
    }
    return builder.str();
}

std::string FingerprintDistances(const std::map<std::string, int> &distances)
{
    std::ostringstream builder;
    for (const auto &[tag, distance] : distances) {
        builder << tag << ':' << distance << ',';
    }
    return builder.str();
}

std::string FingerprintSite(const Site &site, bool includeChildren)
{
    std::ostringstream builder;
    builder << std::fixed << std::setprecision(3);
    const auto bounds = site.polygon.Bounds();
    builder << site.idx << '@' << site.x << ',' << site.y;
    builder << "|w=" << site.weight << "|cw=" << site.currentWeight;
    builder << "|sub=" << (site.subworld != nullptr ? site.subworld->name : "null");
    builder << "|feature=" << (site.globalFeature != nullptr ? site.globalFeature->type : "null");
    builder << "|neighbors=" << site.neighbours.size();
    builder << "|bounds=" << bounds.x << ',' << bounds.y << ',' << bounds.width << ',' << bounds.height;
    builder << "|tags=" << FingerprintTags(site.tags);
    builder << "|dist=" << FingerprintDistances(site.minDistanceToTag);
    builder << "|template=" << site.templateTag;
    if (!includeChildren) {
        return builder.str();
    }
    builder << "|children=";
    if (site.children == nullptr) {
        builder << "null";
        return builder.str();
    }
    for (const auto &child : *site.children) {
        builder << '{' << FingerprintSite(child, false) << '}';
    }
    return builder.str();
}

std::string FingerprintSites(const std::vector<Site> &sites, bool includeChildren)
{
    std::ostringstream builder;
    builder << "count=" << sites.size() << ';';
    for (const auto &site : sites) {
        builder << '[' << FingerprintSite(site, includeChildren) << ']';
    }
    return builder.str();
}

std::string FingerprintGeysers(const std::vector<Vector3i> &geysers)
{
    std::ostringstream builder;
    for (const auto &geyser : geysers) {
        builder << geyser.z << ':' << geyser.x << ':' << geyser.y << ',';
    }
    return builder.str();
}

struct WorldGenPhaseFingerprint {
    std::string runtime;
    std::string afterSeedPoints;
    std::string afterInitialDiagram;
    std::string afterDistanceTags;
    std::string afterConvertUnknownCells;
    std::string afterPostConvertDiagram;
    std::string afterGenerateChildren;
    std::string templatePlacements;
    std::string afterFullGenerate;
    std::string geysers;
};

bool PrepareStartWorldFixture(const std::string &code,
                              SettingsCache &settings,
                              World **world,
                              int *seed,
                              int *clusterSeed = nullptr,
                              int *geyserSeed = nullptr)
{
    if (world == nullptr || seed == nullptr) {
        return false;
    }
    *world = nullptr;
    *seed = 0;
    if (!LoadSettingsFromSharedCopy(settings)) {
        return false;
    }
    if (!settings.CoordinateChanged(code, settings)) {
        return false;
    }
    std::vector<World *> worlds;
    if (!BuildWorldList(settings, worlds)) {
        return false;
    }
    settings.DoSubworldMixing(worlds);
    const int baseSeed = settings.seed;
    if (clusterSeed != nullptr) {
        *clusterSeed = baseSeed;
    }
    if (geyserSeed != nullptr && settings.cluster != nullptr) {
        *geyserSeed = baseSeed + static_cast<int>(settings.cluster->worldPlacements.size()) - 1;
    }
    for (size_t i = 0; i < worlds.size(); ++i) {
        World *candidate = worlds[i];
        if (candidate->locationType != LocationType::StartWorld) {
            continue;
        }
        settings.seed = baseSeed + static_cast<int>(i);
        const auto traits = settings.GetRandomTraits(*candidate);
        for (const auto *trait : traits) {
            candidate->ApplayTraits(*trait, settings);
        }
        *world = candidate;
        *seed = settings.seed;
        return true;
    }
    return false;
}

WorldGenPhaseFingerprint RunWorldGenPhaseFingerprintOnCurrentThread(const std::string &code)
{
    WorldGenPhaseFingerprint fingerprint;

    SettingsCache debugSettings;
    World *debugWorld = nullptr;
    int debugSeed = 0;
    if (!PrepareStartWorldFixture(code, debugSettings, &debugWorld, &debugSeed)) {
        return fingerprint;
    }

    fingerprint.runtime = FingerprintWorldRuntime(*debugWorld);
    WorldGen debugWorldGen(*debugWorld, debugSettings);
    WorldGenDebugPhaseFingerprint phases;
    if (!debugWorldGen.DebugCapturePhaseFingerprint(&phases)) {
        return fingerprint;
    }
    fingerprint.afterSeedPoints = std::move(phases.afterSeedPoints);
    fingerprint.afterInitialDiagram = std::move(phases.afterInitialDiagram);
    fingerprint.afterDistanceTags = std::move(phases.afterDistanceTags);
    fingerprint.afterConvertUnknownCells = std::move(phases.afterConvertUnknownCells);
    fingerprint.afterPostConvertDiagram = std::move(phases.afterPostConvertDiagram);
    fingerprint.afterGenerateChildren = std::move(phases.afterGenerateChildren);
    fingerprint.templatePlacements = std::move(phases.templatePlacements);

    SettingsCache fullSettings;
    World *fullWorld = nullptr;
    int fullSeed = 0;
    int geyserSeed = 0;
    if (!PrepareStartWorldFixture(code, fullSettings, &fullWorld, &fullSeed, nullptr, &geyserSeed)) {
        return fingerprint;
    }
    WorldGen fullWorldGen(*fullWorld, fullSettings);
    std::vector<Site> fullSites;
    if (!fullWorldGen.GenerateOverworld(fullSites)) {
        return fingerprint;
    }
    fingerprint.afterFullGenerate = FingerprintSites(fullSites, true);
    fingerprint.geysers = FingerprintGeysers(fullWorldGen.GetGeysers(geyserSeed));
    return fingerprint;
}

WorldGenPhaseFingerprint RunWorldGenPhaseFingerprintOnFreshThread(const std::string &code)
{
    WorldGenPhaseFingerprint fingerprint;
    std::thread worker([&]() { fingerprint = RunWorldGenPhaseFingerprintOnCurrentThread(code); });
    worker.join();
    return fingerprint;
}

template<typename T, typename Fn>
std::pair<T, T> RunPairConcurrently(Fn fn)
{
    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    T first{};
    T second{};

    auto worker = [&](T *output) {
        ready.fetch_add(1, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        *output = fn();
    };

    std::thread firstThread(worker, &first);
    std::thread secondThread(worker, &second);
    while (ready.load(std::memory_order_acquire) < 2) {
        std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);
    firstThread.join();
    secondThread.join();
    return {std::move(first), std::move(second)};
}

std::string FingerprintSummary(const GeneratedWorldSummary &summary)
{
    std::ostringstream builder;
    builder << summary.seed << '|' << summary.worldType << '|';
    builder << summary.start.x << ',' << summary.start.y << '|';
    builder << summary.worldSize.x << ',' << summary.worldSize.y << '|';
    for (const auto &trait : summary.traits) {
        builder << trait.id << ',';
    }
    builder << '|';
    for (const auto &geyser : summary.geysers) {
        builder << geyser.type << ':' << geyser.x << ':' << geyser.y << ',';
    }
    return builder.str();
}

std::string FingerprintSummaries(const std::vector<GeneratedWorldSummary> &summaries)
{
    std::ostringstream builder;
    for (const auto &summary : summaries) {
        builder << FingerprintSummary(summary) << ';';
    }
    return builder.str();
}

std::string FingerprintStartWorldSummaryAsBatchCapture(
    const std::vector<GeneratedWorldSummary> &summaries)
{
    const GeneratedWorldSummary *startSummary = nullptr;
    for (const auto &summary : summaries) {
        if (summary.worldType == 0) {
            startSummary = &summary;
            break;
        }
    }
    if (startSummary == nullptr) {
        return {};
    }

    std::ostringstream builder;
    builder << startSummary->start.x << ',' << startSummary->start.y << '|';
    builder << startSummary->worldSize.x << ',' << startSummary->worldSize.y << '|';
    for (const auto &trait : startSummary->traits) {
        builder << trait.id << ',';
    }
    builder << '|';
    for (const auto &geyser : startSummary->geysers) {
        builder << geyser.type << ':' << geyser.x << ':' << geyser.y << ',';
    }
    return builder.str();
}

std::string FingerprintBatchCapture(const BatchCaptureRecord &capture)
{
    std::ostringstream builder;
    builder << capture.startX << ',' << capture.startY << '|';
    builder << capture.worldW << ',' << capture.worldH << '|';
    for (int trait : capture.traits) {
        builder << trait << ',';
    }
    builder << '|';
    for (const auto &geyser : capture.geysers) {
        builder << geyser.type << ':' << geyser.x << ':' << geyser.y << ',';
    }
    return builder.str();
}

std::string RunLegacySummaryOnly(AppRuntime *runtime,
                                 RecordingSink &sink,
                                 const std::string &code)
{
    runtime->SetSkipPolygons(true);
    runtime->Initialize(0);
    sink.Reset();
    if (!runtime->Generate(code, 0)) {
        return {};
    }
    return FingerprintSummaries(sink.summaries);
}

std::string RunPreparedSummaryOnly(AppRuntime *runtime,
                                   RecordingSink &sink,
                                   const std::string &code)
{
    sink.Reset();
    if (!runtime->ResetSearchSeed(code)) {
        return {};
    }
    if (!runtime->GeneratePrepared(0)) {
        return {};
    }
    return FingerprintSummaries(sink.summaries);
}

std::string RunLegacyBatchCaptureOnly(AppRuntime *runtime, const std::string &code)
{
    runtime->SetResultSink(&g_batchSearchSink);
    runtime->SetSkipPolygons(true);
    g_batchSearchSink.SetActive(true);
    g_batchSearchSink.Reset();
    runtime->Initialize(0);
    if (!runtime->Generate(code, 0)) {
        return {};
    }
    return FingerprintBatchCapture(g_batchSearchSink.Data());
}

std::string RunLegacyBatchCaptureOnFreshThread(const std::string &code)
{
    std::string fingerprint;
    std::thread worker([&]() {
        auto *runtime = AppRuntime::Instance();
        fingerprint = RunLegacyBatchCaptureOnly(runtime, code);
    });
    worker.join();
    return fingerprint;
}

std::string RunLegacySummaryOnFreshThread(const std::string &code)
{
    std::string fingerprint;
    std::thread worker([&]() {
        auto *runtime = AppRuntime::Instance();
        RecordingSink sink;
        runtime->SetResultSink(&sink);
        fingerprint = RunLegacySummaryOnly(runtime, sink, code);
    });
    worker.join();
    return fingerprint;
}

std::string RunLegacyBatchSearchOnce(const BatchCpu::CompiledSearchCpuPlan &plan)
{
    Batch::SearchRequest request;
    request.seedStart = 0;
    request.seedEnd = 0;
    request.cpuPlan = plan;
    request.cpuGovernorConfig.enabled = false;
    request.chunkSize = 64;
    request.progressInterval = 1;
    request.sampleWindow = std::chrono::milliseconds(10);
    request.initializeWorker = []() {
        auto *runtime = AppRuntime::Instance();
        runtime->SetResultSink(&g_batchSearchSink);
        runtime->SetSkipPolygons(true);
        g_batchSearchSink.SetActive(true);
        runtime->Initialize(0);
    };
    request.evaluateSeed = [](int seed) {
        Batch::SearchSeedEvaluation evaluation;
        auto *runtime = AppRuntime::Instance();
        runtime->SetResultSink(&g_batchSearchSink);
        runtime->SetSkipPolygons(true);
        g_batchSearchSink.SetActive(true);
        g_batchSearchSink.Reset();

        const std::string code = BuildWorldCode(13, seed, 0);
        runtime->Initialize(0);
        evaluation.generated = runtime->Generate(code, 0);
        if (evaluation.generated) {
            evaluation.matched = true;
            evaluation.capture = g_batchSearchSink.Data();
        }
        return evaluation;
    };

    std::string fingerprint;
    Batch::SearchEventCallbacks callbacks;
    callbacks.onMatch = [&](const Batch::SearchMatchEvent &event) {
        fingerprint = FingerprintBatchCapture(event.capture);
    };

    const auto result = Batch::BatchSearchService::Run(request, callbacks);
    if (result.failed || result.cancelled || result.processedSeeds != 1) {
        return {};
    }
    return fingerprint;
}

} // namespace

int RunAllTests()
{
    int failures = 0;

    {
        Polygon polygon(Rect(0.0f, 0.0f, 10.0f, 10.0f));
        (void)polygon.Centroid();
        (void)polygon.Bounds();

        Polygon clip(Rect(0.0f, 0.0f, 5.0f, 5.0f));
        polygon.Intersect(clip);

        const auto &centroid = polygon.Centroid();
        const auto &bounds = polygon.Bounds();
        const bool centroidMatches =
            std::fabs(centroid.x - 2.5f) < 0.001f && std::fabs(centroid.y - 2.5f) < 0.001f;
        const bool boundsMatches =
            std::fabs(bounds.x - 0.0f) < 0.001f && std::fabs(bounds.y - 0.0f) < 0.001f &&
            std::fabs(bounds.width - 5.0f) < 0.001f && std::fabs(bounds.height - 5.0f) < 0.001f;
        Expect(centroidMatches,
               "polygon centroid cache should refresh after Intersect()",
               failures);
        Expect(boundsMatches,
               "polygon bounds cache should refresh after Intersect()",
               failures);
    }

    AppRuntime *runtime = AppRuntime::Instance();
    RecordingSink sink;
    runtime->SetResultSink(&sink);

    const int worldType = 13;
    const int mixing = 625;
    const std::vector<int> seedFixture{100000, 100030, 100123, 100500, 101337};

    runtime->SetSkipPolygons(true);
    runtime->PrepareSearchWorker(BuildWorldCode(worldType, seedFixture.front(), mixing));
    for (int seed : seedFixture) {
        const std::string code = BuildWorldCode(worldType, seed, mixing);
        const std::string legacy = RunLegacySummaryOnly(runtime, sink, code);
        const std::string optimized = RunPreparedSummaryOnly(runtime, sink, code);

        Expect(!legacy.empty(), "legacy summary fingerprint should not be empty", failures);
        Expect(!optimized.empty(), "optimized summary fingerprint should not be empty", failures);
        Expect(legacy == optimized,
               "optimized worker-reset summary output should match legacy path",
               failures);
    }

    {
        const std::string code = BuildWorldCode(worldType, seedFixture.front(), mixing);
        runtime->SetSkipPolygons(false);
        runtime->Initialize(0);
        sink.Reset();
        Expect(runtime->Generate(code, 0), "legacy preview path should generate successfully", failures);
        Expect(!sink.previews.empty(),
               "legacy Generate path should continue emitting previews",
               failures);
    }

    {
        const std::string code = BuildWorldCode(13, 0, 0);
        const auto mainPhases = RunWorldGenPhaseFingerprintOnCurrentThread(code);
        const auto freshPhases = RunWorldGenPhaseFingerprintOnFreshThread(code);

        Expect(!mainPhases.runtime.empty(),
               "main-thread worldgen runtime fingerprint should not be empty",
               failures);
        Expect(!freshPhases.runtime.empty(),
               "fresh-thread worldgen runtime fingerprint should not be empty",
               failures);
        Expect(!mainPhases.afterSeedPoints.empty(),
               "main-thread worldgen seed-points fingerprint should not be empty",
               failures);
        Expect(!freshPhases.afterSeedPoints.empty(),
               "fresh-thread worldgen seed-points fingerprint should not be empty",
               failures);
        Expect(!mainPhases.afterFullGenerate.empty(),
               "main-thread full worldgen fingerprint should not be empty",
               failures);
        Expect(!freshPhases.afterFullGenerate.empty(),
               "fresh-thread full worldgen fingerprint should not be empty",
               failures);

        ExpectEqual(mainPhases.runtime,
                    freshPhases.runtime,
                    "shared-copy runtime state should match between main-thread and fresh-thread",
                    failures);
        ExpectEqual(mainPhases.afterSeedPoints,
                    freshPhases.afterSeedPoints,
                    "worldgen seed-points stage should match between main-thread and fresh-thread",
                    failures);
        ExpectEqual(mainPhases.afterInitialDiagram,
                    freshPhases.afterInitialDiagram,
                    "worldgen initial diagram stage should match between main-thread and fresh-thread",
                    failures);
        ExpectEqual(mainPhases.afterDistanceTags,
                    freshPhases.afterDistanceTags,
                    "worldgen distance-tag stage should match between main-thread and fresh-thread",
                    failures);
        ExpectEqual(mainPhases.afterConvertUnknownCells,
                    freshPhases.afterConvertUnknownCells,
                    "worldgen convert-unknown-cells stage should match between main-thread and fresh-thread",
                    failures);
        ExpectEqual(mainPhases.afterPostConvertDiagram,
                    freshPhases.afterPostConvertDiagram,
                    "worldgen post-convert diagram stage should match between main-thread and fresh-thread",
                    failures);
        ExpectEqual(mainPhases.afterGenerateChildren,
                    freshPhases.afterGenerateChildren,
                    "worldgen generate-children stage should match between main-thread and fresh-thread",
                    failures);
        ExpectEqual(mainPhases.templatePlacements,
                    freshPhases.templatePlacements,
                    "worldgen template-placement stage should match between main-thread and fresh-thread",
                    failures);
        ExpectEqual(mainPhases.afterFullGenerate,
                    freshPhases.afterFullGenerate,
                    "full worldgen output should match between main-thread and fresh-thread",
                    failures);
        ExpectEqual(mainPhases.geysers,
                    freshPhases.geysers,
                    "worldgen geyser output should match between main-thread and fresh-thread",
                    failures);

        const auto concurrentSummaries =
            RunPairConcurrently<std::string>([&]() { return RunLegacySummaryOnFreshThread(code); });
        Expect(!concurrentSummaries.first.empty(),
               "concurrent legacy summary A should not be empty",
               failures);
        Expect(!concurrentSummaries.second.empty(),
               "concurrent legacy summary B should not be empty",
               failures);
        ExpectEqual(concurrentSummaries.first,
                    RunLegacySummaryOnly(runtime, sink, code),
                    "concurrent legacy summary A should match baseline legacy summary",
                    failures);
        ExpectEqual(concurrentSummaries.second,
                    RunLegacySummaryOnly(runtime, sink, code),
                    "concurrent legacy summary B should match baseline legacy summary",
                    failures);

        const auto concurrentPhases = RunPairConcurrently<WorldGenPhaseFingerprint>(
            [&]() { return RunWorldGenPhaseFingerprintOnCurrentThread(code); });
        ExpectEqual(concurrentPhases.first.afterSeedPoints,
                    mainPhases.afterSeedPoints,
                    "concurrent worldgen phase A seed-points should match baseline",
                    failures);
        ExpectEqual(concurrentPhases.second.afterSeedPoints,
                    mainPhases.afterSeedPoints,
                    "concurrent worldgen phase B seed-points should match baseline",
                    failures);
        ExpectEqual(concurrentPhases.first.afterInitialDiagram,
                    mainPhases.afterInitialDiagram,
                    "concurrent worldgen phase A initial diagram should match baseline",
                    failures);
        ExpectEqual(concurrentPhases.second.afterInitialDiagram,
                    mainPhases.afterInitialDiagram,
                    "concurrent worldgen phase B initial diagram should match baseline",
                    failures);
        ExpectEqual(concurrentPhases.first.afterDistanceTags,
                    mainPhases.afterDistanceTags,
                    "concurrent worldgen phase A distance-tag should match baseline",
                    failures);
        ExpectEqual(concurrentPhases.second.afterDistanceTags,
                    mainPhases.afterDistanceTags,
                    "concurrent worldgen phase B distance-tag should match baseline",
                    failures);
        ExpectEqual(concurrentPhases.first.afterConvertUnknownCells,
                    mainPhases.afterConvertUnknownCells,
                    "concurrent worldgen phase A convert-unknown-cells should match baseline",
                    failures);
        ExpectEqual(concurrentPhases.second.afterConvertUnknownCells,
                    mainPhases.afterConvertUnknownCells,
                    "concurrent worldgen phase B convert-unknown-cells should match baseline",
                    failures);
        ExpectEqual(concurrentPhases.first.afterPostConvertDiagram,
                    mainPhases.afterPostConvertDiagram,
                    "concurrent worldgen phase A post-convert diagram should match baseline",
                    failures);
        ExpectEqual(concurrentPhases.second.afterPostConvertDiagram,
                    mainPhases.afterPostConvertDiagram,
                    "concurrent worldgen phase B post-convert diagram should match baseline",
                    failures);
        ExpectEqual(concurrentPhases.first.afterGenerateChildren,
                    mainPhases.afterGenerateChildren,
                    "concurrent worldgen phase A generate-children should match baseline",
                    failures);
        ExpectEqual(concurrentPhases.second.afterGenerateChildren,
                    mainPhases.afterGenerateChildren,
                    "concurrent worldgen phase B generate-children should match baseline",
                    failures);
        ExpectEqual(concurrentPhases.first.templatePlacements,
                    mainPhases.templatePlacements,
                    "concurrent worldgen phase A template-placement should match baseline",
                    failures);
        ExpectEqual(concurrentPhases.second.templatePlacements,
                    mainPhases.templatePlacements,
                    "concurrent worldgen phase B template-placement should match baseline",
                    failures);
        ExpectEqual(concurrentPhases.first.geysers,
                    mainPhases.geysers,
                    "concurrent worldgen phase A geysers should match baseline",
                    failures);
        ExpectEqual(concurrentPhases.second.geysers,
                    mainPhases.geysers,
                    "concurrent worldgen phase B geysers should match baseline",
                    failures);
    }

    {
        const std::string code = BuildWorldCode(13, 0, 0);
        const std::string expected = RunLegacySummaryOnly(runtime, sink, code);
        const std::string freshThreadSummary = RunLegacySummaryOnFreshThread(code);
        const std::string expectedStartWorldCapture =
            FingerprintStartWorldSummaryAsBatchCapture(sink.summaries);
        const std::string mainThreadBatchCapture = RunLegacyBatchCaptureOnly(runtime, code);
        const std::string freshThreadBatchCapture = RunLegacyBatchCaptureOnFreshThread(code);
        const std::string singleWorker = RunLegacyBatchSearchOnce(BuildBalancedPlan(1));
        const std::string multiWorkerA = RunLegacyBatchSearchOnce(BuildActualBalancedPlan());
        const std::string multiWorkerB = RunLegacyBatchSearchOnce(BuildActualBalancedPlan());

        Expect(!expected.empty(),
               "single-seed legacy summary fixture should not be empty",
               failures);
        Expect(!freshThreadSummary.empty(),
               "fresh-thread legacy summary fingerprint should not be empty",
               failures);
        Expect(!expectedStartWorldCapture.empty(),
               "start-world legacy summary fixture should not be empty",
               failures);
        Expect(!mainThreadBatchCapture.empty(),
               "main-thread batch capture fingerprint should not be empty",
               failures);
        Expect(!freshThreadBatchCapture.empty(),
               "fresh-thread batch capture fingerprint should not be empty",
               failures);
        Expect(!singleWorker.empty(),
               "single-worker batch search fingerprint should not be empty",
               failures);
        Expect(!multiWorkerA.empty(),
               "multi-worker batch search fingerprint A should not be empty",
               failures);
        Expect(!multiWorkerB.empty(),
               "multi-worker batch search fingerprint B should not be empty",
               failures);
        ExpectEqual(mainThreadBatchCapture,
                    expectedStartWorldCapture,
                    "main-thread batch capture should match start-world legacy summary",
                    failures);
        ExpectEqual(freshThreadSummary,
                    expected,
                    "fresh-thread legacy summary should match main-thread legacy summary",
                    failures);
        ExpectEqual(freshThreadBatchCapture,
                    expectedStartWorldCapture,
                    "fresh-thread batch capture should match start-world legacy summary",
                    failures);
        ExpectEqual(singleWorker,
                    expectedStartWorldCapture,
                    "single-worker batch search should match start-world legacy summary",
                    failures);
        ExpectEqual(multiWorkerA,
                    singleWorker,
                    "multi-worker batch search should stay deterministic against single-worker baseline",
                    failures);
        ExpectEqual(multiWorkerB,
                    singleWorker,
                    "repeated multi-worker batch search should match single-worker baseline",
                    failures);
    }

    if (failures == 0) {
        std::cout << "[PASS] test_app_runtime_worker_reset" << std::endl;
        return 0;
    }
    return 1;
}
