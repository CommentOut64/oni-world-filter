#include "App/AppRuntime.hpp"

#include <algorithm>
#include <fstream>
#include <memory>
#include <mutex>
#include <ranges>
#include <stack>
#include <string_view>

#include <clipper.hpp>

#include "config.h"

#ifndef __EMSCRIPTEN__
static bool LoadSharedResourceBlob(std::vector<char> &data, std::string *errorMessage)
{
    std::ifstream file(SETTING_ASSET_FILEPATH, std::ios::binary);
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
    m_searchWarpWorld = code.find("M-") == 0;
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
    return GenerateCurrentState(traitsFlag, code.find("M-") == 0);
}

bool AppRuntime::BuildWorldList(std::vector<World *> &worlds)
{
    worlds.clear();
    if (m_settings.cluster == nullptr) {
        LogE("cluster is null before BuildWorldList()");
        return false;
    }
    for (auto &worldPlacement : m_settings.cluster->worldPlacements) {
        auto itr = m_settings.worlds.find(worldPlacement.world);
        if (itr == m_settings.worlds.end()) {
            LogE("world %s was wrong.", worldPlacement.world.c_str());
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

bool AppRuntime::GenerateCurrentState(int traitsFlag, bool genWarpWorld)
{
    std::vector<World *> worlds;
    if (!BuildWorldList(worlds)) {
        return false;
    }
    if (traitsFlag != 0) {
        SetSeedWithTraits(worlds, traitsFlag);
    }

    m_settings.DoSubworldMixing(worlds);
    int seed = m_settings.seed;
    for (size_t i = 0; i < worlds.size(); ++i) {
        auto world = worlds[i];
        if (world->locationType == LocationType::Cluster) {
            continue;
        } else if (world->locationType == LocationType::StartWorld) {
            // keep start world path
        } else if (!world->startingBaseTemplate.contains("::bases/warpworld")) {
            continue;
        } else if (!genWarpWorld) {
            continue;
        }

        m_settings.seed = seed + (int)i;
        auto traits = m_settings.GetRandomTraits(*world);
        for (auto trait : traits) {
            world->ApplayTraits(*trait, m_settings);
        }

        WorldGen worldGen(*world, m_settings);
        std::vector<Site> sites;
        if (!worldGen.GenerateOverworld(sites)) {
            LogE("generate overworld failed.");
            return false;
        }
        if (sites.empty()) {
            LogE("generate overworld produced empty sites.");
            return false;
        }

        auto summary = BuildSummary(seed, world, sites, traits, worldGen);
        m_sink->OnGeneratedWorldSummary(summary);
        if (!m_skipPolygons) {
            auto preview = BuildPreview(world, sites, summary);
            m_sink->OnGeneratedWorldPreview(preview);
        }
    }
    return true;
}

void AppRuntime::SetSeedWithTraits(const std::vector<World *> &worlds, int traitsFlag)
{
    std::vector<const WorldTrait *> presets;
    int index = 0;
    for (auto &pair : m_settings.traits) {
        if ((traitsFlag >> index & 1) == 1) {
            presets.push_back(&pair.second);
        }
        ++index;
    }
    if (presets.empty()) {
        m_settings.seed = m_random.Next();
        return;
    }

    index = 0;
    World *world = worlds[index];
    for (size_t i = 0; i < worlds.size(); ++i) {
        world = worlds[i];
        if (world->locationType == LocationType::StartWorld) {
            index = (int)i;
            break;
        }
    }

    size_t maxCount = 0;
    int maxCountSeed = 0;
    for (int i = 0; i < 1000; ++i) {
        int seed = m_random.Next();
        m_settings.seed = seed + index;
        auto traits = m_settings.GetRandomTraits(*world);
        m_settings.seed = seed;
        size_t count = 0;
        for (auto *preset : presets) {
            if (std::ranges::contains(traits, preset)) {
                ++count;
            }
        }
        if (count == presets.size()) {
            return;
        } else if (maxCount < count) {
            maxCount = count;
            maxCountSeed = seed;
        }
    }

    m_settings.seed = maxCountSeed;
    LogI("can not find seed for preset traits");
}

GeneratedWorldSummary AppRuntime::BuildSummary(int seed,
                                               World *world,
                                               std::vector<Site> &sites,
                                               const std::vector<const WorldTrait *> &traits,
                                               const WorldGen &worldGen)
{
    GeneratedWorldSummary summary;
    summary.seed = seed;
    summary.worldType = (world->locationType == LocationType::StartWorld) ? 0 : 1;
    summary.start = {sites[0].x, sites[0].y};
    summary.worldSize = world->worldsize;
    summary.start.y = summary.worldSize.y - summary.start.y;

    summary.traits.reserve(traits.size());
    for (auto &item : traits) {
        uint32_t index = 0;
        for (auto &pair : m_settings.traits) {
            if (item == &pair.second) {
                summary.traits.push_back({(int)index});
                break;
            }
            ++index;
        }
    }

    int geyserSeed = seed + (int)m_settings.cluster->worldPlacements.size() - 1;
    auto geysers = worldGen.GetGeysers(geyserSeed);
    summary.geysers.reserve(geysers.size());
    for (auto &item : geysers) {
        summary.geysers.push_back({item.z, item.x, item.y});
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
