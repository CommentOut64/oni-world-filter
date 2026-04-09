#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

#include "App/ResultSink.hpp"
#include "BatchCpu/CpuOptimization.hpp"

namespace Batch {

struct SearchSeedEvaluation {
    bool ok = true;
    bool generated = false;
    bool matched = false;
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
    uint32_t workerCount = 1;
    int chunkSize = 64;
    int progressInterval = 1000;
    std::chrono::milliseconds sampleWindow{2000};
    std::chrono::milliseconds maxRunDuration{0};

    bool enableAdaptive = false;
    BatchCpu::AdaptiveConfig adaptiveConfig{};

    std::atomic<bool> *cancelRequested = nullptr;

    SearchWorkerInitializer initializeWorker;
    SearchThreadPlacementApplier applyThreadPlacement;
    SearchSeedEvaluator evaluateSeed;
};

} // namespace Batch
