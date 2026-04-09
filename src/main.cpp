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
#include <chrono>
#include <json/json.h>
#define EMSCRIPTEN_KEEPALIVE
#endif

#include <ranges>
#include <algorithm>
#include <cmath>
#include <optional>
#include <numeric>

#include "App/AppRuntime.hpp"
#include "App/ResultSink.hpp"
#include "BatchCpu/CpuOptimization.hpp"
#include "config.h"
#include "Setting/SettingsCache.hpp"

// I defined only one function for exchanging data between c++ and js,
// it get resource from js and set result to js.
extern "C" void jsExchangeData(uint32_t type, uint32_t count, size_t data);

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

static WasmResultSink g_wasmSink;

static AppRuntime *GetRuntime()
{
    auto *runtime = AppRuntime::Instance();
    if (runtime->GetResultSink() == nullptr) {
        runtime->SetResultSink(&g_wasmSink);
    }
    return runtime;
}

extern "C" void EMSCRIPTEN_KEEPALIVE app_init(int seed)
{
    GetRuntime()->Initialize(seed);
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
    return GetRuntime()->Generate(code, traits);
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
using BatchCapture = BatchCaptureRecord;
static thread_local BatchCaptureSink g_batchSink;

// ============================================================
//  筛选规则
// ============================================================
struct FilterConfig {
    struct CpuConfig {
        std::string mode = "balanced"; // balanced/turbo/custom/conservative
        int workers = 0;
        bool allowSmt = true;
        bool allowLowPerf = false;
        std::string placement = "preferred"; // none/preferred/strict
        bool enableWarmup = true;
        int warmupTotalMs = 10000;
        int warmupPerCandidateMs = 2500;
        int warmupSeedCount = 4000;
        double warmupTieTolerance = 0.03;
        int warmupMinSampledSeeds = 512;
        int warmupMaxRetry = 2;
        bool enableAdaptiveDown = true;
        int adaptiveMinWorkers = 1;
        double adaptiveDropThreshold = 0.12;
        int adaptiveDropWindows = 3;
        int adaptiveCooldownMs = 8000;
        int sampleWindowMs = 2000;
        int chunkSize = 64;
        int progressInterval = 1000;
        bool printMatches = true;
        bool printProgress = true;
        bool benchmarkSilent = false;
        bool printDiagnostics = true;
    };

    int worldType = 0;
    int seedStart = 1;
    int seedEnd = 100000;
    int mixing = 0;
    int threads = 0; // 0 = 自动 (hardware_concurrency)
    bool hasCpuSection = false;
    CpuConfig cpu;
    std::vector<int> required;
    std::vector<int> forbidden;
    struct DistRule {
        int type;
        float minDist = 0;
        float maxDist = 1e9f;
    };
    std::vector<DistRule> distanceRules;
};

template<typename T>
static T ClampValue(T value, T minValue, T maxValue)
{
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

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
    if (cfg.seedEnd < cfg.seedStart) {
        std::swap(cfg.seedStart, cfg.seedEnd);
    }
    cfg.threads = std::max(0, cfg.threads);

    const Json::Value cpu = root["cpu"];
    if (cpu.isObject()) {
        cfg.hasCpuSection = true;
        cfg.cpu.mode = cpu.get("mode", cfg.cpu.mode).asString();
        cfg.cpu.workers = std::max(0, cpu.get("workers", cfg.cpu.workers).asInt());
        cfg.cpu.allowSmt = cpu.get("allowSmt", cfg.cpu.allowSmt).asBool();
        cfg.cpu.allowLowPerf = cpu.get("allowLowPerf", cfg.cpu.allowLowPerf).asBool();
        cfg.cpu.placement = cpu.get("placement", cfg.cpu.placement).asString();
        cfg.cpu.enableWarmup = cpu.get("enableWarmup", cfg.cpu.enableWarmup).asBool();
        cfg.cpu.warmupTotalMs = cpu.get("warmupTotalMs", cfg.cpu.warmupTotalMs).asInt();
        cfg.cpu.warmupPerCandidateMs = cpu.get("warmupPerCandidateMs", cfg.cpu.warmupPerCandidateMs).asInt();
        cfg.cpu.warmupSeedCount = cpu.get("warmupSeedCount", cfg.cpu.warmupSeedCount).asInt();
        cfg.cpu.warmupTieTolerance = cpu.get("warmupTieTolerance", cfg.cpu.warmupTieTolerance).asDouble();
        cfg.cpu.warmupMinSampledSeeds = cpu.get("warmupMinSampledSeeds", cfg.cpu.warmupMinSampledSeeds).asInt();
        cfg.cpu.warmupMaxRetry = cpu.get("warmupMaxRetry", cfg.cpu.warmupMaxRetry).asInt();
        cfg.cpu.enableAdaptiveDown = cpu.get("enableAdaptiveDown", cfg.cpu.enableAdaptiveDown).asBool();
        cfg.cpu.adaptiveMinWorkers = cpu.get("adaptiveMinWorkers", cfg.cpu.adaptiveMinWorkers).asInt();
        cfg.cpu.adaptiveDropThreshold = cpu.get("adaptiveDropThreshold", cfg.cpu.adaptiveDropThreshold).asDouble();
        cfg.cpu.adaptiveDropWindows = cpu.get("adaptiveDropWindows", cfg.cpu.adaptiveDropWindows).asInt();
        cfg.cpu.adaptiveCooldownMs = cpu.get("adaptiveCooldownMs", cfg.cpu.adaptiveCooldownMs).asInt();
        cfg.cpu.sampleWindowMs = cpu.get("sampleWindowMs", cfg.cpu.sampleWindowMs).asInt();
        cfg.cpu.chunkSize = cpu.get("chunkSize", cfg.cpu.chunkSize).asInt();
        cfg.cpu.progressInterval = cpu.get("progressInterval", cfg.cpu.progressInterval).asInt();
        cfg.cpu.printMatches = cpu.get("printMatches", cfg.cpu.printMatches).asBool();
        cfg.cpu.printProgress = cpu.get("printProgress", cfg.cpu.printProgress).asBool();
        cfg.cpu.benchmarkSilent = cpu.get("benchmarkSilent", cfg.cpu.benchmarkSilent).asBool();
        cfg.cpu.printDiagnostics = cpu.get("printDiagnostics", cfg.cpu.printDiagnostics).asBool();
    }

    cfg.cpu.warmupTotalMs = ClampValue(cfg.cpu.warmupTotalMs, 1000, 30000);
    cfg.cpu.warmupPerCandidateMs = ClampValue(cfg.cpu.warmupPerCandidateMs, 500, 8000);
    cfg.cpu.warmupSeedCount = ClampValue(cfg.cpu.warmupSeedCount, 256, 200000);
    cfg.cpu.warmupTieTolerance = ClampValue(cfg.cpu.warmupTieTolerance, 0.0, 0.2);
    cfg.cpu.warmupMinSampledSeeds = ClampValue(cfg.cpu.warmupMinSampledSeeds, 32, 200000);
    cfg.cpu.warmupMaxRetry = ClampValue(cfg.cpu.warmupMaxRetry, 0, 5);
    cfg.cpu.adaptiveMinWorkers = ClampValue(cfg.cpu.adaptiveMinWorkers, 1, 1024);
    cfg.cpu.adaptiveDropThreshold = ClampValue(cfg.cpu.adaptiveDropThreshold, 0.0, 0.5);
    cfg.cpu.adaptiveDropWindows = ClampValue(cfg.cpu.adaptiveDropWindows, 1, 10);
    cfg.cpu.adaptiveCooldownMs = ClampValue(cfg.cpu.adaptiveCooldownMs, 1000, 60000);
    cfg.cpu.sampleWindowMs = ClampValue(cfg.cpu.sampleWindowMs, 200, 10000);
    cfg.cpu.chunkSize = ClampValue(cfg.cpu.chunkSize, 1, 2048);
    cfg.cpu.progressInterval = ClampValue(cfg.cpu.progressInterval, 1, 1000000);

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
    printf("    filter.json supports optional cpu{} for topology-aware scheduling and benchmarkSilent\n");
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

struct BatchRunOptions {
    bool printMatches = true;
    bool printProgress = true;
    bool printDiagnostics = true;
    bool enableAdaptive = false;
    int chunkSize = 64;
    int progressInterval = 1000;
    std::chrono::milliseconds sampleWindow{2000};
    std::chrono::milliseconds maxRunDuration{0}; // 0 表示不限制
    BatchCpu::AdaptiveConfig adaptiveConfig{};
};

struct BatchRunResult {
    int totalSeeds = 0;
    int processedSeeds = 0;
    int totalMatches = 0;
    int finalActiveWorkers = 0;
    uint32_t autoFallbackCount = 0;
    bool stoppedByBudget = false;
    BatchCpu::ThroughputStats throughput{};
};

static double ComputeStdDev(const std::vector<double> &samples, double avg)
{
    if (samples.size() <= 1) {
        return 0.0;
    }
    double sumSquares = 0.0;
    for (double sample : samples) {
        const double diff = sample - avg;
        sumSquares += diff * diff;
    }
    return std::sqrt(sumSquares / (double)samples.size());
}

static BatchCpu::PlannerInput BuildPlannerInput(const FilterConfig &cfg,
                                                const BatchCpu::CpuTopology &topology)
{
    BatchCpu::PlannerInput input;
    input.topology = &topology;

    const bool legacyThreadsOnly = !cfg.hasCpuSection && cfg.threads > 0;
    if (legacyThreadsOnly) {
        input.mode = BatchCpu::CpuMode::Custom;
        input.customWorkers = (uint32_t)cfg.threads;
        input.customAllowSmt = true;
        input.customAllowLowPerf = true;
        input.customPlacement = BatchCpu::PlacementMode::Preferred;
        return input;
    }

    input.mode = BatchCpu::ParseCpuMode(cfg.cpu.mode);
    input.legacyThreadOverride = 0;
    input.customWorkers = (uint32_t)std::max(0, cfg.cpu.workers);
    input.customAllowSmt = cfg.cpu.allowSmt;
    input.customAllowLowPerf = cfg.cpu.allowLowPerf;
    input.customPlacement = BatchCpu::ParsePlacementMode(cfg.cpu.placement);
    return input;
}

static void PrintThreadPolicy(const BatchCpu::ThreadPolicy &policy, const char *prefix)
{
    printf("%sname=%s workers=%u placement=%s smt=%s low_perf=%s logical=[%s]\n",
           prefix,
           policy.name.c_str(),
           policy.workerCount,
           BatchCpu::ToString(policy.placement),
           policy.allowSmt ? "on" : "off",
           policy.allowLowPerf ? "on" : "off",
           BatchCpu::JoinLogicalList(policy.targetLogicalProcessors).c_str());
}

static BatchRunResult RunBatchWithPolicy(const FilterConfig &cfg,
                                         int seedStart,
                                         int seedEnd,
                                         const BatchCpu::ThreadPolicy &policy,
                                         const BatchRunOptions &options)
{
    BatchRunResult result;
    if (seedEnd < seedStart) {
        return result;
    }
    result.totalSeeds = seedEnd - seedStart + 1;

    const int workerCount = std::max<int>(1, (int)policy.workerCount);
    std::atomic<int> nextSeed{seedStart};
    std::atomic<int> processed{0};
    std::atomic<int> totalMatches{0};
    std::atomic<int> activeWorkers{workerCount};
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> hitBudget{false};
    std::mutex outputMutex;
    std::vector<double> sampledThroughput;
    sampledThroughput.reserve(64);

    BatchCpu::AdaptiveConfig adaptive = options.adaptiveConfig;
    adaptive.enabled = options.enableAdaptive && adaptive.enabled;
    adaptive.minWorkers = (uint32_t)std::min<int>(adaptive.minWorkers, workerCount);
    BatchCpu::AdaptiveConcurrencyController controller(adaptive, (uint32_t)workerCount);

    const auto startedAt = std::chrono::steady_clock::now();
    auto deadline = startedAt;
    if (options.maxRunDuration.count() > 0) {
        deadline = startedAt + options.maxRunDuration;
    }

    auto worker = [&](int workerIndex) {
        auto *runtime = AppRuntime::Instance();
        runtime->SetResultSink(&g_batchSink);
        runtime->SetSkipPolygons(true);
        g_batchSink.SetActive(true);
        app_init(0);

        std::string placementError;
        if (!BatchCpu::ApplyThreadPlacement(policy, (uint32_t)workerIndex, &placementError) &&
            options.printDiagnostics && !placementError.empty()) {
            std::lock_guard<std::mutex> lock(outputMutex);
            printf("  [cpu] worker %d placement fallback: %s\n",
                   workerIndex, placementError.c_str());
        }

        while (true) {
            if (stopRequested.load(std::memory_order_relaxed)) {
                break;
            }
            if (nextSeed.load(std::memory_order_relaxed) > seedEnd) {
                break;
            }
            if (workerIndex >= activeWorkers.load(std::memory_order_relaxed)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            const int chunkStart = nextSeed.fetch_add(options.chunkSize);
            if (chunkStart > seedEnd) {
                break;
            }
            const int chunkEnd = std::min(seedEnd, chunkStart + options.chunkSize - 1);
            for (int seed = chunkStart; seed <= chunkEnd; ++seed) {
                if (stopRequested.load(std::memory_order_relaxed)) {
                    break;
                }

                g_batchSink.Reset();
                const bool generated = app_generate(cfg.worldType, seed, cfg.mixing);
                const bool matched = generated && MatchFilter(cfg, g_batchSink.Data());
                if (matched) {
                    totalMatches.fetch_add(1);
                    if (options.printMatches) {
                        std::lock_guard<std::mutex> lock(outputMutex);
                        PrintMatch(seed, g_batchSink.Data(), cfg);
                    }
                }

                const int done = processed.fetch_add(1) + 1;
                if (options.printProgress && done % options.progressInterval == 0) {
                    std::lock_guard<std::mutex> lock(outputMutex);
                    printf("[%d/%d] found %d matches (active workers=%d)\n",
                           done,
                           result.totalSeeds,
                           totalMatches.load(),
                           activeWorkers.load());
                }

                if (options.maxRunDuration.count() > 0 &&
                    std::chrono::steady_clock::now() >= deadline) {
                    stopRequested.store(true);
                    hitBudget.store(true);
                    break;
                }
            }
        }
    };

    std::thread monitor([&] {
        int lastProcessed = 0;
        auto lastTs = std::chrono::steady_clock::now();
        while (!stopRequested.load(std::memory_order_relaxed)) {
            if (processed.load(std::memory_order_relaxed) >= result.totalSeeds) {
                break;
            }
            std::this_thread::sleep_for(options.sampleWindow);
            const auto now = std::chrono::steady_clock::now();
            if (options.maxRunDuration.count() > 0 && now >= deadline) {
                stopRequested.store(true);
                hitBudget.store(true);
            }

            const int current = processed.load(std::memory_order_relaxed);
            const int delta = current - lastProcessed;
            const double seconds =
                std::chrono::duration<double>(now - lastTs).count();
            if (seconds <= 0.0 || delta <= 0) {
                continue;
            }
            const double seedsPerSecond = (double)delta / seconds;
            sampledThroughput.push_back(seedsPerSecond);

            if (options.printProgress) {
                std::lock_guard<std::mutex> lock(outputMutex);
                printf("  [cpu] window throughput: %.1f seeds/s, active workers=%d\n",
                       seedsPerSecond,
                       activeWorkers.load(std::memory_order_relaxed));
            }

            if (adaptive.enabled) {
                const auto nextWorkers = controller.Observe(
                    seedsPerSecond,
                    (uint32_t)activeWorkers.load(std::memory_order_relaxed),
                    now);
                if (nextWorkers.has_value() &&
                    (int)nextWorkers.value() < activeWorkers.load(std::memory_order_relaxed)) {
                    activeWorkers.store((int)nextWorkers.value(), std::memory_order_relaxed);
                    if (options.printDiagnostics || options.printProgress) {
                        std::lock_guard<std::mutex> lock(outputMutex);
                        printf("  [cpu] adaptive fallback -> active workers %u (peak=%.1f)\n",
                               nextWorkers.value(),
                               controller.PeakSeedsPerSecond());
                    }
                }
            }

            lastProcessed = current;
            lastTs = now;
        }
    });

    std::vector<std::thread> workers;
    workers.reserve((size_t)workerCount);
    for (int i = 0; i < workerCount; ++i) {
        workers.emplace_back(worker, i);
    }
    for (auto &th : workers) {
        th.join();
    }

    stopRequested.store(true);
    if (monitor.joinable()) {
        monitor.join();
    }

    result.processedSeeds = processed.load();
    result.totalMatches = totalMatches.load();
    result.finalActiveWorkers = activeWorkers.load();
    result.autoFallbackCount = controller.ReductionCount();
    result.stoppedByBudget = hitBudget.load();

    const auto finishedAt = std::chrono::steady_clock::now();
    const double elapsedSeconds =
        std::chrono::duration<double>(finishedAt - startedAt).count();
    if (elapsedSeconds > 0.0 && result.processedSeeds > 0) {
        result.throughput.averageSeedsPerSecond =
            (double)result.processedSeeds / elapsedSeconds;
        result.throughput.processedSeeds = (uint64_t)result.processedSeeds;
        result.throughput.valid = true;
    }
    if (!sampledThroughput.empty()) {
        const double sampleAvg = std::accumulate(sampledThroughput.begin(),
                                                 sampledThroughput.end(),
                                                 0.0) /
                                 (double)sampledThroughput.size();
        result.throughput.stddevSeedsPerSecond =
            ComputeStdDev(sampledThroughput, sampleAvg);
    }

    return result;
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
        const int totalSeeds = cfg.seedEnd - cfg.seedStart + 1;
        const bool legacyThreadsOnly = !cfg.hasCpuSection && cfg.threads > 0;
        const bool benchmarkSilent = cfg.cpu.benchmarkSilent;

        auto topology = BatchCpu::CpuTopologyDetector::Detect();
        auto plannerInput = BuildPlannerInput(cfg, topology);
        auto candidates = BatchCpu::ThreadPolicyPlanner::BuildCandidates(plannerInput);
        if (candidates.empty()) {
            candidates.push_back(BatchCpu::ThreadPolicyPlanner::BuildConservativePolicy(topology));
        }

        printf("Scanning seeds %d ~ %d (worldType=%d, mixing=%d)\n",
               cfg.seedStart, cfg.seedEnd, cfg.worldType, cfg.mixing);
        printf("  required: %zu, forbidden: %zu, distance rules: %zu\n",
               cfg.required.size(), cfg.forbidden.size(), cfg.distanceRules.size());
        printf("  cpu mode: %s%s\n",
               BatchCpu::ToString(plannerInput.mode),
               legacyThreadsOnly ? " (legacy threads override)" : "");
        if (cfg.cpu.printDiagnostics && !benchmarkSilent) {
            printf("  %s\n", topology.diagnostics.c_str());
            for (size_t i = 0; i < candidates.size(); ++i) {
                printf("  candidate[%zu] ", i);
                PrintThreadPolicy(candidates[i], "");
            }
        }

        auto selectedPolicy = candidates.front();
        bool enableWarmup = cfg.cpu.enableWarmup && !legacyThreadsOnly &&
                            plannerInput.mode != BatchCpu::CpuMode::Custom &&
                            plannerInput.mode != BatchCpu::CpuMode::Conservative &&
                            candidates.size() > 1;
        if (cfg.seedStart >= cfg.seedEnd) {
            enableWarmup = false;
        }

        if (enableWarmup) {
            const int warmupEnd = std::min(
                cfg.seedEnd,
                cfg.seedStart + std::max(1, cfg.cpu.warmupSeedCount) - 1);
            const int warmupTotalSeeds = warmupEnd - cfg.seedStart + 1;
            const int warmupMinSampledSeeds = std::min(
                warmupTotalSeeds,
                std::max(1, cfg.cpu.warmupMinSampledSeeds));
            BatchCpu::WarmupConfig warmupConfig;
            warmupConfig.enabled = true;
            warmupConfig.totalBudget = std::chrono::milliseconds(cfg.cpu.warmupTotalMs);
            warmupConfig.perCandidateBudget = std::chrono::milliseconds(cfg.cpu.warmupPerCandidateMs);
            warmupConfig.tieToleranceRatio = cfg.cpu.warmupTieTolerance;

            BatchRunOptions warmupOptions;
            warmupOptions.printMatches = false;
            warmupOptions.printProgress = false;
            warmupOptions.printDiagnostics = false;
            warmupOptions.enableAdaptive = false;
            warmupOptions.chunkSize = cfg.cpu.chunkSize;
            warmupOptions.sampleWindow = std::chrono::milliseconds(cfg.cpu.sampleWindowMs);

            if (!benchmarkSilent) {
                printf("  [warmup] calibrating policies on seeds %d ~ %d ...\n",
                       cfg.seedStart, warmupEnd);
            }
            auto warmupResults = BatchCpu::ThroughputCalibrator::Evaluate(
                candidates,
                warmupConfig,
                [&](const BatchCpu::ThreadPolicy &policy, std::chrono::milliseconds budget) {
                    auto currentBudget = budget;
                    BatchRunResult bestRun;
                    for (int attempt = 0; attempt <= cfg.cpu.warmupMaxRetry; ++attempt) {
                        warmupOptions.maxRunDuration = currentBudget;
                        auto attemptRun = RunBatchWithPolicy(
                            cfg, cfg.seedStart, warmupEnd, policy, warmupOptions);
                        if (attemptRun.processedSeeds > bestRun.processedSeeds) {
                            bestRun = attemptRun;
                        }
                        const bool sampledEnough = attemptRun.processedSeeds >= warmupMinSampledSeeds;
                        if (attemptRun.throughput.valid && sampledEnough) {
                            return attemptRun.throughput;
                        }
                        if (!attemptRun.stoppedByBudget) {
                            break;
                        }
                        currentBudget = std::max(currentBudget * 2, std::chrono::milliseconds(5000));
                    }
                    return bestRun.throughput;
                });

            if (!benchmarkSilent) {
                for (size_t i = 0; i < warmupResults.size(); ++i) {
                    const auto &item = warmupResults[i];
                    const bool sampledEnough =
                        (int)item.stats.processedSeeds >= warmupMinSampledSeeds;
                    printf("  [warmup] candidate[%zu] %s -> avg %.1f seeds/s, stdev %.1f, sampled=%llu/%d%s\n",
                           i,
                           item.policy.name.c_str(),
                           item.stats.averageSeedsPerSecond,
                           item.stats.stddevSeedsPerSecond,
                           (unsigned long long)item.stats.processedSeeds,
                           warmupMinSampledSeeds,
                           sampledEnough ? "" : " [under-sampled]");
                }
            }

            auto selectableResults = warmupResults;
            bool hasQualifiedResult = false;
            for (auto &item : selectableResults) {
                if ((int)item.stats.processedSeeds < warmupMinSampledSeeds) {
                    item.stats.valid = false;
                    continue;
                }
                if (item.stats.valid) {
                    hasQualifiedResult = true;
                }
            }
            if (!hasQualifiedResult && cfg.cpu.printDiagnostics && !benchmarkSilent) {
                printf("  [warmup] warning: no candidate reached min sampled seeds, fallback to raw comparison\n");
            }
            const auto &resultsForSelection =
                hasQualifiedResult ? selectableResults : warmupResults;
            const size_t bestIndex = BatchCpu::ThroughputCalibrator::PickBestIndex(
                resultsForSelection, cfg.cpu.warmupTieTolerance);
            if (!warmupResults.empty() && bestIndex < warmupResults.size()) {
                selectedPolicy = warmupResults[bestIndex].policy;
            }
        }

        if (!benchmarkSilent) {
            printf("  selected policy: ");
            PrintThreadPolicy(selectedPolicy, "");
        }

        BatchRunOptions runOptions;
        runOptions.printMatches = !benchmarkSilent && cfg.cpu.printMatches;
        runOptions.printProgress = !benchmarkSilent && cfg.cpu.printProgress;
        runOptions.printDiagnostics = !benchmarkSilent && cfg.cpu.printDiagnostics;
        runOptions.enableAdaptive = cfg.cpu.enableAdaptiveDown && !legacyThreadsOnly;
        runOptions.chunkSize = cfg.cpu.chunkSize;
        runOptions.progressInterval = cfg.cpu.progressInterval;
        runOptions.sampleWindow = std::chrono::milliseconds(cfg.cpu.sampleWindowMs);
        runOptions.maxRunDuration = std::chrono::milliseconds(0);
        runOptions.adaptiveConfig.enabled = cfg.cpu.enableAdaptiveDown && !legacyThreadsOnly;
        runOptions.adaptiveConfig.minWorkers = (uint32_t)std::max(1, cfg.cpu.adaptiveMinWorkers);
        runOptions.adaptiveConfig.dropThreshold = cfg.cpu.adaptiveDropThreshold;
        runOptions.adaptiveConfig.consecutiveDropWindows = cfg.cpu.adaptiveDropWindows;
        runOptions.adaptiveConfig.cooldown = std::chrono::milliseconds(cfg.cpu.adaptiveCooldownMs);

        auto finalRun = RunBatchWithPolicy(cfg, cfg.seedStart, cfg.seedEnd, selectedPolicy, runOptions);
        printf("\nDone. Scanned %d/%d seeds, found %d matches.\n",
               finalRun.processedSeeds, totalSeeds, finalRun.totalMatches);
        printf("Throughput summary: avg=%.1f seeds/s, stdev=%.1f, active_workers=%d, fallback_count=%u\n",
               finalRun.throughput.averageSeedsPerSecond,
               finalRun.throughput.stddevSeedsPerSecond,
               finalRun.finalActiveWorkers,
               finalRun.autoFallbackCount);
        if (finalRun.stoppedByBudget) {
            printf("  [warn] run stopped by time budget\n");
        }
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

    case (uint32_t)ResultType::Starting:
        break;

    case (uint32_t)ResultType::Trait: {
        auto ptr = (uint32_t *)data;
        auto end = ptr + count;
        while (ptr < end) {
            auto index = *ptr++;
            LogI("%s", g_traitNames[index]);
        }
        break;
    }

    case (uint32_t)ResultType::Geyser: {
        auto ptr = (uint32_t *)data;
        auto end = ptr + count;
        while (ptr < end) {
            auto index = *ptr++;
            auto x = *ptr++;
            auto y = *ptr++;
            LogI("%s: %d, %d", g_geyserNames[index], x, y);
        }
        break;
    }

    case (uint32_t)ResultType::WorldSize:
        break;

    case (uint32_t)ResultType::Polygon:
        break;

    case (uint32_t)ResultType::Resource: {
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
