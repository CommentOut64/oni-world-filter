#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

#include "App/ResultSink.hpp"
#include "BatchCpu/SearchCpuGovernor.hpp"

namespace Batch {

struct SearchSeedEvaluation {
    bool ok = true;
    bool generated = false;
    bool matched = false;
    std::string coord;
    BatchCaptureRecord capture;
    std::string errorMessage;
};

using SearchSeedEvaluator = std::function<SearchSeedEvaluation(int seed)>;
using SearchWorkerInitializer = std::function<void()>;
using SearchThreadPlacementApplier =
    std::function<bool(uint32_t workerIndex, std::string *errorMessage)>;

struct SearchRequest {
    int seedStart = 1;
    int seedEnd = 0;
    BatchCpu::CompiledSearchCpuPlan cpuPlan{};
    BatchCpu::SearchCpuGovernorConfig cpuGovernorConfig{};
    int chunkSize = 64;
    int progressInterval = 1000;
    std::chrono::milliseconds sampleWindow{2000};
    std::chrono::milliseconds maxRunDuration{0};

    std::atomic<bool> *cancelRequested = nullptr;
    std::atomic<int> *activeWorkerCap = nullptr;

    SearchWorkerInitializer initializeWorker;
    SearchThreadPlacementApplier applyThreadPlacement;
    SearchSeedEvaluator evaluateSeed;
};

} // namespace Batch
