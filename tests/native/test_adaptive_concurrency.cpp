#include "Batch/BatchSearchService.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

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

} // namespace

int RunAllTests()
{
    int failures = 0;

    Batch::SearchRequest request;
    request.seedStart = 1;
    request.seedEnd = 400;
    request.workerCount = 8;
    request.chunkSize = 8;
    request.progressInterval = 8;
    request.sampleWindow = std::chrono::milliseconds(80);
    request.enableAdaptive = true;
    request.adaptiveConfig.enabled = true;
    request.adaptiveConfig.minWorkers = 2;
    request.adaptiveConfig.dropThreshold = 0.08;
    request.adaptiveConfig.consecutiveDropWindows = 1;
    request.adaptiveConfig.cooldown = std::chrono::milliseconds(50);

    std::atomic<int> counter{0};
    request.evaluateSeed = [&](int) {
        Batch::SearchSeedEvaluation evaluation;
        evaluation.generated = true;
        const int index = counter.fetch_add(1, std::memory_order_relaxed);
        if (index < 80) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(6));
        }
        return evaluation;
    };

    std::atomic<int> reducedEvents{0};
    Batch::SearchEventCallbacks callbacks;
    callbacks.onProgress = [&](const Batch::SearchProgressEvent &event) {
        if (event.activeWorkersReduced) {
            reducedEvents.fetch_add(1, std::memory_order_relaxed);
        }
    };

    const auto result = Batch::BatchSearchService::Run(request, callbacks);

    Expect(!result.failed, "adaptive run should not fail", failures);
    Expect(!result.cancelled, "adaptive run should not cancel", failures);
    Expect(result.processedSeeds == result.totalSeeds, "adaptive run should process all seeds", failures);
    Expect(result.totalSeeds == 400, "adaptive run total seed count mismatch", failures);
    Expect(result.autoFallbackCount > 0, "adaptive run should trigger fallback", failures);
    Expect(result.finalActiveWorkers < static_cast<int>(request.workerCount),
           "adaptive run should reduce active workers",
           failures);
    Expect(reducedEvents.load(std::memory_order_relaxed) > 0,
           "adaptive run should emit reduced progress events",
           failures);

    if (failures == 0) {
        std::cout << "[PASS] test_adaptive_concurrency" << std::endl;
        return 0;
    }
    return 1;
}

