#include "App/AppRuntime.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <mutex>
#include <ranges>
#include <stack>
#include <string_view>

#include <clipper.hpp>

#include "App/SettingsAsset.hpp"
#include "Geyser/GeyserCatalog.hpp"
#include "config.h"

#ifndef __EMSCRIPTEN__
static bool LoadSharedResourceBlob(std::vector<char> &data, std::string *errorMessage)
{
    std::ifstream file(ResolveSettingsAssetPath(), std::ios::binary);
    if (!file.is_open()) {
        if (errorMessage != nullptr) {
            *errorMessage = "failed to open shared asset blob";
        }
        return false;
    }
    file.seekg(0, std::ios::end);
    const std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    if (size <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = "asset blob size is invalid";
        }
        return false;
    }
    data.resize((size_t)size);
    if (!file.read(data.data(), size)) {
        if (errorMessage != nullptr) {
            *errorMessage = "failed to read shared asset blob";
        }
        return false;
    }
    return true;
}
#endif

AppRuntime *AppRuntime::Instance()
{
#ifdef __EMSCRIPTEN__
    static AppRuntime inst;
#else
    thread_local AppRuntime inst;
#endif
    return &inst;
}

namespace {

std::string ReadEnvironmentVariable(const char *name)
{
    if (name == nullptr || name[0] == '\0') {
        return {};
    }
    char *value = nullptr;
    size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr || length == 0) {
        if (value != nullptr) {
            free(value);
        }
        return {};
    }
    std::string result(value);
    free(value);
    return result;
}

std::string_view ResolveWorldgenGeyserCatalogKey(int worldgenType)
{
    static constexpr std::string_view kWorldgenTypes[] = {
        "steam", "hot_steam", "hot_water", "slush_water", "filthy_water",
        "slush_salt_water", "salt_water", "small_volcano", "big_volcano",
        "liquid_co2", "hot_co2", "hot_hydrogen", "hot_po2", "slimy_po2",
        "chlorine_gas", "methane", "molten_copper", "molten_iron",
        "molten_gold", "molten_aluminum", "molten_cobalt", "oil_drip",
        "liquid_sulfur", "chlorine_gas_cool", "molten_tungsten",
        "molten_niobium", "murky_brine", "OilWell", "SmallReefGeyser",
        "UnderwaterVent", "receiver", "sender", "teleporter", "cryopod",
        "printpod",
    };
    if (worldgenType < 0 ||
        worldgenType >= static_cast<int>(std::size(kWorldgenTypes))) {
        return {};
    }
    switch (worldgenType) {
    case 27:
        return "oil_reservoir";
    case 28:
        return "small_reef_geyser";
    case 29:
        return "underwater_vent";
    case 30:
        return "warp_receiver";
    case 31:
        return "warp_sender";
    case 32:
        return "warp_portal";
    case 33:
        return "cryo_tank";
    case 34:
        return "printing_pod";
    default:
        return kWorldgenTypes[static_cast<size_t>(worldgenType)];
    }
}

int ResolveCatalogGeyserType(int worldgenType)
{
    const auto key = ResolveWorldgenGeyserCatalogKey(worldgenType);
    if (key.empty()) {
        return worldgenType;
    }
    const int mapped = Geyser::FindIdByKey(key);
    return mapped >= 0 ? mapped : worldgenType;
}

bool ShouldIncludeInAuthoritativeSummary(int worldgenType)
{
    switch (worldgenType) {
    case 33: // cryopod
    case 34: // printpod
        return false;
    default:
        return true;
    }
}

} // namespace

void AppRuntime::SetResultSink(ResultSink *sink)
{
    m_sink = sink;
}

ResultSink *AppRuntime::GetResultSink() const
{
    return m_sink;
}

void AppRuntime::SetSkipPolygons(bool skip)
{
    m_skipPolygons = skip;
}

bool AppRuntime::IsSkippingPolygons() const
{
    return m_skipPolygons;
}

void AppRuntime::Initialize(int seed)
{
#ifndef __EMSCRIPTEN__
    std::string sharedError;
    const auto shared = SharedSettingsCache::GetOrCreate(LoadSharedResourceBlob, &sharedError);
    if (shared != nullptr) {
        m_settings = *shared;
        m_random = KRandom(seed);
        return;
    }
    if (!sharedError.empty()) {
        LogE("load shared settings cache failed: %s", sharedError.c_str());
    }
#endif

    if (m_sink == nullptr) {
        LogE("result sink is not set before Initialize()");
        return;
    }

    std::vector<char> data;
    if (!m_sink->RequestResource(SETTING_ASSET_FILESIZE, data)) {
        LogE("request resource failed");
        return;
    }
    if (data.size() != SETTING_ASSET_FILESIZE) {
        LogE("resource size mismatch, expect=%u actual=%zu",
             SETTING_ASSET_FILESIZE,
             data.size());
        return;
    }

    std::string_view content(data.data(), data.size());
    if (!m_settings.LoadSettingsCache(content)) {
        LogE("load settings cache failed");
    }
    m_random = KRandom(seed);
}

bool AppRuntime::PrepareSearchWorker(const std::string &code)
{
    Initialize(0);
    if (!m_settings.CoordinateChanged(code, m_settings)) {
        LogE("parse seed code %s failed during PrepareSearchWorker().", code.c_str());
        m_searchWorkerPrepared = false;
        m_searchSeedPrepared = false;
        m_searchWarpWorld = false;
        return false;
    }
    m_searchMutableBaseline = m_settings.CaptureSearchMutableState();
    m_searchWorkerPrepared = true;
    m_searchSeedPrepared = false;
    m_searchWarpWorld = false;
    return true;
}

bool AppRuntime::ResetSearchSeed(const std::string &code)
{
    if (!m_searchWorkerPrepared) {
        LogE("search worker is not prepared before ResetSearchSeed()");
        return false;
    }
    m_settings.RestoreSearchMutableState(m_searchMutableBaseline);
    m_random = KRandom(0);
    if (!m_settings.CoordinateChanged(code, m_settings)) {
        LogE("parse seed code %s failed.", code.c_str());
        return false;
    }
    m_searchSeedPrepared = true;
    m_searchWarpWorld = ShouldGenerateWarpWorldForCode(code);
    return true;
}

bool AppRuntime::GeneratePrepared(int traitsFlag)
{
    if (m_sink == nullptr) {
        LogE("result sink is not set before GeneratePrepared()");
        return false;
    }
    if (!m_searchWorkerPrepared || !m_searchSeedPrepared) {
        LogE("search seed is not prepared before GeneratePrepared()");
        return false;
    }
    const bool generated = GenerateCurrentState(traitsFlag, m_searchWarpWorld);
    m_searchSeedPrepared = false;
    return generated;
}

bool AppRuntime::Generate(const std::string &code, int traitsFlag)
{
    if (m_sink == nullptr) {
        LogE("result sink is not set before Generate()");
        return false;
    }
    if (!m_settings.CoordinateChanged(code, m_settings)) {
        LogE("parse seed code %s failed.", code.c_str());
        return false;
    }
    const bool shouldPreviewWarpWorld = ShouldGenerateWarpWorldForCode(code);
    return GenerateCurrentState(traitsFlag, shouldPreviewWarpWorld);
}

bool AppRuntime::ShouldGenerateWarpWorldForCode(const std::string &code)
{
    // DLC5 经典/太空风格主世界同样存在唯一 warp 副世界，批量链路必须与预览链路保持一致。
    return code.find("M-") == 0 || m_settings.IsContentEnabled("DLC5_ID");
}

bool AppRuntime::GenerateSelectedPlacements(const std::string &code,
                                            int traitsFlag,
                                            const std::vector<int> &placementIndexes,
                                            int primaryPlacementIndex)
{
    if (m_sink == nullptr) {
        LogE("result sink is not set before GenerateSelectedPlacements()");
        return false;
    }
    if (!m_settings.CoordinateChanged(code, m_settings)) {
        LogE("parse seed code %s failed.", code.c_str());
        return false;
    }

    std::vector<ResolvedWorldPlacement> placements;
    if (!BuildWorldList(placements)) {
        return false;
    }
    return GenerateWorldsForPlacementIndexes(placements,
                                             traitsFlag,
                                             placementIndexes,
                                             primaryPlacementIndex);
}

bool AppRuntime::BuildWorldList(std::vector<ResolvedWorldPlacement> &placements)
{
    std::string errorMessage;
    if (!BuildResolvedWorldPlacements(m_settings, &placements, &errorMessage)) {
        LogE("BuildWorldList failed: %s", errorMessage.c_str());
        return false;
    }
    return true;
}

bool AppRuntime::GenerateCurrentState(int traitsFlag, bool genWarpWorld)
{
    std::vector<ResolvedWorldPlacement> placements;
    if (!BuildWorldList(placements)) {
        return false;
    }
    const auto placementIndexes = CollectPreviewPlacementIndexes(placements, genWarpWorld);
    return GenerateWorldsForPlacementIndexes(placements,
                                             traitsFlag,
                                             placementIndexes,
                                             FindPrimaryPlacementIndex(placements));
}

int AppRuntime::FindPrimaryPlacementIndex(const std::vector<ResolvedWorldPlacement> &placements)
{
    for (const auto &placement : placements) {
        if (placement.sourceWorld != nullptr &&
            placement.sourceWorld->locationType == LocationType::StartWorld) {
            return placement.placementIndex;
        }
    }
    return -1;
}

std::vector<int> AppRuntime::CollectPreviewPlacementIndexes(
    const std::vector<ResolvedWorldPlacement> &placements,
                                                            bool genWarpWorld)
{
    std::vector<int> placementIndexes;
    placementIndexes.reserve(placements.size());
    for (const auto &placement : placements) {
        const auto *world = placement.sourceWorld;
        if (world == nullptr || world->locationType == LocationType::Cluster) {
            continue;
        }
        if (world->locationType == LocationType::StartWorld) {
            placementIndexes.push_back(placement.placementIndex);
            continue;
        }
        if (genWarpWorld && world->startingBaseTemplate.contains("::bases/warpworld")) {
            placementIndexes.push_back(placement.placementIndex);
        }
    }
    return placementIndexes;
}

bool AppRuntime::GenerateWorldsForPlacementIndexes(std::vector<ResolvedWorldPlacement> &placements,
                                                   int traitsFlag,
                                                   const std::vector<int> &placementIndexes,
                                                   int primaryPlacementIndex)
{
    std::string errorMessage;
    std::vector<ClusterWorldOffset> worldOffsets;
    if (!ComputeClusterWorldOffsets(placements, &worldOffsets, &errorMessage)) {
        LogE("ComputeClusterWorldOffsets failed: %s", errorMessage.c_str());
        return false;
    }

    std::vector<World *> activeWorlds;
    if (!m_settings.InitializeWorlds(activeWorlds)) {
        LogE("InitializeWorlds failed.");
        return false;
    }
    if (activeWorlds.size() != placements.size()) {
        LogE("active world count mismatch, placements=%zu worlds=%zu",
             placements.size(),
             activeWorlds.size());
        return false;
    }
    for (size_t i = 0; i < placements.size(); ++i) {
        auto *world = activeWorlds[i];
        if (world == nullptr) {
            LogE("world source at placement index %zu is null.", i);
            return false;
        }
        world->ClearMixingsAndTraits();
        placements[i].sourceWorld = world;
    }

    if (traitsFlag != 0) {
        m_settings.SetSeedWithTraits(activeWorlds, traitsFlag, m_random);
    }

    int seed = m_settings.seed;
    std::vector<std::vector<const WorldTrait *>> randomTraitsByPlacement(placements.size());
    for (size_t i = 0; i < placements.size(); ++i) {
        const auto &placement = placements[i];
        auto *world = activeWorlds[i];
        if (world->locationType == LocationType::Cluster) {
            continue;
        }

        auto &randomTraits = randomTraitsByPlacement[static_cast<size_t>(placement.placementIndex)];
        randomTraits = m_settings.GetRandomTraits(*world, seed + static_cast<int>(i));
        for (const auto *trait : randomTraits) {
            if (trait != nullptr) {
                world->ApplayTraits(*trait, m_settings);
            }
        }
    }
    m_settings.seed = seed;
    m_settings.DoSubworldMixing(activeWorlds, false);

    std::vector<int> normalizedPlacementIndexes;
    normalizedPlacementIndexes.reserve(placementIndexes.size());
    for (const int placementIndex : placementIndexes) {
        if (placementIndex < 0 || placementIndex >= static_cast<int>(placements.size())) {
            LogE("placement index %d is out of range.", placementIndex);
            return false;
        }
        if (std::ranges::contains(normalizedPlacementIndexes, placementIndex)) {
            continue;
        }
        normalizedPlacementIndexes.push_back(placementIndex);
    }

    for (const int placementIndex : normalizedPlacementIndexes) {
        if (placementIndex < 0 || placementIndex >= static_cast<int>(placements.size())) {
            LogE("placement index %d is out of range during world generation.", placementIndex);
            return false;
        }
        auto &placement = placements[static_cast<size_t>(placementIndex)];
        auto *world = placement.sourceWorld;
        if (world == nullptr) {
            LogE("world source at placement index %d is null.", placementIndex);
            return false;
        }
        if (world->locationType == LocationType::Cluster) {
            LogE("placement index %d points to cluster-only world.", placementIndex);
            return false;
        }

        m_settings.seed = seed + placementIndex;
        WorldGen worldGen(*world, m_settings, seed + placementIndex);
        std::vector<Site> sites;
        if (!worldGen.GenerateOverworld(sites)) {
            LogE("generate overworld failed.");
            return false;
        }
        if (sites.empty()) {
            LogE("generate overworld produced empty sites.");
            return false;
        }
        const std::string dumpPrefix = ReadEnvironmentVariable("ONI_DEBUG_WORLD_DUMP");
        if (!dumpPrefix.empty()) {
            worldGen.DumpDebugWorldState(
                sites,
                dumpPrefix + "-" + std::to_string(placementIndex) + ".json");
        }

        const auto *worldOffset = FindClusterWorldOffset(worldOffsets, placementIndex);
        if (worldOffset == nullptr) {
            LogE("cluster world offset at placement index %d is missing.", placementIndex);
            return false;
        }
        WorldEffectiveState summaryState;
        summaryState.placementIndex = placementIndex;
        summaryState.worldAssetId = placement.worldAssetId;
        summaryState.randomTraits =
            randomTraitsByPlacement[static_cast<size_t>(placementIndex)];
        summaryState.world = *world;
        auto summary = BuildSummary(seed, summaryState, *worldOffset, sites, worldGen);
        summary.isPrimary = primaryPlacementIndex >= 0
                                ? placementIndex == primaryPlacementIndex
                                : world->locationType == LocationType::StartWorld;
        summary.worldType = summary.isPrimary ? 0 : 1;
        summary.hasSecondaryPreview = normalizedPlacementIndexes.size() > 1;
        m_sink->OnGeneratedWorldSummary(summary);
        if (!m_skipPolygons) {
            auto preview = BuildPreview(world, sites, summary);
            m_sink->OnGeneratedWorldPreview(preview);
        }
    }
    m_settings.seed = seed;
    return true;
}

void AppRuntime::SetSeedWithTraits(const std::vector<World *> &worlds, int traitsFlag)
{
    m_settings.SetSeedWithTraits(worlds, traitsFlag, m_random);
}

GeneratedWorldSummary AppRuntime::BuildSummary(int seed,
                                               const WorldEffectiveState &state,
                                               const ClusterWorldOffset &worldOffset,
                                               std::vector<Site> &sites,
                                               const WorldGen &worldGen)
{
    GeneratedWorldSummary summary;
    World *world = const_cast<World *>(&state.world);
    summary.seed = seed;
    summary.geyserSeed = seed + (int)m_settings.cluster->worldPlacements.size() - 1;
    summary.isPrimary = world->locationType == LocationType::StartWorld;
    summary.worldType = summary.isPrimary ? 0 : 1;
    summary.worldPlacementIndex = state.placementIndex;
    summary.worldAssetId = state.worldAssetId;
    summary.start = {sites[0].x, sites[0].y};
    summary.worldSize = world->worldsize;
    summary.start.y = summary.worldSize.y - summary.start.y;
    summary.worldOffsetX = worldOffset.offset.x;
    summary.worldOffsetY = worldOffset.offset.y;

    summary.traits.reserve(state.randomTraits.size());
    for (auto &item : state.randomTraits) {
        uint32_t index = 0;
        for (auto &pair : m_settings.traits) {
            if (item == &pair.second) {
                summary.traits.push_back({(int)index});
                break;
            }
            ++index;
        }
    }

    auto geysers = worldGen.GetGeysers(summary.geyserSeed);
    summary.geysers.reserve(geysers.size());
    for (auto &item : geysers) {
        if (!ShouldIncludeInAuthoritativeSummary(item.z)) {
            continue;
        }
        const int geyserType = ResolveCatalogGeyserType(item.z);
        summary.geysers.push_back({
            geyserType,
            item.x,
            summary.worldSize.y - item.y,
            item.x,
            summary.worldSize.y - item.y,
        });
    }

    return summary;
}

GeneratedWorldPreview AppRuntime::BuildPreview(World *world,
                                               std::vector<Site> &sites,
                                               const GeneratedWorldSummary &summary)
{
    GeneratedWorldPreview preview;
    preview.summary = summary;

    std::ranges::for_each(sites, [](Site &site) { site.visited = false; });
    for (auto &item : sites) {
        if (item.visited) {
            continue;
        }

        Polygon polygon;
        PolygonSummary polygonSummary;
        polygonSummary.hasHole = GetZonePolygon(item, polygon);
        polygonSummary.zoneType = (int)item.subworld->zoneType;
        polygonSummary.vertices.reserve(polygon.Vertices.size());
        for (auto &vex : polygon.Vertices) {
            polygonSummary.vertices.push_back({
                (int)vex.x,
                world->worldsize.y - (int)vex.y,
            });
        }
        preview.polygons.push_back(std::move(polygonSummary));
    }

    return preview;
}

bool AppRuntime::GetZonePolygon(Site &site, Polygon &polygon)
{
    ZoneType zoneType = site.subworld->zoneType;
    ClipperLib::Clipper clipper;
    std::stack<Site *> stack;
    stack.push(&site);
    while (!stack.empty()) {
        auto top = stack.top();
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
        for (auto neighbour : top->neighbours) {
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
