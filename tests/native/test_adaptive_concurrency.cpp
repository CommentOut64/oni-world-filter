#include "Batch/BatchSearchService.hpp"
#include "BatchCpu/CpuOptimization.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

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

    {
        BatchCpu::AdaptiveConfig config;
        config.enabled = true;
        config.minWorkers = 2;
        config.dropThreshold = 0.08;
        config.consecutiveDropWindows = 1;
        config.cooldown = std::chrono::milliseconds(1);

        BatchCpu::AdaptiveConcurrencyController controller(config, 8);
        const auto t0 = std::chrono::steady_clock::now();

        Expect(!controller.Observe(100.0, 8, t0).has_value(),
               "initial sample should establish baseline without reducing workers",
               failures);

        const auto firstDrop =
            controller.Observe(80.0, 8, t0 + std::chrono::milliseconds(2));
        Expect(firstDrop.has_value() && firstDrop.value() == 7,
               "controller should reduce workers after throughput drops below baseline",
               failures);

        const auto stableAfterDrop =
            controller.Observe(78.0, 7, t0 + std::chrono::milliseconds(4));
        Expect(!stableAfterDrop.has_value(),
               "controller should rebuild baseline after a reduction instead of reusing the old peak",
               failures);

        const auto secondDrop =
            controller.Observe(60.0, 7, t0 + std::chrono::milliseconds(6));
        Expect(secondDrop.has_value() && secondDrop.value() == 6,
               "controller should still be able to reduce again after the new baseline is established",
               failures);
    }

    {
        BatchCpu::RecoveryConfig config;
        config.enabled = true;
        config.stableWindows = 2;
        config.retentionRatio = 0.97;
        config.cooldown = std::chrono::milliseconds(1);

        BatchCpu::BoundedRecoveryController controller(config, 8);
        const auto t0 = std::chrono::steady_clock::now();

        Expect(!controller.Observe(70.0, 6, t0).has_value(),
               "recovery controller should establish a stage baseline before recovering",
               failures);
        Expect(!controller.Observe(69.0, 6, t0 + std::chrono::milliseconds(2)).has_value(),
               "recovery controller should wait for the configured number of stable windows",
               failures);

        const auto firstRecovery =
            controller.Observe(68.5, 6, t0 + std::chrono::milliseconds(4));
        Expect(firstRecovery.has_value() && firstRecovery.value() == 7,
               "recovery controller should raise workers by exactly one after stable windows",
               failures);

        Expect(!controller.Observe(68.0, 7, t0 + std::chrono::milliseconds(6)).has_value(),
               "worker-count changes should reset the recovery baseline",
               failures);
        Expect(!controller.Observe(68.0, 8, t0 + std::chrono::milliseconds(8)).has_value(),
               "recovery controller should never raise above the initial worker count",
               failures);
    }

    {
        Batch::SearchRequest request;
        request.seedStart = 1;
        request.seedEnd = 100000;
        request.workerCount = 8;
        request.chunkSize = 8;
        request.progressInterval = 8;
        request.sampleWindow = std::chrono::milliseconds(80);
        request.maxRunDuration = std::chrono::milliseconds(700);
        request.enableAdaptive = true;
        request.adaptiveConfig.enabled = true;
        request.adaptiveConfig.minWorkers = 2;
        request.adaptiveConfig.dropThreshold = 0.20;
        request.adaptiveConfig.consecutiveDropWindows = 2;
        request.adaptiveConfig.cooldown = std::chrono::milliseconds(60);

        std::atomic<int> activeWorkerCap{0};
        request.activeWorkerCap = &activeWorkerCap;

        request.evaluateSeed = [&](int) {
            Batch::SearchSeedEvaluation evaluation;
            evaluation.generated = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
            return evaluation;
        };

        std::atomic<int> reducedEvents{0};
        Batch::SearchEventCallbacks callbacks;
        callbacks.onProgress = [&](const Batch::SearchProgressEvent &event) {
            if (event.activeWorkersReduced) {
                reducedEvents.fetch_add(1, std::memory_order_relaxed);
            }
        };

        std::thread hostCapThread([&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
            activeWorkerCap.store(4, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::milliseconds(240));
            activeWorkerCap.store(0, std::memory_order_relaxed);
        });

        const auto result = Batch::BatchSearchService::Run(request, callbacks);
        hostCapThread.join();

        Expect(!result.failed, "externally capped run should not fail", failures);
        Expect(!result.cancelled, "externally capped run should not cancel", failures);
        Expect(result.stoppedByBudget,
               "externally capped run should stop by budget to avoid tail-end noise",
               failures);
        Expect(result.processedSeeds > 0,
               "externally capped run should process some seeds before the budget is hit",
               failures);
        Expect(result.autoFallbackCount == 0,
               "external host cap should not trigger adaptive fallback",
               failures);
        Expect(result.finalActiveWorkers == static_cast<int>(request.workerCount),
               "active workers should recover to the runtime ceiling after host cap is released",
               failures);
        Expect(reducedEvents.load(std::memory_order_relaxed) == 0,
               "host cap windows should not emit adaptive reduction events",
               failures);
    }

    {
        Batch::SearchRequest request;
        request.seedStart = 1;
        request.seedEnd = 3600;
        request.workerCount = 8;
        request.initialActiveWorkers = 4;
        request.chunkSize = 4;
        request.progressInterval = 4;
        request.sampleWindow = std::chrono::milliseconds(40);
        request.enableAdaptive = false;
        request.enableRecovery = true;
        request.recoveryConfig.enabled = true;
        request.recoveryConfig.stableWindows = 1;
        request.recoveryConfig.retentionRatio = 0.95;
        request.recoveryConfig.cooldown = std::chrono::milliseconds(15);

        request.evaluateSeed = [&](int) {
            Batch::SearchSeedEvaluation evaluation;
            evaluation.generated = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            return evaluation;
        };

        std::vector<int> observedWorkers;
        Batch::SearchEventCallbacks callbacks;
        callbacks.onProgress = [&](const Batch::SearchProgressEvent &event) {
            if (!event.hasWindowSample) {
                return;
            }
            observedWorkers.push_back(event.activeWorkers);
        };

        const auto result = Batch::BatchSearchService::Run(request, callbacks);

        int maxObservedWorkers = 0;
        for (int value : observedWorkers) {
            maxObservedWorkers = std::max(maxObservedWorkers, value);
        }

        Expect(!result.failed, "initial worker recovery run should not fail", failures);
        Expect(!result.cancelled, "initial worker recovery run should not cancel", failures);
        Expect(result.processedSeeds == result.totalSeeds,
               "initial worker recovery run should process all seeds",
               failures);
        Expect(maxObservedWorkers == static_cast<int>(request.workerCount),
               "initial worker recovery run should recover to the runtime ceiling",
               failures);
    }

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
