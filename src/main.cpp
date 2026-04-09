#ifdef EMSCRIPTEN
#include <emscripten.h>
#else
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <thread>
#include <mutex>
#include <atomic>
#include <json/json.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#include <windows.h>
#endif
#define EMSCRIPTEN_KEEPALIVE
#endif

#include <stack>
#include <ranges>
#include <algorithm>

#include <clipper.hpp>

#include "config.h"
#include "Setting/SettingsCache.hpp"
#include "WorldGen.hpp"

// I defined only one function for exchanging data between c++ and js,
// it get resource from js and set result to js.
extern "C" void jsExchangeData(uint32_t type, uint32_t count, size_t data);

enum ResultType {
    RT_Starting,
    RT_Trait,
    RT_Geyser,
    RT_Polygon,
    RT_WorldSize,
    RT_Resource
};

// for debug
void WriteToBinary(const std::vector<Site> &sites)
{
    static int index = 10;
    std::vector<uint32_t> data;
    for (auto &site : sites) {
        data.push_back(site.idx);
        data.push_back(*(uint32_t *)&site.x);
        data.push_back(*(uint32_t *)&site.y);
        int count = (int)site.polygon.Vertices.size();
        if (count != 0) {
            data.push_back(count);
            for (auto &point : site.polygon.Vertices) {
                data.push_back(*(uint32_t *)&point.x);
                data.push_back(*(uint32_t *)&point.y);
            }
        }
        if (site.children && !site.children->empty()) {
            for (auto &child : *site.children) {
                data.push_back(child.idx);
                data.push_back(*(uint32_t *)&child.x);
                data.push_back(*(uint32_t *)&child.y);
                int count2 = (int)child.polygon.Vertices.size();
                if (count2 != 0) {
                    data.push_back(count2);
                    for (auto &point : child.polygon.Vertices) {
                        data.push_back(*(uint32_t *)&point.x);
                        data.push_back(*(uint32_t *)&point.y);
                    }
                }
            }
        }
    }
    jsExchangeData(index++, (uint32_t)data.size(), (size_t)data.data());
}

class App
{
private:
    SettingsCache m_settings;
    KRandom m_random{0};

    App() = default;

public:
    bool skipPolygons = false; // 批量模式跳过多边形计算
    static App *Instance()
    {
#ifdef EMSCRIPTEN
        static App inst;
#else
        // 每个线程拥有独立实例，支持多线程并行生成
        thread_local App inst;
#endif
        return &inst;
    }

    void Initialize(int seed)
    {
        uint32_t count = SETTING_ASSET_FILESIZE;
        auto data = std::make_unique<char[]>(count);
        jsExchangeData(RT_Resource, count, (size_t)data.get());
        std::string_view content(data.get(), count);
        m_settings.LoadSettingsCache(content);
        m_random = KRandom(seed);
    }

    bool Generate(const std::string &code, int traits);
    void SetSeedWithTraits(const std::vector<World *> &worlds, int traitsFlag);
    void SetResultWorldInfo(int seed, World *world, std::vector<Site> &sites);
    void SetResultTraits(const std::vector<const WorldTrait *> &traits);
    void SetResultGeysers(int seed, const WorldGen &worldGen);
    void SetResultPolygons(World *world, std::vector<Site> &sites);
    // union sites with the same zone type. if result has hole return true.
    static bool GetZonePolygon(Site &site, Polygon &polygon);
};

bool App::Generate(const std::string &code, int traitsFlag)
{
    if (!m_settings.CoordinateChanged(code, m_settings)) {
        LogE("parse seed code %s failed.", code.c_str());
        return false;
    }
    std::vector<World *> worlds;
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
    if (traitsFlag != 0) { // roll seed for preset traits
        SetSeedWithTraits(worlds, traitsFlag);
    }
    m_settings.DoSubworldMixing(worlds);
    int seed = m_settings.seed;
    bool genWarpWorld = code.find("M-") == 0;
    for (size_t i = 0; i < worlds.size(); ++i) {
        auto world = worlds[i];
        if (world->locationType == LocationType::Cluster) {
            continue;
        } else if (world->locationType == LocationType::StartWorld) {
            // go on;
        } else if (!world->startingBaseTemplate.contains("::bases/warpworld")) {
            continue; // other inner cluster
        } else if (!genWarpWorld) {
            continue;
        }
        m_settings.seed = seed + i;
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
        SetResultWorldInfo(seed, world, sites);
        SetResultTraits(traits);
        SetResultGeysers(seed, worldGen);
        if (!skipPolygons)
            SetResultPolygons(world, sites);
    }
    return true;
}

void App::SetSeedWithTraits(const std::vector<World *> &worlds, int traitsFlag)
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
            index = i;
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

void App::SetResultWorldInfo(int seed, World *world, std::vector<Site> &sites)
{
    Vector2i starting = {sites[0].x, sites[0].y};
    Vector2i worldSize = world->worldsize;
    starting.y = worldSize.y - starting.y;
    int worldType = (world->locationType == LocationType::StartWorld) ? 0 : 1;
    jsExchangeData(RT_Starting, worldType, (size_t)&starting);
    jsExchangeData(RT_WorldSize, seed, (size_t)&worldSize);
}

void App::SetResultTraits(const std::vector<const WorldTrait *> &traits)
{
    std::vector<int> result;
    result.reserve(traits.size());
    for (auto &item : traits) {
        uint32_t index = 0;
        for (auto &pair : m_settings.traits) {
            if (item == &pair.second) {
                result.push_back(index);
                break;
            } else {
                index++;
            }
        }
    }
    jsExchangeData(RT_Trait, (uint32_t)result.size(), (size_t)result.data());
}

void App::SetResultGeysers(int seed, const WorldGen &worldGen)
{
    seed += (int)m_settings.cluster->worldPlacements.size() - 1;
    auto geysers = worldGen.GetGeysers(seed);
    std::vector<int> result;
    result.reserve(geysers.size() * 3);
    for (auto &item : geysers) {
        result.insert(result.end(), {item.z, item.x, item.y}); // z is type
    }
    jsExchangeData(RT_Geyser, (uint32_t)result.size(), (size_t)result.data());
}

void App::SetResultPolygons(World *world, std::vector<Site> &sites)
{
    std::vector<int> result;
    std::ranges::for_each(sites, [](Site &site) { site.visited = false; });
    for (auto &item : sites) {
        if (item.visited) {
            continue;
        }
        Polygon polygon;
        bool hasHole = GetZonePolygon(item, polygon);
        result.push_back(hasHole ? 1 : 0);
        result.push_back((int)item.subworld->zoneType);
        result.push_back((int)polygon.Vertices.size());
        for (auto &vex : polygon.Vertices) {
            result.push_back(vex.x);
            result.push_back(world->worldsize.y - vex.y);
        }
    }
    jsExchangeData(RT_Polygon, (uint32_t)result.size(), (size_t)result.data());
}

bool App::GetZonePolygon(Site &site, Polygon &polygon)
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

extern "C" void EMSCRIPTEN_KEEPALIVE app_init(int seed)
{
    App::Instance()->Initialize(seed);
}

#ifndef EMSCRIPTEN
static bool g_batchMode = false;
#endif

extern "C" bool EMSCRIPTEN_KEEPALIVE app_generate(int type, int seed, int mix)
{
    const char *worlds[] = {
        "SNDST-A-",  "OCAN-A-",    "S-FRZ-",     "LUSH-A-",    "FRST-A-",
        "VOLCA-",    "BAD-A-",     "HTFST-A-",   "OASIS-A-",   "CER-A-",
        "CERS-A-",   "PRE-A-",     "PRES-A-",    "V-SNDST-C-", "V-OCAN-C-",
        "V-SWMP-C-", "V-SFRZ-C-",  "V-LUSH-C-",  "V-FRST-C-",  "V-VOLCA-C-",
        "V-BAD-C-",  "V-HTFST-C-", "V-OASIS-C-", "V-CER-C-",   "V-CERS-C-",
        "V-PRE-C-",  "V-PRES-C-",  "SNDST-C-",   "PRE-C-",     "CER-C-",
        "FRST-C-",   "SWMP-C-",    "M-SWMP-C-",  "M-BAD-C-",   "M-FRZ-C-",
        "M-FLIP-C-", "M-RAD-C-",   "M-CERS-C-"};
    if (type < 0 || (int)std::size(worlds) <= type) {
        return false;
    }
    std::string code = worlds[type];
    int traits = 0;
    if (seed < 0) {
        traits = -seed;
        seed = 0;
    }
    code += std::to_string(seed);
    code += "-0-D3-";
    code += SettingsCache::BinaryToBase36(mix);
#ifndef EMSCRIPTEN
    if (!g_batchMode)
#endif
        LogI("generate with code: %s", code.c_str());
    return App::Instance()->Generate(code, traits);
}

#ifndef EMSCRIPTEN

// -- 喷口标识 (英文 ID，用于 filter.json 匹配) --
static const char *g_geyserIds[] = {
    "steam", "hot_steam", "hot_water", "slush_water", "filthy_water",
    "slush_salt_water", "salt_water", "small_volcano", "big_volcano",
    "liquid_co2", "hot_co2", "hot_hydrogen", "hot_po2", "slimy_po2",
    "chlorine_gas", "methane", "molten_copper", "molten_iron",
    "molten_gold", "molten_aluminum", "molten_cobalt",
    "oil_drip", "liquid_sulfur", "chlorine_gas_cool",
    "molten_tungsten", "molten_niobium",
    "printing_pod", "oil_reservoir", "warp_sender", "warp_receiver",
    "warp_portal", "cryo_tank"};

// -- 喷口中文名 (显示用) --
static const char *g_geyserNames[] = {
    "低温蒸汽喷孔", "蒸汽喷孔",     "清水泉",       "低温泥浆泉",
    "污水泉",       "低温盐泥泉",   "盐水泉",       "小型火山",
    "火山",         "二氧化碳泉",   "二氧化碳喷孔", "氢气喷孔",
    "高温污氧喷孔", "含菌污氧喷孔", "氯气喷孔",     "天然气喷孔",
    "铜火山",       "铁火山",       "金火山",       "铝火山",
    "钴火山",       "渗油裂缝",     "液硫泉",       "冷氯喷孔",
    "钨火山",       "铌火山",       "打印舱",       "储油石",
    "输出端",       "输入端",       "传送器",       "低温箱"};

// -- 特性中文名 --
static const char *g_traitNames[] = {
    "坠毁的卫星群", "冰封之友",   "不规则的原油区",   "繁茂核心",
    "金属洞穴",     "放射性地壳", "地下海洋",         "火山活跃",
    "大型石块",     "中型石块",   "混合型石块",       "小型石块",
    "被圈闭的原油", "冰冻核心",   "活跃性地质",       "晶洞",
    "休眠性地质",   "大型冰川",   "不规则的原油区",   "岩浆通道",
    "金属贫瘠",     "金属富足",   "备选的打印舱位置", "粘液菌团",
    "地下海洋",     "火山活跃"};

static constexpr int GEYSER_TYPE_COUNT = 32;

static int GeyserIdToIndex(const std::string &id)
{
    for (int i = 0; i < GEYSER_TYPE_COUNT; ++i)
        if (id == g_geyserIds[i])
            return i;
    return -1;
}

// ============================================================
//  批量模式 — 数据捕获
// ============================================================
struct BatchCapture {
    bool active = false;
    int startX = 0, startY = 0; // 出生点坐标 (y 已翻转，用于显示)
    int worldW = 0, worldH = 0; // 世界尺寸
    struct Geyser {
        int type, x, y;
    };
    std::vector<Geyser> geysers;
    std::vector<int> traits;

    void Reset()
    {
        geysers.clear();
        traits.clear();
        startX = startY = worldW = worldH = 0;
    }
};
static thread_local BatchCapture g_batch;

// ============================================================
//  筛选规则
// ============================================================
struct FilterConfig {
    int worldType = 0;
    int seedStart = 1;
    int seedEnd = 100000;
    int mixing = 0;
    int threads = 0; // 0 = 自动 (hardware_concurrency)
    std::vector<int> required;
    std::vector<int> forbidden;
    struct DistRule {
        int type;
        float minDist = 0;
        float maxDist = 1e9f;
    };
    std::vector<DistRule> distanceRules;
};

static bool LoadFilter(const std::string &path, FilterConfig &cfg)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        LogE("cannot open filter file: %s", path.c_str());
        return false;
    }
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;
    if (!Json::parseFromStream(builder, file, &root, &errs)) {
        LogE("parse filter json failed: %s", errs.c_str());
        return false;
    }

    cfg.worldType = root.get("worldType", 0).asInt();
    cfg.seedStart = root.get("seedStart", 1).asInt();
    cfg.seedEnd = root.get("seedEnd", 100000).asInt();
    cfg.mixing = root.get("mixing", 0).asInt();
    cfg.threads = root.get("threads", 0).asInt();

    for (const auto &v : root["required"]) {
        int i = GeyserIdToIndex(v.asString());
        if (i >= 0)
            cfg.required.push_back(i);
        else
            LogE("unknown geyser id in required: %s", v.asCString());
    }
    for (const auto &v : root["forbidden"]) {
        int i = GeyserIdToIndex(v.asString());
        if (i >= 0)
            cfg.forbidden.push_back(i);
        else
            LogE("unknown geyser id in forbidden: %s", v.asCString());
    }
    for (const auto &v : root["distance"]) {
        FilterConfig::DistRule rule;
        rule.type = GeyserIdToIndex(v["geyser"].asString());
        rule.minDist = v.get("minDist", 0).asFloat();
        rule.maxDist = v.get("maxDist", 1e9f).asFloat();
        if (rule.type >= 0)
            cfg.distanceRules.push_back(rule);
        else
            LogE("unknown geyser id in distance: %s", v["geyser"].asCString());
    }
    return true;
}

static bool MatchFilter(const FilterConfig &cfg, const BatchCapture &cap)
{
    // 出生点坐标 (与喷口坐标同为 display 坐标系)
    float sx = (float)cap.startX;
    float sy = (float)cap.startY;

    // 禁止喷口: 只要出现就不匹配
    for (const auto &g : cap.geysers)
        for (int fid : cfg.forbidden)
            if (g.type == fid)
                return false;

    // 必须喷口: 每种至少出现一个
    for (int rid : cfg.required) {
        bool found = false;
        for (const auto &g : cap.geysers)
            if (g.type == rid) {
                found = true;
                break;
            }
        if (!found)
            return false;
    }

    // 距离规则: 指定喷口必须存在且在距离范围内
    for (const auto &rule : cfg.distanceRules) {
        bool ok = false;
        for (const auto &g : cap.geysers) {
            if (g.type != rule.type)
                continue;
            float dx = (float)g.x - sx;
            float dy = (float)g.y - sy;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist >= rule.minDist && dist <= rule.maxDist) {
                ok = true;
                break;
            }
        }
        if (!ok)
            return false;
    }
    return true;
}

static void PrintMatch(int seed, const BatchCapture &cap,
                       const FilterConfig &cfg)
{
    // 生成坐标码 (与 app_generate 中逻辑一致)
    static const char *worldPrefixes[] = {
        "SNDST-A-",  "OCAN-A-",    "S-FRZ-",     "LUSH-A-",    "FRST-A-",
        "VOLCA-",    "BAD-A-",     "HTFST-A-",   "OASIS-A-",   "CER-A-",
        "CERS-A-",   "PRE-A-",     "PRES-A-",    "V-SNDST-C-", "V-OCAN-C-",
        "V-SWMP-C-", "V-SFRZ-C-",  "V-LUSH-C-",  "V-FRST-C-",  "V-VOLCA-C-",
        "V-BAD-C-",  "V-HTFST-C-", "V-OASIS-C-", "V-CER-C-",   "V-CERS-C-",
        "V-PRE-C-",  "V-PRES-C-",  "SNDST-C-",   "PRE-C-",     "CER-C-",
        "FRST-C-",   "SWMP-C-",    "M-SWMP-C-",  "M-BAD-C-",   "M-FRZ-C-",
        "M-FLIP-C-", "M-RAD-C-",   "M-CERS-C-"};
    std::string code = worldPrefixes[cfg.worldType];
    code += std::to_string(seed);
    code += "-0-D3-";
    code += SettingsCache::BinaryToBase36(cfg.mixing);

    float sx = (float)cap.startX;
    float sy = (float)cap.startY;

    printf("\n=== SEED %d ===\n", seed);
    printf("  code: %s\n", code.c_str());
    printf("  world: %dx%d, start: (%d, %d)\n",
           cap.worldW, cap.worldH, cap.startX, cap.startY);

    // 特性
    if (!cap.traits.empty()) {
        printf("  traits:");
        for (int idx : cap.traits)
            printf(" [%s]", g_traitNames[idx]);
        printf("\n");
    }

    // 喷口列表
    for (const auto &g : cap.geysers) {
        float dx = (float)g.x - sx;
        float dy = (float)g.y - sy;
        float dist = std::sqrt(dx * dx + dy * dy);
        printf("  %-16s %-20s (%3d, %3d) dist=%6.1f\n",
               g_geyserIds[g.type], g_geyserNames[g.type], g.x, g.y, dist);
    }
}

static void PrintUsage()
{
    printf("Usage:\n");
    printf("  oniWorldApp                       Interactive mode\n");
    printf("  oniWorldApp --filter filter.json   Batch search mode\n");
    printf("  oniWorldApp --list-geysers         Show all geyser IDs\n");
    printf("  oniWorldApp --list-worlds          Show all world type IDs\n");
}

static void PrintGeyserList()
{
    printf("Geyser IDs (for use in filter.json):\n");
    for (int i = 0; i < GEYSER_TYPE_COUNT; ++i)
        printf("  %2d  %-24s %s\n", i, g_geyserIds[i], g_geyserNames[i]);
}

static void PrintWorldList()
{
    const char *worlds[] = {
        "SNDST-A  (Sandstone, vanilla)",
        "OCAN-A   (Oceania)",
        "S-FRZ    (Rime)",
        "LUSH-A   (Arboria)",
        "FRST-A   (Verdante)",
        "VOLCA    (Volcanea)",
        "BAD-A    (Badlands)",
        "HTFST-A  (Aridio)",
        "OASIS-A  (Oasisse)",
        "CER-A    (Ceres, vanilla)",
        "CERS-A   (Ceres SO)",
        "PRE-A    (Blasted Ceres, vanilla)",
        "PRES-A   (Blasted Ceres SO)",
    };
    printf("World type IDs (worldType in filter.json):\n");
    for (int i = 0; i < (int)std::size(worlds); ++i)
        printf("  %2d  %s\n", i, worlds[i]);
    printf("  ... (13~37 for Spaced Out cluster variants)\n");
}

// ============================================================
//  main
// ============================================================
int main(int argc, char *argv[])
{
    // 解析命令行
    if (argc >= 2) {
        std::string arg1 = argv[1];
        if (arg1 == "--help" || arg1 == "-h") {
            PrintUsage();
            return 0;
        }
        if (arg1 == "--list-geysers") {
            PrintGeyserList();
            return 0;
        }
        if (arg1 == "--list-worlds") {
            PrintWorldList();
            return 0;
        }
    }

    // 初始化 (加载 data.zip 资源包 — 主线程实例)
    app_init(0);

    if (argc >= 3 && std::string(argv[1]) == "--filter") {
        // ---- 批量筛选模式 (多线程) ----
        FilterConfig cfg;
        if (!LoadFilter(argv[2], cfg))
            return 1;

        g_batchMode = true;
        int total = cfg.seedEnd - cfg.seedStart + 1;
        int numThreads = cfg.threads;
        if (numThreads <= 0) {
            numThreads = (int)std::thread::hardware_concurrency();
            if (numThreads <= 0)
                numThreads = 4;
        }

        printf("Scanning seeds %d ~ %d (worldType=%d, mixing=%d) with %d threads\n",
               cfg.seedStart, cfg.seedEnd, cfg.worldType, cfg.mixing, numThreads);
        printf("  required: %zu, forbidden: %zu, distance rules: %zu\n",
               cfg.required.size(), cfg.forbidden.size(), cfg.distanceRules.size());

        std::mutex outputMutex;
        std::atomic<int> totalMatches{0};
        std::atomic<int> seedsProcessed{0};

        auto worker = [&](int rangeStart, int rangeEnd) {
            // 每个线程独立初始化 App (thread_local 实例)
            app_init(0);
            App::Instance()->skipPolygons = true;
            g_batch.active = true;

            for (int seed = rangeStart; seed <= rangeEnd; ++seed) {
                g_batch.Reset();
                if (!app_generate(cfg.worldType, seed, cfg.mixing))
                    continue;

                if (MatchFilter(cfg, g_batch)) {
                    totalMatches.fetch_add(1);
                    std::lock_guard<std::mutex> lock(outputMutex);
                    PrintMatch(seed, g_batch, cfg);
                }

                int done = seedsProcessed.fetch_add(1) + 1;
                if (done % 1000 == 0) {
                    std::lock_guard<std::mutex> lock(outputMutex);
                    printf("[%d/%d] found %d matches\n",
                           done, total, totalMatches.load());
                }
            }
        };

        // 将种子范围均匀分配到各线程
        std::vector<std::thread> threads;
        int seedsPerThread = total / numThreads;
        int remainder = total % numThreads;
        int currentStart = cfg.seedStart;

        for (int t = 0; t < numThreads; ++t) {
            int count = seedsPerThread + (t < remainder ? 1 : 0);
            int rangeEnd = currentStart + count - 1;
            threads.emplace_back(worker, currentStart, rangeEnd);
            currentStart = rangeEnd + 1;
        }

#ifdef _WIN32
        // Intel 大小核: 将线程绑定到前 numThreads 个逻辑处理器 (通常是 P 核)
        // 每个线程绑定一个核心，避免 E 核调度和 P 核降频
        {
            int hwThreads = (int)std::thread::hardware_concurrency();
            // 仅当用户指定的线程数 < 总逻辑核心数时才绑定
            // (说明用户有意只用部分核心)
            if (numThreads < hwThreads) {
                for (int t = 0; t < numThreads; ++t) {
                    DWORD_PTR mask = (DWORD_PTR)1 << t;
                    SetThreadAffinityMask(threads[t].native_handle(), mask);
                }
                printf("  affinity: pinned %d threads to logical processors 0-%d\n",
                       numThreads, numThreads - 1);
            }
        }
#endif

        for (auto &th : threads)
            th.join();

        printf("\nDone. Scanned %d seeds, found %d matches.\n",
               total, totalMatches.load());
    } else {
        // ---- 交互模式 (原有行为) ----
        int type, seed, mixing;
        while (true) {
            std::cout << "input type, seed, mixing: ";
            std::cin >> type >> seed >> mixing;
            if (seed == 0)
                break;
            if (!app_generate(type, seed, mixing)) {
                LogE("generate failed.");
            }
        }
    }
    return 0;
}

// ============================================================
//  jsExchangeData — C++ 端实现 (非 WASM)
// ============================================================
void jsExchangeData(uint32_t type, uint32_t count, size_t data)
{
    switch (type) {
    default:
        break;

    case RT_Starting: {
        auto ptr = (int32_t *)data;
        if (g_batch.active && count == 0) {
            // count=0 表示 StartWorld，捕获出生点坐标
            g_batch.startX = ptr[0];
            g_batch.startY = ptr[1];
        }
        break;
    }

    case RT_Trait: {
        auto ptr = (uint32_t *)data;
        auto end = ptr + count;
        if (g_batch.active) {
            while (ptr < end)
                g_batch.traits.push_back((int)*ptr++);
        } else {
            while (ptr < end) {
                auto index = *ptr++;
                LogI("%s", g_traitNames[index]);
            }
        }
        break;
    }

    case RT_Geyser: {
        auto ptr = (uint32_t *)data;
        auto end = ptr + count;
        while (ptr < end) {
            auto index = *ptr++;
            auto x = *ptr++;
            auto y = *ptr++;
            if (g_batch.active) {
                g_batch.geysers.push_back({(int)index, (int)x, (int)y});
            } else {
                LogI("%s: %d, %d", g_geyserNames[index], x, y);
            }
        }
        break;
    }

    case RT_WorldSize: {
        auto ptr = (int32_t *)data;
        if (g_batch.active && g_batch.worldW == 0) {
            g_batch.worldW = ptr[0];
            g_batch.worldH = ptr[1];
        }
        break;
    }

    case RT_Polygon:
        break;

    case RT_Resource: {
        auto ptr = (char *)data;
        *ptr = 'E';
        std::ifstream fstm(SETTING_ASSET_FILEPATH, std::ios::binary);
        if (fstm.is_open()) {
            auto size = fstm.seekg(0, std::ios::end).tellg();
            if (size == count) {
                fstm.seekg(0).read(ptr, count);
            } else {
                LogE("wrong count.");
            }
        } else {
            LogE("can not open file.");
        }
        break;
    }
    }
}

#endif
