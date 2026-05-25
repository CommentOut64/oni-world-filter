#define main sidecar_entry_main_for_test
#include "../../src/entry_sidecar.cpp"
#undef main

#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stack>
#include <string>
#include <vector>

#include <clipper.hpp>
#include <json/json.h>

namespace {

bool Expect(bool condition, const std::string &message, std::vector<std::string> *failures)
{
    if (condition || failures == nullptr) {
        return condition;
    }
    failures->push_back(message);
    return false;
}

class CollectingPreviewSink final : public ResultSink
{
public:
    bool RequestResource(uint32_t expectedSize, std::vector<char> &data) override
    {
        data.assign(expectedSize, 0);
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
        summaries.push_back(summary);
    }

    void OnGeneratedWorldPreview(const GeneratedWorldPreview &preview) override
    {
        previews.push_back(preview);
    }

    std::vector<GeneratedWorldSummary> summaries;
    std::vector<GeneratedWorldPreview> previews;
};

bool GeneratePreviewSet(int worldType,
                        int seed,
                        int mixing,
                        CollectingPreviewSink *sink,
                        std::string *code)
{
    if (sink == nullptr) {
        return false;
    }

    if (!BuildWorldCode(worldType, seed, mixing, code)) {
        return false;
    }

    auto *runtime = AppRuntime::Instance();
    runtime->SetResultSink(sink);
    runtime->SetSkipPolygons(false);
    runtime->Initialize(0);
    return runtime->Generate(*code, 0);
}

bool GeneratePreviewSessionForTest(int worldType,
                                   int seed,
                                   int mixing,
                                   PreviewWorldSession *session,
                                   std::string *errorMessage)
{
    Batch::SidecarPreviewRequest request;
    request.worldType = worldType;
    request.seed = seed;
    request.mixing = mixing;
    request.target = Batch::PreviewTarget::Primary;
    return GeneratePreviewSession(request, session, errorMessage);
}

bool HasPreviewWithPrimaryFlag(const std::vector<GeneratedWorldPreview> &previews, bool isPrimary)
{
    for (const auto &preview : previews) {
        if (preview.summary.isPrimary == isPrimary) {
            return true;
        }
    }
    return false;
}

bool PreviewContainsZoneType(const GeneratedWorldPreview &preview, int zoneType)
{
    for (const auto &polygon : preview.polygons) {
        if (polygon.zoneType == zoneType) {
            return true;
        }
    }
    return false;
}

struct PreviewSample {
    int worldType{};
    int seed{};
    int mixing{};
    const char *expectedPrefix{};
};

struct Dlc5PreviewSample {
    int worldType{};
    int seed{};
    int mixing{};
    const char *expectedPrefix{};
    const char *expectedPrimaryWorldAssetId{};
    int expectedPrimaryPlacementIndex{};
    std::optional<const char *> expectedSecondaryWorldAssetId;
    std::optional<int> expectedSecondaryPlacementIndex;
};

bool LoadSettingsForWorldType(int worldType,
                              int seed,
                              int mixing,
                              SettingsCache *settings,
                              std::string *code,
                              std::string *errorMessage)
{
    if (settings == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "settings output is null";
        }
        return false;
    }

    if (!BuildWorldCode(worldType, seed, mixing, code)) {
        if (errorMessage != nullptr) {
            *errorMessage = "invalid worldType";
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
    if (!settings->CoordinateChanged(*code, *settings)) {
        if (errorMessage != nullptr) {
            *errorMessage = "parse world code failed";
        }
        return false;
    }
    return true;
}

int CountWarpWorldPlacements(const SettingsCache &settings)
{
    if (settings.cluster == nullptr) {
        return 0;
    }

    int warpPlacementCount = 0;
    for (const auto &placement : settings.cluster->worldPlacements) {
        const auto itr = settings.worlds.find(placement.world);
        if (itr == settings.worlds.end()) {
            continue;
        }
        if (itr->second.startingBaseTemplate.contains("::bases/warpworld")) {
            ++warpPlacementCount;
        }
    }
    return warpPlacementCount;
}

std::string CaptureStdout(const std::function<void()> &action)
{
    std::ostringstream stream;
    auto *previous = std::cout.rdbuf(stream.rdbuf());
    action();
    std::cout.rdbuf(previous);
    return stream.str();
}

bool ParseSingleJsonLine(const std::string &text,
                         Json::Value *root,
                         std::vector<std::string> *failures,
                         const std::string &message)
{
    if (root == nullptr) {
        return false;
    }
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream stream(text);
    if (!Json::parseFromStream(builder, stream, root, &errors)) {
        if (failures != nullptr) {
            failures->push_back(message + ": " + errors);
        }
        return false;
    }
    return true;
}

bool BuildRawGeyserSummariesForTarget(int worldType,
                                      int seed,
                                      int mixing,
                                      Batch::PreviewTarget target,
                                      std::vector<GeyserSummary> *rawGeysers,
                                      Vector2i *worldSize,
                                      std::string *errorMessage)
{
    if (rawGeysers == nullptr || worldSize == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "raw geyser outputs are null";
        }
        return false;
    }

    SettingsCache settings;
    std::string code;
    if (!LoadSettingsForWorldType(worldType, seed, mixing, &settings, &code, errorMessage)) {
        return false;
    }

    PreviewPlacementSelection selection;
    if (!ResolvePreviewPlacementSelection(settings, &selection, errorMessage)) {
        return false;
    }

    const int placementIndex = IsPrimaryTarget(target)
                                   ? selection.primaryPlacementIndex
                                   : (selection.secondaryPlacementIndex.has_value()
                                          ? selection.secondaryPlacementIndex.value()
                                          : -1);
    if (placementIndex < 0) {
        if (errorMessage != nullptr) {
            *errorMessage = "secondary preview is not available for current seed";
        }
        return false;
    }

    std::vector<ResolvedWorldPlacement> placements;
    if (!BuildResolvedWorldPlacements(settings, &placements, errorMessage)) {
        return false;
    }

    std::vector<WorldEffectiveState> states;
    if (!InitializeWorldEffectiveStates(settings, placements, &states, errorMessage)) {
        return false;
    }

    const int baseSeed = settings.seed;
    for (auto &state : states) {
        World *world = &state.world;
        if (world->locationType == LocationType::Cluster) {
            continue;
        }

        settings.seed = baseSeed + state.placementIndex;
        state.randomTraits = settings.GetRandomTraits(*world);
        for (const auto *trait : state.randomTraits) {
            if (trait != nullptr) {
                world->ApplayTraits(*trait, settings);
            }
        }
    }
    settings.seed = baseSeed;
    ApplySubworldMixingToWorldEffectiveStates(settings, states);

    WorldEffectiveState *selectedState = FindWorldEffectiveState(states, placementIndex);
    if (selectedState == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "selected placement state not found";
        }
        return false;
    }

    settings.seed = baseSeed + placementIndex;
    WorldGen worldGen(selectedState->world, settings);
    std::vector<Site> sites;
    if (!worldGen.GenerateOverworld(sites)) {
        if (errorMessage != nullptr) {
            *errorMessage = "GenerateOverworld failed";
        }
        return false;
    }

    const int geyserSeed = baseSeed + (settings.cluster != nullptr
                                           ? static_cast<int>(settings.cluster->worldPlacements.size()) - 1
                                           : 0);
    const auto generatedGeysers = worldGen.GetGeysers(geyserSeed);
    rawGeysers->clear();
    rawGeysers->reserve(generatedGeysers.size());
    for (const auto &geyser : generatedGeysers) {
        rawGeysers->push_back({geyser.z, geyser.x, geyser.y, geyser.x, geyser.y});
    }
    *worldSize = selectedState->world.worldsize;
    settings.seed = baseSeed;
    return true;
}

void TraceTestProgress(const std::string &message)
{
    std::cerr << "[TRACE] " << message << std::endl;
}

bool BuildZonePolygonForTest(Site &site, Polygon &polygon)
{
    const ZoneType zoneType = site.subworld->zoneType;
    ClipperLib::Clipper clipper;
    std::stack<Site *> stack;
    stack.push(&site);
    while (!stack.empty()) {
        Site *top = stack.top();
        stack.pop();
        if (top->visited) {
            continue;
        }
        ClipperLib::Path path;
        for (Vector2f point : top->polygon.Vertices) {
            point *= 10000.0f;
            path.emplace_back((int)point.x, (int)point.y);
        }
        clipper.AddPath(path, ClipperLib::ptSubject, true);
        top->visited = true;
        for (auto *neighbour : top->neighbours) {
            if (neighbour->visited) {
                continue;
            }
            if (neighbour->subworld->zoneType != zoneType) {
                continue;
            }
            stack.push(neighbour);
        }
    }

    ClipperLib::PolyTree polytree;
    ClipperLib::Paths paths;
    clipper.Execute(ClipperLib::ctUnion, polytree, ClipperLib::pftEvenOdd);
    ClipperLib::PolyTreeToPaths(polytree, paths);
    if (!paths.empty()) {
        auto &path = paths[0];
        for (auto &item : path) {
            Vector2f point{(float)item.X, (float)item.Y};
            polygon.Vertices.emplace_back(point * 0.0001f);
        }
    }
    return paths.size() > 1;
}

bool TracePrimaryWorldGenerationStages(int worldType,
                                       int seed,
                                       int mixing,
                                       std::string *errorMessage)
{
    SettingsCache settings;
    std::string code;
    if (!LoadSettingsForWorldType(worldType, seed, mixing, &settings, &code, errorMessage)) {
        return false;
    }
    TraceTestProgress("stage placements begin");
    std::vector<ResolvedWorldPlacement> placements;
    if (!BuildResolvedWorldPlacements(settings, &placements, errorMessage)) {
        return false;
    }
    TraceTestProgress("stage placements end");

    TraceTestProgress("stage effective states begin");
    std::vector<WorldEffectiveState> states;
    if (!InitializeWorldEffectiveStates(settings, placements, &states, errorMessage)) {
        return false;
    }
    ApplySubworldMixingToWorldEffectiveStates(settings, states);
    TraceTestProgress("stage effective states end");

    TraceTestProgress("stage offsets begin");
    std::vector<ClusterWorldOffset> worldOffsets;
    if (!ComputeClusterWorldOffsets(placements, &worldOffsets, errorMessage)) {
        return false;
    }
    TraceTestProgress("stage offsets end");

    WorldEffectiveState *primaryState = nullptr;
    for (auto &state : states) {
        if (state.world.locationType == LocationType::StartWorld) {
            primaryState = &state;
            break;
        }
    }
    if (primaryState == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "primary world effective state not found";
        }
        return false;
    }

    const int baseSeed = settings.seed;
    settings.seed = baseSeed + primaryState->placementIndex;
    primaryState->randomTraits = settings.GetRandomTraits(primaryState->world);
    for (const auto *trait : primaryState->randomTraits) {
        if (trait != nullptr) {
            primaryState->world.ApplayTraits(*trait, settings);
        }
    }

    TraceTestProgress("stage overworld begin");
    WorldGen worldGen(primaryState->world, settings);
    std::vector<Site> sites;
    const bool generated = worldGen.GenerateOverworld(sites);
    TraceTestProgress("stage overworld end");
    TraceTestProgress("stage geysers begin");
    const int geyserSeed = baseSeed + (settings.cluster != nullptr
                                           ? static_cast<int>(settings.cluster->worldPlacements.size()) - 1
                                           : 0);
    const auto geysers = worldGen.GetGeysers(geyserSeed);
    (void)geysers;
    TraceTestProgress("stage geysers end");
    TraceTestProgress("stage preview polygons begin");
    GeneratedWorldSummary summary;
    summary.seed = baseSeed;
    summary.geyserSeed = geyserSeed;
    summary.isPrimary = true;
    summary.worldType = 0;
    summary.worldPlacementIndex = primaryState->placementIndex;
    summary.worldAssetId = primaryState->worldAssetId;
    summary.worldSize = primaryState->world.worldsize;
    const auto *worldOffset = FindClusterWorldOffset(worldOffsets, primaryState->placementIndex);
    if (worldOffset != nullptr) {
        summary.worldOffsetX = worldOffset->offset.x;
        summary.worldOffsetY = worldOffset->offset.y;
    }
    for (const auto &geyser : geysers) {
        summary.geysers.push_back({
            geyser.z,
            geyser.x,
            static_cast<int>(primaryState->world.worldsize.y - geyser.y),
            geyser.x,
            geyser.y,
        });
    }
    GeneratedWorldPreview preview;
    preview.summary = summary;
    size_t totalVertices = 0;
    std::ranges::for_each(sites, [](Site &site) { site.visited = false; });
    for (auto &site : sites) {
        if (site.visited) {
            continue;
        }
        TraceTestProgress("stage preview polygon zone=" +
                          std::to_string(static_cast<int>(site.subworld->zoneType)));
        Polygon polygon;
        PolygonSummary polygonSummary;
        polygonSummary.hasHole = BuildZonePolygonForTest(site, polygon);
        polygonSummary.zoneType = (int)site.subworld->zoneType;
        polygonSummary.vertices.reserve(polygon.Vertices.size());
        for (auto &vertex : polygon.Vertices) {
            polygonSummary.vertices.push_back({
                (int)vertex.x,
                primaryState->world.worldsize.y - (int)vertex.y,
            });
        }
        totalVertices += polygonSummary.vertices.size();
        preview.polygons.push_back(std::move(polygonSummary));
    }
    TraceTestProgress("stage preview polygon summary count=" +
                      std::to_string(preview.polygons.size()) +
                      " vertices=" + std::to_string(totalVertices));
    TraceTestProgress("stage sink push begin");
    CollectingPreviewSink sink;
    sink.OnGeneratedWorldSummary(summary);
    sink.OnGeneratedWorldPreview(preview);
    TraceTestProgress("stage sink push end");
    TraceTestProgress("stage preview polygons end");
    settings.seed = baseSeed;

    if (!generated) {
        if (errorMessage != nullptr && errorMessage->empty()) {
            *errorMessage = "GenerateOverworld failed";
        }
        return false;
    }
    return true;
}

bool TraceSelectedPlacementPipeline(int worldType,
                                    int seed,
                                    int mixing,
                                    int placementIndex,
                                    std::string *errorMessage)
{
    SettingsCache settings;
    std::string code;
    if (!LoadSettingsForWorldType(worldType, seed, mixing, &settings, &code, errorMessage)) {
        return false;
    }

    TraceTestProgress("selected pipeline placements begin");
    std::vector<ResolvedWorldPlacement> placements;
    if (!BuildResolvedWorldPlacements(settings, &placements, errorMessage)) {
        return false;
    }
    TraceTestProgress("selected pipeline placements end");

    TraceTestProgress("selected pipeline effective states begin");
    std::vector<WorldEffectiveState> states;
    if (!InitializeWorldEffectiveStates(settings, placements, &states, errorMessage)) {
        return false;
    }
    TraceTestProgress("selected pipeline effective states end");

    TraceTestProgress("selected pipeline offsets begin");
    std::vector<ClusterWorldOffset> worldOffsets;
    if (!ComputeClusterWorldOffsets(placements, &worldOffsets, errorMessage)) {
        return false;
    }
    TraceTestProgress("selected pipeline offsets end");

    const int baseSeed = settings.seed;
    TraceTestProgress("selected pipeline traits begin");
    for (auto &state : states) {
        World *world = &state.world;
        if (world->locationType == LocationType::Cluster) {
            continue;
        }

        settings.seed = baseSeed + state.placementIndex;
        state.randomTraits = settings.GetRandomTraits(*world);
        for (const auto *trait : state.randomTraits) {
            if (trait != nullptr) {
                world->ApplayTraits(*trait, settings);
            }
        }
    }
    settings.seed = baseSeed;
    ApplySubworldMixingToWorldEffectiveStates(settings, states);
    TraceTestProgress("selected pipeline traits end");

    WorldEffectiveState *selectedState = FindWorldEffectiveState(states, placementIndex);
    if (selectedState == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "selected placement state not found";
        }
        return false;
    }

    const ClusterWorldOffset *worldOffset = FindClusterWorldOffset(worldOffsets, placementIndex);
    if (worldOffset == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "selected placement world offset not found";
        }
        return false;
    }

    TraceTestProgress("selected pipeline worldgen begin");
    settings.seed = baseSeed + placementIndex;
    WorldGen worldGen(selectedState->world, settings);
    std::vector<Site> sites;
    if (!worldGen.GenerateOverworld(sites)) {
        if (errorMessage != nullptr && errorMessage->empty()) {
            *errorMessage = "selected placement GenerateOverworld failed";
        }
        return false;
    }
    TraceTestProgress("selected pipeline worldgen end");

    TraceTestProgress("selected pipeline summary begin");
    GeneratedWorldSummary summary;
    summary.seed = baseSeed;
    summary.geyserSeed = baseSeed + (settings.cluster != nullptr
                                         ? static_cast<int>(settings.cluster->worldPlacements.size()) - 1
                                         : 0);
    summary.isPrimary = true;
    summary.worldType = 0;
    summary.worldPlacementIndex = selectedState->placementIndex;
    summary.worldAssetId = selectedState->worldAssetId;
    summary.start = {sites[0].x, sites[0].y};
    summary.worldSize = selectedState->world.worldsize;
    summary.start.y = summary.worldSize.y - summary.start.y;
    summary.worldOffsetX = worldOffset->offset.x;
    summary.worldOffsetY = worldOffset->offset.y;
    for (const auto *trait : selectedState->randomTraits) {
        uint32_t index = 0;
        for (auto &pair : settings.traits) {
            if (trait == &pair.second) {
                summary.traits.push_back({(int)index});
                break;
            }
            ++index;
        }
    }
    const auto geysers = worldGen.GetGeysers(summary.geyserSeed);
    for (const auto &geyser : geysers) {
        summary.geysers.push_back({
            geyser.z,
            geyser.x,
            static_cast<int>(selectedState->world.worldsize.y - geyser.y),
            geyser.x,
            geyser.y,
        });
    }
    TraceTestProgress("selected pipeline summary end");

    TraceTestProgress("selected pipeline preview begin");
    GeneratedWorldPreview preview;
    preview.summary = summary;
    std::ranges::for_each(sites, [](Site &site) { site.visited = false; });
    for (auto &site : sites) {
        if (site.visited) {
            continue;
        }
        Polygon polygon;
        PolygonSummary polygonSummary;
        polygonSummary.hasHole = BuildZonePolygonForTest(site, polygon);
        polygonSummary.zoneType = (int)site.subworld->zoneType;
        for (auto &vertex : polygon.Vertices) {
            polygonSummary.vertices.push_back({
                (int)vertex.x,
                selectedState->world.worldsize.y - (int)vertex.y,
            });
        }
        preview.polygons.push_back(std::move(polygonSummary));
    }
    TraceTestProgress("selected pipeline preview end");
    settings.seed = baseSeed;
    return true;
}

} // namespace

int RunAllTests()
{
    std::vector<std::string> failures;

    const std::vector<PreviewSample> moonletSamples = {
        {32, 100123, 0, "M-SWMP-C-"},
        {33, 100123, 0, "M-BAD-C-"},
        {34, 100123, 0, "M-FRZ-C-"},
        {36, 100123, 0, "M-RAD-C-"},
        {37, 100123, 0, "M-CERS-C-"},
    };
    const std::vector<PreviewSample> classicSpacedOutSamples = {
        {27, 100123, 0, "SNDST-C-"},
        {28, 100123, 0, "PRE-C-"},
        {29, 100123, 0, "CER-C-"},
        {30, 100123, 0, "FRST-C-"},
        {31, 100123, 0, "SWMP-C-"},
    };
    const std::vector<PreviewSample> vanillaStyleSpacedOutSamples = {
        {13, 100123, 0, "V-SNDST-C-"},
        {15, 100123, 0, "V-SWMP-C-"},
        {16, 100123, 0, "V-SFRZ-C-"},
        {23, 100123, 0, "V-CER-C-"},
    };
    const std::vector<Dlc5PreviewSample> aquaticSamples = {
        {38, 100123, 0, "AQU-A-", "dlc5::worlds/AquaticBaseGameAsteroid", 0, std::nullopt, std::nullopt},
        {39, 100123, 0, "V-AQU-C-", "dlc5::worlds/AquaticClassicAsteroid", 0,
         "expansion1::worlds/MediumSwampy", 1},
        {40, 100123, 0, "AQU-C-", "dlc5::worlds/AquaticSpacedOutAsteroid", 0,
         "expansion1::worlds/WarpOilySandySwamp", 2},
    };

    for (const auto &sample : moonletSamples) {
        CollectingPreviewSink sink;
        std::string code;
        const bool generated =
            GeneratePreviewSet(sample.worldType, sample.seed, sample.mixing, &sink, &code);
        Expect(generated, std::string("moonlet preview should generate for ") + sample.expectedPrefix, &failures);
        if (!generated) {
            continue;
        }

        Expect(code.starts_with(sample.expectedPrefix),
               std::string("generated code prefix mismatch for ") + sample.expectedPrefix,
               &failures);
        Expect(!sink.previews.empty(),
               std::string("preview list should not be empty for ") + sample.expectedPrefix,
               &failures);
        Expect(HasPreviewWithPrimaryFlag(sink.previews, true),
               std::string("primary preview should exist for ") + sample.expectedPrefix,
               &failures);
        Expect(HasPreviewWithPrimaryFlag(sink.previews, false),
               std::string("secondary preview should exist for ") + sample.expectedPrefix,
               &failures);

        for (const auto &preview : sink.previews) {
            Expect(preview.summary.worldPlacementIndex >= 0,
                   std::string("worldPlacementIndex should be non-negative for ") + sample.expectedPrefix,
                   &failures);
            Expect(preview.summary.seed == sample.seed,
                   std::string("summary seed should stay on base seed for ") + sample.expectedPrefix,
                   &failures);
        }
    }

    {
        for (const auto &sample : classicSpacedOutSamples) {
            SettingsCache settings;
            std::string code;
            std::string errorMessage;
            Expect(LoadSettingsForWorldType(sample.worldType,
                                            sample.seed,
                                            sample.mixing,
                                            &settings,
                                            &code,
                                            &errorMessage),
                   std::string("classic SO settings should load for ") + sample.expectedPrefix,
                   &failures);
            if (!errorMessage.empty()) {
                failures.push_back(std::string("classic SO settings load error for ") +
                                   sample.expectedPrefix + ": " + errorMessage);
            }
            if (!settings.cluster) {
                continue;
            }

            Expect(CountWarpWorldPlacements(settings) == 1,
                   std::string("classic SO cluster should contain exactly one warp placement for ") +
                       sample.expectedPrefix,
                   &failures);

            PreviewWorldSession session;
            errorMessage.clear();
            Expect(GeneratePreviewSessionForTest(sample.worldType,
                                                sample.seed,
                                                sample.mixing,
                                                &session,
                                                &errorMessage),
                   std::string("classic SO preview session should generate for ") + sample.expectedPrefix,
                   &failures);
            if (!errorMessage.empty()) {
                failures.push_back(std::string("classic SO preview session error for ") +
                                   sample.expectedPrefix + ": " + errorMessage);
            }
            Expect(session.secondaryPreview.has_value(),
                   std::string("classic SO preview session should expose secondary preview for ") +
                       sample.expectedPrefix,
                   &failures);
            if (session.primaryPreview.has_value()) {
                Expect(session.primaryPreview->summary.hasSecondaryPreview,
                       std::string("classic SO primary preview should report secondary availability for ") +
                           sample.expectedPrefix,
                       &failures);
            }
        }
    }

    {
        PreviewWorldSession session;
        std::string errorMessage;
        Expect(GeneratePreviewSessionForTest(33, 100123, 0, &session, &errorMessage),
               "M-BAD-C preview session should generate",
               &failures);
        if (session.secondaryPreview.has_value()) {
            Expect(session.secondaryPreview->summary.worldAssetId ==
                       "expansion1::worlds/MiniRadioactiveOceanWarp",
                   "M-BAD-C secondary preview should keep its real world asset id",
                   &failures);
        }
    }

    {
        PreviewWorldSession session;
        std::string errorMessage;
        Expect(GeneratePreviewSessionForTest(36, 100123, 0, &session, &errorMessage),
               "M-RAD-C preview session should generate",
               &failures);
        if (session.secondaryPreview.has_value()) {
            Expect(session.secondaryPreview->summary.worldAssetId ==
                       "expansion1::worlds/MiniFlippedWarp",
                   "M-RAD-C secondary preview should not be locked to another world asset",
                   &failures);
        }
    }

    {
        CollectingPreviewSink sink;
        std::string code;
        const bool generated = GeneratePreviewSet(13, 100123, 625, &sink, &code);
        Expect(generated, "non-moonlet preview should generate", &failures);
        if (generated) {
            Expect(code.starts_with("V-SNDST-C-"),
                   "non-moonlet code prefix mismatch",
                   &failures);
            Expect(HasPreviewWithPrimaryFlag(sink.previews, true),
                   "non-moonlet preview should contain primary world",
                   &failures);
            Expect(!HasPreviewWithPrimaryFlag(sink.previews, false),
                   "non-moonlet preview should not contain secondary world",
                   &failures);
        }
    }

    {
        for (const auto &sample : aquaticSamples) {
            TraceTestProgress(std::string("DLC5 sample start: ") + sample.expectedPrefix);
            SettingsCache settings;
            std::string code;
            std::string errorMessage;
            Expect(LoadSettingsForWorldType(sample.worldType,
                                            sample.seed,
                                            sample.mixing,
                                            &settings,
                                            &code,
                                           &errorMessage),
                   std::string("DLC5 settings should load for ") + sample.expectedPrefix,
                   &failures);
            TraceTestProgress(std::string("DLC5 settings loaded: ") + sample.expectedPrefix);
            if (!errorMessage.empty()) {
                failures.push_back(std::string("DLC5 settings load error for ") +
                                   sample.expectedPrefix + ": " + errorMessage);
            }
            Expect(code.starts_with(sample.expectedPrefix),
                   std::string("DLC5 generated code prefix mismatch for ") + sample.expectedPrefix,
                   &failures);
            Expect(settings.cluster != nullptr,
                   std::string("DLC5 cluster should resolve for ") + sample.expectedPrefix,
                   &failures);

            if (settings.cluster != nullptr) {
                Expect(!settings.cluster->worldPlacements.empty(),
                       std::string("DLC5 cluster should contain world placements for ") +
                           sample.expectedPrefix,
                       &failures);
            }

            std::string stageError;
            TraceTestProgress(std::string("DLC5 stage trace begin: ") + sample.expectedPrefix);
            Expect(TracePrimaryWorldGenerationStages(sample.worldType,
                                                    sample.seed,
                                                    sample.mixing,
                                                    &stageError),
                   std::string("DLC5 staged primary generation should succeed for ") +
                       sample.expectedPrefix,
                   &failures);
            TraceTestProgress(std::string("DLC5 stage trace end: ") + sample.expectedPrefix);
            if (!stageError.empty()) {
                failures.push_back(std::string("DLC5 staged primary generation error for ") +
                                   sample.expectedPrefix + ": " + stageError);
            }

            std::string selectedPipelineError;
            TraceTestProgress(std::string("DLC5 selected pipeline trace begin: ") + sample.expectedPrefix);
            Expect(TraceSelectedPlacementPipeline(sample.worldType,
                                                 sample.seed,
                                                 sample.mixing,
                                                 sample.expectedPrimaryPlacementIndex,
                                                 &selectedPipelineError),
                   std::string("DLC5 selected placement pipeline trace should succeed for ") +
                       sample.expectedPrefix,
                   &failures);
            TraceTestProgress(std::string("DLC5 selected pipeline trace end: ") + sample.expectedPrefix);
            if (!selectedPipelineError.empty()) {
                failures.push_back(std::string("DLC5 selected placement pipeline error for ") +
                                   sample.expectedPrefix + ": " + selectedPipelineError);
            }

            CollectingPreviewSink selectedPlacementSink;
            auto *runtime = AppRuntime::Instance();
            runtime->SetResultSink(&selectedPlacementSink);
            runtime->SetSkipPolygons(false);
            runtime->Initialize(0);
            TraceTestProgress(std::string("DLC5 selected placements call begin: ") + sample.expectedPrefix);
            const bool selectedPlacementsGenerated =
                runtime->GenerateSelectedPlacements(code,
                                                   0,
                                                   {sample.expectedPrimaryPlacementIndex},
                                                   sample.expectedPrimaryPlacementIndex);
            TraceTestProgress(std::string("DLC5 selected placements call end: ") + sample.expectedPrefix);
            Expect(selectedPlacementsGenerated,
                   std::string("DLC5 selected placement preview should generate for ") +
                       sample.expectedPrefix,
                   &failures);

            CollectingPreviewSink sink;
            TraceTestProgress(std::string("DLC5 preview set call begin: ") + sample.expectedPrefix);
            const bool generated =
                GeneratePreviewSet(sample.worldType, sample.seed, sample.mixing, &sink, &code);
            TraceTestProgress(std::string("DLC5 preview set call end: ") + sample.expectedPrefix);
            Expect(generated,
                   std::string("DLC5 preview should generate for ") + sample.expectedPrefix,
                   &failures);
            TraceTestProgress(std::string("DLC5 preview set generated: ") + sample.expectedPrefix);
            if (!generated || sink.previews.empty()) {
                continue;
            }

            const auto *primaryPreview = FindPrimaryGeneratedWorldPreview(sink.previews);
            Expect(primaryPreview != nullptr,
                   std::string("DLC5 primary preview should exist for ") + sample.expectedPrefix,
                   &failures);
            if (primaryPreview == nullptr) {
                continue;
            }

            Expect(primaryPreview->summary.seed == sample.seed,
                   std::string("DLC5 preview seed should stay on base seed for ") +
                       sample.expectedPrefix,
                   &failures);
            Expect(primaryPreview->summary.isPrimary,
                   std::string("DLC5 primary preview should keep primary flag for ") +
                       sample.expectedPrefix,
                   &failures);
            Expect(primaryPreview->summary.worldPlacementIndex >= 0,
                   std::string("DLC5 preview placement index should be non-negative for ") +
                       sample.expectedPrefix,
                   &failures);
            Expect(primaryPreview->summary.worldPlacementIndex == sample.expectedPrimaryPlacementIndex,
                   std::string("DLC5 primary preview placement index mismatch for ") +
                       sample.expectedPrefix,
                   &failures);
            Expect(primaryPreview->summary.worldAssetId == sample.expectedPrimaryWorldAssetId,
                   std::string("DLC5 primary preview world asset id mismatch for ") +
                       sample.expectedPrefix,
                   &failures);
            Expect(primaryPreview->summary.hasSecondaryPreview ==
                       sample.expectedSecondaryWorldAssetId.has_value(),
                   std::string("DLC5 primary preview secondary-availability mismatch for ") +
                       sample.expectedPrefix,
                   &failures);
            Expect(HasPreviewWithPrimaryFlag(sink.previews, false) ==
                       sample.expectedSecondaryWorldAssetId.has_value(),
                   std::string("DLC5 generated preview list secondary-presence mismatch for ") +
                       sample.expectedPrefix,
                   &failures);
            Expect(PreviewContainsZoneType(*primaryPreview, static_cast<int>(ZoneType::Beach)),
                   std::string("DLC5 preview should contain Beach zone for ") +
                       sample.expectedPrefix,
                   &failures);
            Expect(PreviewContainsZoneType(*primaryPreview, static_cast<int>(ZoneType::Reef)),
                   std::string("DLC5 preview should contain Reef zone for ") +
                       sample.expectedPrefix,
                   &failures);
            Expect(PreviewContainsZoneType(*primaryPreview,
                                           static_cast<int>(ZoneType::KelpForest)),
                   std::string("DLC5 preview should contain KelpForest zone for ") +
                       sample.expectedPrefix,
                   &failures);
            Expect(PreviewContainsZoneType(*primaryPreview, static_cast<int>(ZoneType::Abyss)),
                   std::string("DLC5 preview should contain Abyss zone for ") +
                       sample.expectedPrefix,
                   &failures);

            PreviewWorldSession session;
            errorMessage.clear();
            TraceTestProgress(std::string("DLC5 preview session begin: ") + sample.expectedPrefix);
            Expect(GeneratePreviewSessionForTest(sample.worldType,
                                                sample.seed,
                                                sample.mixing,
                                                &session,
                                                &errorMessage),
                   std::string("DLC5 preview session should generate for ") +
                       sample.expectedPrefix,
                   &failures);
            TraceTestProgress(std::string("DLC5 preview session end: ") + sample.expectedPrefix);
            if (!errorMessage.empty()) {
                failures.push_back(std::string("DLC5 preview session error for ") +
                                   sample.expectedPrefix + ": " + errorMessage);
            }

            Expect(session.primaryPreview.has_value(),
                   std::string("DLC5 preview session should expose primary preview for ") +
                       sample.expectedPrefix,
                   &failures);
            Expect(session.secondaryPreview.has_value() ==
                       sample.expectedSecondaryWorldAssetId.has_value(),
                   std::string("DLC5 preview session secondary-presence mismatch for ") +
                       sample.expectedPrefix,
                   &failures);
            Expect(session.secondaryPlacementIndex.has_value() ==
                       sample.expectedSecondaryPlacementIndex.has_value(),
                   std::string("DLC5 preview session secondary-placement mismatch for ") +
                       sample.expectedPrefix,
                   &failures);

            if (session.primaryPreview.has_value()) {
                Expect(session.primaryPreview->summary.worldAssetId ==
                           sample.expectedPrimaryWorldAssetId,
                       std::string("DLC5 preview session primary world asset id mismatch for ") +
                           sample.expectedPrefix,
                       &failures);
                Expect(session.primaryPreview->summary.worldPlacementIndex ==
                           sample.expectedPrimaryPlacementIndex,
                       std::string("DLC5 preview session primary placement index mismatch for ") +
                           sample.expectedPrefix,
                       &failures);
                Expect(session.primaryPreview->summary.hasSecondaryPreview ==
                           sample.expectedSecondaryWorldAssetId.has_value(),
                       std::string("DLC5 preview session primary secondary-availability mismatch for ") +
                           sample.expectedPrefix,
                       &failures);
            }

            if (sample.expectedSecondaryWorldAssetId.has_value()) {
                Expect(session.secondaryPreview.has_value(),
                       std::string("DLC5 preview session should expose secondary preview for ") +
                           sample.expectedPrefix,
                       &failures);
                Expect(session.secondaryPlacementIndex == sample.expectedSecondaryPlacementIndex,
                       std::string("DLC5 preview session secondary placement index mismatch for ") +
                           sample.expectedPrefix,
                       &failures);
                if (session.secondaryPreview.has_value()) {
                    Expect(session.secondaryPreview->summary.worldAssetId ==
                               sample.expectedSecondaryWorldAssetId.value(),
                           std::string("DLC5 preview session secondary world asset id mismatch for ") +
                               sample.expectedPrefix,
                           &failures);
                    Expect(session.secondaryPreview->summary.worldPlacementIndex ==
                               sample.expectedSecondaryPlacementIndex.value(),
                           std::string("DLC5 preview session secondary preview placement index mismatch for ") +
                               sample.expectedPrefix,
                           &failures);
                    Expect(!session.secondaryPreview->summary.isPrimary,
                           std::string("DLC5 secondary preview should keep non-primary flag for ") +
                               sample.expectedPrefix,
                           &failures);
                    Expect(session.secondaryPreview->summary.hasSecondaryPreview,
                           std::string("DLC5 secondary preview should report secondary availability for ") +
                               sample.expectedPrefix,
                           &failures);
                }
            } else {
                Expect(!session.secondaryPreview.has_value(),
                       std::string("DLC5 base-game preview session should not expose secondary preview for ") +
                           sample.expectedPrefix,
                       &failures);
                Expect(!session.secondaryPlacementIndex.has_value(),
                       std::string("DLC5 base-game preview session should not expose secondary placement for ") +
                           sample.expectedPrefix,
                       &failures);
            }
        }
    }

    {
        SettingsCache settings;
        std::string code;
        std::string errorMessage;
        Expect(LoadSettingsForWorldType(27, 100123, 0, &settings, &code, &errorMessage),
               "SNDST-C settings should load for geyser offset checks",
               &failures);
        PreviewPlacementSelection selection;
        Expect(ResolvePreviewPlacementSelection(settings, &selection, &errorMessage),
               "SNDST-C placement selection should resolve for geyser offset checks",
               &failures);
        Expect(settings.cluster != nullptr &&
                   settings.cluster->clusterCategory == ClusterCategory::SpacedOutStyle,
               "SNDST-C should stay SpacedOutStyle after settings load",
               &failures);
        Expect(selection.primaryPlacementIndex == 0,
               "SNDST-C primary placement should stay at index 0",
               &failures);
        Expect(selection.secondaryPlacementIndex.has_value(),
               "SNDST-C should expose a secondary placement candidate",
               &failures);
    }

    {
        GeyserSeedContext primaryContext;
        std::string errorMessage;
        Expect(ResolveGeyserSeedContext(0,
                                        100123,
                                        0,
                                        Batch::PreviewTarget::Primary,
                                        &primaryContext,
                                        &errorMessage),
               "SNDST-A primary geyser context should resolve",
               &failures);
        Expect(primaryContext.worldOffsetX == 0,
               "SNDST-A primary worldOffsetX should stay 0",
               &failures);
        Expect(primaryContext.worldOffsetY == 0,
               "SNDST-A primary worldOffsetY should stay 0",
               &failures);
    }

    {
        GeyserSeedContext primaryContext;
        GeyserSeedContext secondaryContext;
        std::string errorMessage;
        PreviewWorldSession session;
        Expect(GeneratePreviewSessionForTest(33, 100123, 0, &session, &errorMessage),
               "M-BAD-C preview session should generate for authoritative offset checks",
               &failures);
        Expect(ResolveGeyserSeedContext(33,
                                        100123,
                                        0,
                                        Batch::PreviewTarget::Primary,
                                        &primaryContext,
                                        &errorMessage),
               "M-BAD-C primary geyser context should resolve",
               &failures);
        errorMessage.clear();
        Expect(ResolveGeyserSeedContext(33,
                                        100123,
                                        0,
                                        Batch::PreviewTarget::Secondary,
                                        &secondaryContext,
                                        &errorMessage),
               "M-BAD-C secondary geyser context should resolve from preview summary",
               &failures);
        Expect(session.primaryPreview.has_value(),
               "M-BAD-C primary preview should exist for authoritative offset checks",
               &failures);
        Expect(session.secondaryPreview.has_value(),
               "M-BAD-C secondary preview should exist for authoritative offset checks",
               &failures);
        if (session.primaryPreview.has_value()) {
            Expect(primaryContext.worldOffsetX == session.primaryPreview->summary.worldOffsetX,
                   "M-BAD-C primary geyser context should match preview summary worldOffsetX",
                   &failures);
            Expect(primaryContext.worldOffsetY == session.primaryPreview->summary.worldOffsetY,
                   "M-BAD-C primary geyser context should match preview summary worldOffsetY",
                   &failures);
        }
        if (session.secondaryPreview.has_value()) {
            Expect(secondaryContext.worldOffsetX == session.secondaryPreview->summary.worldOffsetX,
                   "M-BAD-C secondary geyser context should match preview summary worldOffsetX",
                   &failures);
            Expect(secondaryContext.worldOffsetY == session.secondaryPreview->summary.worldOffsetY,
                   "M-BAD-C secondary geyser context should match preview summary worldOffsetY",
                   &failures);
        }
        Expect(secondaryContext.worldOffsetX != 82,
               "M-BAD-C secondary worldOffsetX should not collapse onto primary legacy constant",
               &failures);
        Expect(primaryContext.worldOffsetX == 82,
               "M-BAD-C primary worldOffsetX should stay 82",
               &failures);
        Expect(primaryContext.worldOffsetY == 0,
               "M-BAD-C primary worldOffsetY should stay 0",
               &failures);
    }

    {
        GeyserSeedContext primaryContext;
        GeyserSeedContext secondaryContext;
        std::string errorMessage;
        PreviewWorldSession session;
        Expect(GeneratePreviewSessionForTest(34, 100123, 0, &session, &errorMessage),
               "M-FRZ-C preview session should generate for authoritative offset checks",
               &failures);
        Expect(ResolveGeyserSeedContext(34,
                                        100123,
                                        0,
                                        Batch::PreviewTarget::Primary,
                                        &primaryContext,
                                        &errorMessage),
               "M-FRZ-C primary geyser context should resolve",
               &failures);
        errorMessage.clear();
        Expect(ResolveGeyserSeedContext(34,
                                        100123,
                                        0,
                                        Batch::PreviewTarget::Secondary,
                                        &secondaryContext,
                                        &errorMessage),
               "M-FRZ-C secondary geyser context should resolve from preview summary",
               &failures);
        Expect(session.primaryPreview.has_value(),
               "M-FRZ-C primary preview should exist for authoritative offset checks",
               &failures);
        Expect(session.secondaryPreview.has_value(),
               "M-FRZ-C secondary preview should exist for authoritative offset checks",
               &failures);
        if (session.primaryPreview.has_value()) {
            Expect(primaryContext.worldOffsetX == session.primaryPreview->summary.worldOffsetX,
                   "M-FRZ-C primary geyser context should match preview summary worldOffsetX",
                   &failures);
            Expect(primaryContext.worldOffsetY == session.primaryPreview->summary.worldOffsetY,
                   "M-FRZ-C primary geyser context should match preview summary worldOffsetY",
                   &failures);
        }
        if (session.secondaryPreview.has_value()) {
            Expect(secondaryContext.worldOffsetX == session.secondaryPreview->summary.worldOffsetX,
                   "M-FRZ-C secondary geyser context should match preview summary worldOffsetX",
                   &failures);
            Expect(secondaryContext.worldOffsetY == session.secondaryPreview->summary.worldOffsetY,
                   "M-FRZ-C secondary geyser context should match preview summary worldOffsetY",
                   &failures);
        }
        Expect(secondaryContext.worldOffsetX != 212,
               "M-FRZ-C secondary worldOffsetX should not collapse onto primary legacy constant",
               &failures);
        Expect(primaryContext.worldOffsetX == 212,
               "M-FRZ-C primary worldOffsetX should stay 212",
               &failures);
        Expect(primaryContext.worldOffsetY == 0,
               "M-FRZ-C primary worldOffsetY should stay 0",
               &failures);
    }

    {
        for (const auto &sample : classicSpacedOutSamples) {
            GeyserSeedContext primaryContext;
            GeyserSeedContext secondaryContext;
            std::string errorMessage;
            PreviewWorldSession session;
            Expect(GeneratePreviewSessionForTest(sample.worldType,
                                                sample.seed,
                                                sample.mixing,
                                                &session,
                                                &errorMessage),
                   std::string(sample.expectedPrefix) +
                       " preview session should generate for authoritative offset checks",
                   &failures);
            Expect(ResolveGeyserSeedContext(sample.worldType,
                                            sample.seed,
                                            sample.mixing,
                                            Batch::PreviewTarget::Primary,
                                            &primaryContext,
                                            &errorMessage),
                   std::string(sample.expectedPrefix) +
                       " primary geyser context should resolve from generated summary offset",
                   &failures);
            Expect(primaryContext.worldOffsetX == 0,
                   std::string(sample.expectedPrefix) +
                       " primary worldOffsetX should stay 0 from generated summary offset",
                   &failures);
            Expect(primaryContext.worldOffsetY == 0,
                   std::string(sample.expectedPrefix) +
                       " primary worldOffsetY should stay 0 from generated summary offset",
                   &failures);
            errorMessage.clear();
            Expect(ResolveGeyserSeedContext(sample.worldType,
                                            sample.seed,
                                            sample.mixing,
                                            Batch::PreviewTarget::Secondary,
                                            &secondaryContext,
                                            &errorMessage),
                   std::string(sample.expectedPrefix) +
                       " secondary geyser context should resolve from preview summary",
                   &failures);
            Expect(session.secondaryPreview.has_value(),
                   std::string(sample.expectedPrefix) +
                       " secondary preview should exist for authoritative offset checks",
                   &failures);
            if (session.secondaryPreview.has_value()) {
                Expect(secondaryContext.worldOffsetX == session.secondaryPreview->summary.worldOffsetX,
                       std::string(sample.expectedPrefix) +
                           " secondary geyser context should match preview summary worldOffsetX",
                       &failures);
                Expect(secondaryContext.worldOffsetY == session.secondaryPreview->summary.worldOffsetY,
                       std::string(sample.expectedPrefix) +
                           " secondary geyser context should match preview summary worldOffsetY",
                       &failures);
            }
        }
    }

    {
        for (const auto &sample : vanillaStyleSpacedOutSamples) {
            GeyserSeedContext primaryContext;
            GeyserSeedContext secondaryContext;
            std::string errorMessage;
            PreviewWorldSession session;
            Expect(GeneratePreviewSessionForTest(sample.worldType,
                                                sample.seed,
                                                sample.mixing,
                                                &session,
                                                &errorMessage),
                   std::string(sample.expectedPrefix) +
                       " preview session should generate for authoritative offset checks",
                   &failures);
            Expect(ResolveGeyserSeedContext(sample.worldType,
                                            sample.seed,
                                            sample.mixing,
                                            Batch::PreviewTarget::Primary,
                                            &primaryContext,
                                            &errorMessage),
                   std::string(sample.expectedPrefix) +
                       " primary geyser context should resolve from generated summary offset",
                   &failures);
            Expect(primaryContext.worldOffsetX == 0,
                   std::string(sample.expectedPrefix) +
                       " primary worldOffsetX should stay 0 from generated summary offset",
                   &failures);
            Expect(primaryContext.worldOffsetY == 0,
                   std::string(sample.expectedPrefix) +
                       " primary worldOffsetY should stay 0 from generated summary offset",
                   &failures);
            errorMessage.clear();
            if (session.secondaryPreview.has_value()) {
                Expect(ResolveGeyserSeedContext(sample.worldType,
                                                sample.seed,
                                                sample.mixing,
                                                Batch::PreviewTarget::Secondary,
                                                &secondaryContext,
                                                &errorMessage),
                       std::string(sample.expectedPrefix) +
                           " secondary geyser context should resolve when preview session contains secondary",
                       &failures);
                Expect(secondaryContext.worldOffsetX == session.secondaryPreview->summary.worldOffsetX,
                       std::string(sample.expectedPrefix) +
                           " secondary geyser context should match preview summary worldOffsetX",
                       &failures);
                Expect(secondaryContext.worldOffsetY == session.secondaryPreview->summary.worldOffsetY,
                       std::string(sample.expectedPrefix) +
                           " secondary geyser context should match preview summary worldOffsetY",
                       &failures);
            } else {
                Expect(!ResolveGeyserSeedContext(sample.worldType,
                                                 sample.seed,
                                                 sample.mixing,
                                                 Batch::PreviewTarget::Secondary,
                                                 &secondaryContext,
                                                 &errorMessage),
                       std::string(sample.expectedPrefix) +
                           " secondary geyser context should fail when preview session has no secondary",
                       &failures);
                Expect(errorMessage.find("secondary preview is not available") != std::string::npos,
                       std::string(sample.expectedPrefix) +
                           " secondary geyser context should report missing secondary preview",
                       &failures);
            }
        }
    }

    {
        PreviewWorldSession session;
        std::string errorMessage;
        Expect(GeneratePreviewSessionForTest(34, 100123, 0, &session, &errorMessage),
               "M-FRZ-C preview session should generate",
               &failures);

        PreviewWorldContext primaryContext;
        PreviewWorldContext secondaryContext;
        Expect(ResolvePreviewWorldContext(session,
                                          Batch::PreviewTarget::Primary,
                                          &primaryContext,
                                          &errorMessage),
               "M-FRZ-C primary preview context should resolve",
               &failures);
        errorMessage.clear();
        Expect(ResolvePreviewWorldContext(session,
                                          Batch::PreviewTarget::Secondary,
                                          &secondaryContext,
                                          &errorMessage),
               "M-FRZ-C secondary preview context should resolve from preview summary",
               &failures);
        Expect(primaryContext.worldOffsetX == 212,
               "M-FRZ-C primary preview context should expose generated summary worldOffsetX",
               &failures);
        Expect(primaryContext.worldOffsetY == 0,
               "M-FRZ-C primary preview context should expose generated summary worldOffsetY",
               &failures);
        Expect(session.secondaryPreview.has_value(),
               "M-FRZ-C preview session should expose secondary preview for context checks",
               &failures);
        if (session.secondaryPreview.has_value()) {
            Expect(secondaryContext.worldOffsetX == session.secondaryPreview->summary.worldOffsetX,
                   "M-FRZ-C secondary preview context should match preview summary worldOffsetX",
                   &failures);
            Expect(secondaryContext.worldOffsetY == session.secondaryPreview->summary.worldOffsetY,
                   "M-FRZ-C secondary preview context should match preview summary worldOffsetY",
                   &failures);
        }

        const auto primaryDetails = GeyserCalc::BuildGeyserDetails(primaryContext.geyserSeed,
                                                                   primaryContext.summary.worldSize.y,
                                                                   primaryContext.summary.geysers,
                                                                   primaryContext.worldOffsetX,
                                                                   primaryContext.worldOffsetY);
        Expect(!primaryDetails.empty(),
               "M-FRZ-C primary geyser details should not be empty after restoring primary offset",
               &failures);
        Expect(primaryDetails.size() == primaryContext.summary.geysers.size(),
               "M-FRZ-C primary detail count should match summary geysers after restoring primary offset",
               &failures);
        const auto secondaryDetails = GeyserCalc::BuildGeyserDetails(secondaryContext.geyserSeed,
                                                                     secondaryContext.summary.worldSize.y,
                                                                     secondaryContext.summary.geysers,
                                                                     secondaryContext.worldOffsetX,
                                                                     secondaryContext.worldOffsetY);
        Expect(secondaryDetails.size() == secondaryContext.summary.geysers.size(),
               "M-FRZ-C secondary detail count should match summary geysers with authoritative offset",
               &failures);
    }

    {
        PreviewWorldSession session;
        std::string errorMessage;
        Expect(GeneratePreviewSessionForTest(39, 100123, 0, &session, &errorMessage),
               "V-AQU-C preview session should generate for geyser preview coordinate checks",
               &failures);
        std::vector<GeyserSummary> rawPrimaryGeysers;
        Vector2i primaryWorldSize{};
        Expect(BuildRawGeyserSummariesForTarget(39,
                                               100123,
                                               0,
                                               Batch::PreviewTarget::Primary,
                                               &rawPrimaryGeysers,
                                               &primaryWorldSize,
                                               &errorMessage),
               "V-AQU-C primary raw geysers should rebuild for preview coordinate checks",
               &failures);
        if (session.primaryPreview.has_value()) {
            const auto &previewGeysers = session.primaryPreview->summary.geysers;
            Expect(previewGeysers.size() == rawPrimaryGeysers.size(),
                   "V-AQU-C primary preview geyser count should match raw geyser count",
                   &failures);
            if (previewGeysers.size() == rawPrimaryGeysers.size()) {
                for (size_t index = 0; index < previewGeysers.size(); ++index) {
                    const auto &previewGeyser = previewGeysers[index];
                    const auto &rawGeyser = rawPrimaryGeysers[index];
                    Expect(previewGeyser.type == rawGeyser.type,
                           "V-AQU-C primary preview geyser type should match raw geyser type",
                           &failures);
                    Expect(previewGeyser.x == rawGeyser.x,
                           "V-AQU-C primary preview geyser x should stay equal to raw geyser x",
                           &failures);
                    Expect(previewGeyser.y == primaryWorldSize.y - rawGeyser.y,
                           "V-AQU-C primary preview geyser y should be flipped into preview coordinates",
                           &failures);
                }
            }
        }
        Expect(session.secondaryPreview.has_value(),
               "V-AQU-C preview session should expose secondary preview for geyser preview coordinate checks",
               &failures);
        std::vector<GeyserSummary> rawSecondaryGeysers;
        Vector2i secondaryWorldSize{};
        errorMessage.clear();
        Expect(BuildRawGeyserSummariesForTarget(39,
                                               100123,
                                               0,
                                               Batch::PreviewTarget::Secondary,
                                               &rawSecondaryGeysers,
                                               &secondaryWorldSize,
                                               &errorMessage),
               "V-AQU-C secondary raw geysers should rebuild for preview coordinate checks",
               &failures);
        if (session.secondaryPreview.has_value()) {
            const auto &previewGeysers = session.secondaryPreview->summary.geysers;
            Expect(previewGeysers.size() == rawSecondaryGeysers.size(),
                   "V-AQU-C secondary preview geyser count should match raw geyser count",
                   &failures);
            if (previewGeysers.size() == rawSecondaryGeysers.size()) {
                for (size_t index = 0; index < previewGeysers.size(); ++index) {
                    const auto &previewGeyser = previewGeysers[index];
                    const auto &rawGeyser = rawSecondaryGeysers[index];
                    Expect(previewGeyser.type == rawGeyser.type,
                           "V-AQU-C secondary preview geyser type should match raw geyser type",
                           &failures);
                    Expect(previewGeyser.x == rawGeyser.x,
                           "V-AQU-C secondary preview geyser x should stay equal to raw geyser x",
                           &failures);
                    Expect(previewGeyser.y == secondaryWorldSize.y - rawGeyser.y,
                           "V-AQU-C secondary preview geyser y should be flipped into preview coordinates",
                           &failures);
                }
            }
        }
    }

    {
        Batch::SidecarPreviewGeyserDetailsRequest request;
        request.jobId = "preview-geyser-details-rebuild-primary";
        request.worldType = 0;
        request.seed = 100123;
        request.mixing = 0;
        request.target = Batch::PreviewTarget::Primary;

        {
            std::lock_guard<std::mutex> lock(g_previewSessionMutex);
            g_previewSession.reset();
        }

        Json::Value root;
        const auto output = CaptureStdout([&]() { RunPreviewGeyserDetailsCommand(request); });
        Expect(ParseSingleJsonLine(output, &root, &failures,
                                   "preview_geyser_details primary output should be valid json"),
               "preview_geyser_details primary output should parse",
               &failures);
        Expect(root["event"].asString() == "preview_geyser_details",
               "preview_geyser_details primary should rebuild preview session instead of failing on missing cache",
               &failures);
    }

    {
        Batch::SidecarPreviewGeyserDetailsRequest request;
        request.jobId = "preview-geyser-details-rebuild-secondary";
        request.worldType = 27;
        request.seed = 100123;
        request.mixing = 0;
        request.target = Batch::PreviewTarget::Secondary;

        {
            std::lock_guard<std::mutex> lock(g_previewSessionMutex);
            g_previewSession.reset();
        }

        Json::Value root;
        const auto output = CaptureStdout([&]() { RunPreviewGeyserDetailsCommand(request); });
        Expect(ParseSingleJsonLine(output, &root, &failures,
                                   "preview_geyser_details secondary output should be valid json"),
               "preview_geyser_details secondary output should parse",
               &failures);
        Expect(root["event"].asString() == "preview_geyser_details",
               "preview_geyser_details secondary should rebuild preview session with authoritative offset",
               &failures);
        Expect(root["geyserDetails"].isArray(),
               "preview_geyser_details secondary should return geyser detail array after rebuilding session",
               &failures);
    }

    if (!failures.empty()) {
        for (const auto &failure : failures) {
            std::cerr << "[FAIL] " << failure << std::endl;
        }
        return 1;
    }

    std::cout << "[PASS] test_preview_world_session" << std::endl;
    return 0;
}
