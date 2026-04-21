#ifdef __EMSCRIPTEN__
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
#define EMSCRIPTEN_KEEPALIVE
#endif

#include <ranges>
#include <algorithm>
#include <cmath>
#include <numeric>

#include "App/AppRuntime.hpp"
#include "App/ResultSink.hpp"
#include "Batch/BatchSearchService.hpp"
#include "Batch/BatchMatcher.hpp"
#include "Batch/CpuTopology.hpp"
#include "Batch/FilterConfig.hpp"
#include "Batch/ThreadPolicy.hpp"
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

#ifndef __EMSCRIPTEN__
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
#ifndef __EMSCRIPTEN__
    if (!g_batchMode)
#endif
        LogI("generate with code: %s", code.c_str());
    return GetRuntime()->Generate(code, traits);
}

#ifndef __EMSCRIPTEN__

static const auto &g_geyserIds = Batch::GetGeyserIds();

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

// ============================================================
//  批量模式 — 数据捕获
// ============================================================
using BatchCapture = BatchCaptureRecord;
static thread_local BatchCaptureSink g_batchSink;


static void PrintMatch(int seed, const BatchCapture &cap,
                       const Batch::FilterConfig &cfg)
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
               g_geyserIds[(size_t)g.type].c_str(), g_geyserNames[g.type], g.x, g.y, dist);
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
    for (int i = 0; i < (int)g_geyserIds.size(); ++i)
        printf("  %2d  %-24s %s\n", i, g_geyserIds[(size_t)i].c_str(), g_geyserNames[i]);
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
};

static void PrintCompiledSearchCpuPlan(const Batch::CompiledSearchCpuRuntime &runtime,
                                       const char *prefix)
{
    printf("%s%s\n",
           prefix,
           Batch::DescribeCompiledSearchCpuPlan(runtime.cpuPlan).c_str());
}

static Batch::SearchRequest BuildSearchRequest(const Batch::FilterConfig &cfg,
                                               int seedStart,
                                               int seedEnd,
                                               const Batch::CompiledSearchCpuRuntime &runtime,
                                               const BatchRunOptions &options,
                                               std::mutex *outputMutex)
{
    Batch::SearchRequest request;
    request.seedStart = seedStart;
    request.seedEnd = seedEnd;
    request.cpuPlan = runtime.cpuPlan;
    request.cpuGovernorConfig = runtime.cpuGovernorConfig;
    request.initializeWorker = []() {
        auto *runtime = AppRuntime::Instance();
        runtime->SetResultSink(&g_batchSink);
        runtime->SetSkipPolygons(true);
        g_batchSink.SetActive(true);
        app_init(0);
    };
    request.applyThreadPlacement =
        [cpuPlan = runtime.cpuPlan, printDiagnostics = options.printDiagnostics, outputMutex](
            uint32_t workerIndex, std::string *errorMessage) {
            const bool applied =
                BatchCpu::ApplyThreadPlacement(cpuPlan.placement,
                                              cpuPlan.policy.binding,
                                              workerIndex,
                                              errorMessage);
            if (!applied &&
                printDiagnostics &&
                errorMessage != nullptr &&
                !errorMessage->empty() &&
                outputMutex != nullptr) {
                std::lock_guard<std::mutex> lock(*outputMutex);
                printf("  [cpu] worker %u placement fallback: %s\n",
                       workerIndex,
                       errorMessage->c_str());
            }
            return applied;
        };
    request.evaluateSeed = [&cfg](int seed) {
        Batch::SearchSeedEvaluation evaluation;
        auto *runtime = AppRuntime::Instance();
        runtime->SetResultSink(&g_batchSink);
        runtime->SetSkipPolygons(true);
        g_batchSink.SetActive(true);
        runtime->Initialize(0);
        g_batchSink.Reset();
        evaluation.generated = app_generate(cfg.worldType, seed, cfg.mixing);
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

static Batch::SearchEventCallbacks BuildConsoleCallbacks(
    const Batch::FilterConfig &cfg,
    const BatchRunOptions &options,
    std::mutex &outputMutex)
{
    Batch::SearchEventCallbacks callbacks;
    callbacks.onProgress = [&](const Batch::SearchProgressEvent &event) {
        if (event.hasWindowSample && options.printProgress) {
            std::lock_guard<std::mutex> lock(outputMutex);
            printf("  [cpu] window throughput: %.1f seeds/s, active workers=%d\n",
                   event.windowSeedsPerSecond,
                   event.activeWorkers);
        }
        if (event.activeWorkersReduced &&
            (options.printDiagnostics || options.printProgress)) {
            std::lock_guard<std::mutex> lock(outputMutex);
            printf("  [cpu] adaptive fallback -> active workers %d (peak=%.1f)\n",
                   event.activeWorkers,
                   event.peakSeedsPerSecond);
        }
        if (!event.hasWindowSample && options.printProgress) {
            std::lock_guard<std::mutex> lock(outputMutex);
            printf("[%d/%d] found %d matches (active workers=%d)\n",
                   event.processedSeeds,
                   event.totalSeeds,
                   event.totalMatches,
                   event.activeWorkers);
        }
    };
    callbacks.onMatch = [&](const Batch::SearchMatchEvent &event) {
        if (!options.printMatches) {
            return;
        }
        std::lock_guard<std::mutex> lock(outputMutex);
        PrintMatch(event.seed, event.capture, cfg);
    };
    callbacks.onFailed = [&](const Batch::SearchFailedEvent &event) {
        if (!options.printDiagnostics) {
            return;
        }
        std::lock_guard<std::mutex> lock(outputMutex);
        printf("  [search] failed: %s\n", event.message.c_str());
    };
    return callbacks;
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
        auto loadResult = Batch::LoadFilterConfig(argv[2]);
        if (!loadResult.Ok()) {
            for (const auto &error : loadResult.errors) {
                LogE("%s", Batch::FormatFilterError(error).c_str());
            }
            return 1;
        }
        const auto &cfg = loadResult.config;

        g_batchMode = true;
        const int totalSeeds = cfg.seedEnd - cfg.seedStart + 1;
        const bool benchmarkSilent = cfg.cpu.benchmarkSilent;
        const auto topology = Batch::DetectCpuTopologyFacts();
        const auto runtimePlan = Batch::CompileSearchCpuRuntime(cfg, topology);

        printf("Scanning seeds %d ~ %d (worldType=%d, mixing=%d)\n",
               cfg.seedStart, cfg.seedEnd, cfg.worldType, cfg.mixing);
        printf("  required: %zu, forbidden: %zu, distance rules: %zu\n",
               cfg.required.size(), cfg.forbidden.size(), cfg.distanceRules.size());
        printf("  cpu mode: %s\n",
               BatchCpu::ToString(runtimePlan.cpuPlan.policy.mode));
        if (cfg.cpu.printDiagnostics && !benchmarkSilent) {
            printf("  %s\n", topology.diagnostics.c_str());
            printf("  cpu plan: ");
            PrintCompiledSearchCpuPlan(runtimePlan, "");
        }

        BatchRunOptions runOptions;
        runOptions.printMatches = !benchmarkSilent && cfg.cpu.printMatches;
        runOptions.printProgress = !benchmarkSilent && cfg.cpu.printProgress;
        runOptions.printDiagnostics = !benchmarkSilent && cfg.cpu.printDiagnostics;

        std::mutex outputMutex;
        auto finalRequest = BuildSearchRequest(
            cfg, cfg.seedStart, cfg.seedEnd, runtimePlan, runOptions, &outputMutex);
        auto finalCallbacks = BuildConsoleCallbacks(cfg, runOptions, outputMutex);
        auto finalRun = Batch::BatchSearchService::Run(finalRequest, finalCallbacks);
        if (finalRun.failed) {
            return 1;
        }
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
