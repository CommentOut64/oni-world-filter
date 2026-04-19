#include "Batch/BatchSearchService.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <string>
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
        Batch::SearchRequest request;
        request.seedStart = 1;
        request.seedEnd = 20;
        request.workerCount = 3;
        request.chunkSize = 4;
        request.progressInterval = 2;
        request.sampleWindow = std::chrono::milliseconds(10);
        request.maxRunDuration = std::chrono::milliseconds(0);
        request.enableAdaptive = false;
        request.evaluateSeed = [](int seed) {
            Batch::SearchSeedEvaluation evaluation;
            evaluation.generated = true;
            evaluation.matched = (seed % 7 == 0);
            if (evaluation.matched) {
                evaluation.capture.startX = 10 + seed;
                evaluation.capture.startY = 20 + seed;
                evaluation.capture.worldW = 256;
                evaluation.capture.worldH = 256;
            }
            return evaluation;
        };

        std::atomic<int> startedCount{0};
        std::atomic<int> progressCount{0};
        std::atomic<int> matchCount{0};
        std::atomic<int> completedCount{0};
        std::atomic<int> failedCount{0};
        std::atomic<int> cancelledCount{0};
        std::vector<int> matchedSeeds;
        std::mutex matchedSeedsMutex;

        Batch::SearchEventCallbacks callbacks;
        callbacks.onStarted = [&](const Batch::SearchStartedEvent &) {
            startedCount.fetch_add(1, std::memory_order_relaxed);
        };
        callbacks.onProgress = [&](const Batch::SearchProgressEvent &) {
            progressCount.fetch_add(1, std::memory_order_relaxed);
        };
        callbacks.onMatch = [&](const Batch::SearchMatchEvent &event) {
            matchCount.fetch_add(1, std::memory_order_relaxed);
            std::lock_guard<std::mutex> lock(matchedSeedsMutex);
            matchedSeeds.push_back(event.seed);
        };
        callbacks.onCompleted = [&](const Batch::SearchCompletedEvent &) {
            completedCount.fetch_add(1, std::memory_order_relaxed);
        };
        callbacks.onFailed = [&](const Batch::SearchFailedEvent &) {
            failedCount.fetch_add(1, std::memory_order_relaxed);
        };
        callbacks.onCancelled = [&](const Batch::SearchCancelledEvent &) {
            cancelledCount.fetch_add(1, std::memory_order_relaxed);
        };

        const auto result = Batch::BatchSearchService::Run(request, callbacks);
        std::sort(matchedSeeds.begin(), matchedSeeds.end());

        Expect(!result.failed, "stable run should not fail", failures);
        Expect(!result.cancelled, "stable run should not cancel", failures);
        Expect(result.processedSeeds == 20, "stable run processed seeds mismatch", failures);
        Expect(result.totalMatches == 2, "stable run total matches mismatch", failures);
        Expect(startedCount.load(std::memory_order_relaxed) == 1, "started event count mismatch", failures);
        Expect(completedCount.load(std::memory_order_relaxed) == 1, "completed event count mismatch", failures);
        Expect(failedCount.load(std::memory_order_relaxed) == 0, "failed event should be zero", failures);
        Expect(cancelledCount.load(std::memory_order_relaxed) == 0, "cancelled event should be zero", failures);
        Expect(matchCount.load(std::memory_order_relaxed) == 2, "match event count mismatch", failures);
        Expect(progressCount.load(std::memory_order_relaxed) > 0, "progress event should be emitted", failures);
        Expect(matchedSeeds.size() == 2, "matched seed size mismatch", failures);
        Expect(matchedSeeds.size() == 2 && matchedSeeds[0] == 7 && matchedSeeds[1] == 14,
               "matched seeds should be stable",
               failures);
    }

    {
        std::atomic<bool> cancelRequested{false};

        Batch::SearchRequest request;
        request.seedStart = 1;
        request.seedEnd = 500;
        request.workerCount = 2;
        request.chunkSize = 8;
        request.progressInterval = 1;
        request.sampleWindow = std::chrono::milliseconds(10);
        request.maxRunDuration = std::chrono::milliseconds(0);
        request.enableAdaptive = false;
        request.cancelRequested = &cancelRequested;
        request.evaluateSeed = [](int) {
            Batch::SearchSeedEvaluation evaluation;
            evaluation.generated = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return evaluation;
        };

        std::atomic<int> completedCount{0};
        std::atomic<int> failedCount{0};
        std::atomic<int> cancelledCount{0};

        Batch::SearchEventCallbacks callbacks;
        callbacks.onProgress = [&](const Batch::SearchProgressEvent &event) {
            if (event.processedSeeds >= 12) {
                cancelRequested.store(true, std::memory_order_relaxed);
            }
        };
        callbacks.onCompleted = [&](const Batch::SearchCompletedEvent &) {
            completedCount.fetch_add(1, std::memory_order_relaxed);
        };
        callbacks.onFailed = [&](const Batch::SearchFailedEvent &) {
            failedCount.fetch_add(1, std::memory_order_relaxed);
        };
        callbacks.onCancelled = [&](const Batch::SearchCancelledEvent &) {
            cancelledCount.fetch_add(1, std::memory_order_relaxed);
        };

        const auto result = Batch::BatchSearchService::Run(request, callbacks);

        Expect(result.cancelled, "cancelled run should be cancelled", failures);
        Expect(!result.failed, "cancelled run should not fail", failures);
        Expect(result.processedSeeds < result.totalSeeds, "cancelled run should stop early", failures);
        Expect(cancelledCount.load(std::memory_order_relaxed) == 1, "cancelled event count mismatch", failures);
        Expect(completedCount.load(std::memory_order_relaxed) == 0, "completed event should be zero when cancelled", failures);
        Expect(failedCount.load(std::memory_order_relaxed) == 0, "failed event should be zero when cancelled", failures);
    }

    {
        Batch::SearchRequest request;
        request.seedStart = 1;
        request.seedEnd = 20;
        request.workerCount = 2;
        request.chunkSize = 4;
        request.progressInterval = 1;
        request.sampleWindow = std::chrono::milliseconds(5);
        request.maxRunDuration = std::chrono::milliseconds(0);
        request.enableAdaptive = false;
        request.evaluateSeed = [](int seed) {
            Batch::SearchSeedEvaluation evaluation;
            if (seed == 5) {
                evaluation.ok = false;
                evaluation.errorMessage = "forced failure";
                return evaluation;
            }
            evaluation.generated = true;
            return evaluation;
        };

        std::atomic<int> completedCount{0};
        std::atomic<int> failedCount{0};
        std::atomic<int> cancelledCount{0};

        Batch::SearchEventCallbacks callbacks;
        callbacks.onCompleted = [&](const Batch::SearchCompletedEvent &) {
            completedCount.fetch_add(1, std::memory_order_relaxed);
        };
        callbacks.onFailed = [&](const Batch::SearchFailedEvent &) {
            failedCount.fetch_add(1, std::memory_order_relaxed);
        };
        callbacks.onCancelled = [&](const Batch::SearchCancelledEvent &) {
            cancelledCount.fetch_add(1, std::memory_order_relaxed);
        };

        const auto result = Batch::BatchSearchService::Run(request, callbacks);

        Expect(result.failed, "failed run should be failed", failures);
        Expect(!result.cancelled, "failed run should not be cancelled", failures);
        Expect(!result.failureMessage.empty(), "failed run should have failure message", failures);
        Expect(failedCount.load(std::memory_order_relaxed) == 1, "failed event count mismatch", failures);
        Expect(completedCount.load(std::memory_order_relaxed) == 0, "completed event should be zero when failed", failures);
        Expect(cancelledCount.load(std::memory_order_relaxed) == 0, "cancelled event should be zero when failed", failures);
    }

    {
        std::atomic<int> activeWorkerCap{0};

        Batch::SearchRequest request;
        request.seedStart = 1;
        request.seedEnd = 240;
        request.workerCount = 4;
        request.chunkSize = 1;
        request.progressInterval = 1;
        request.sampleWindow = std::chrono::milliseconds(10);
        request.maxRunDuration = std::chrono::milliseconds(0);
        request.enableAdaptive = false;
        request.activeWorkerCap = &activeWorkerCap;
        request.evaluateSeed = [](int) {
            Batch::SearchSeedEvaluation evaluation;
            evaluation.generated = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
            return evaluation;
        };

        std::vector<int> observedWorkers;
        std::mutex observedWorkersMutex;

        Batch::SearchEventCallbacks callbacks;
        callbacks.onProgress = [&](const Batch::SearchProgressEvent &event) {
            std::lock_guard<std::mutex> lock(observedWorkersMutex);
            observedWorkers.push_back(event.activeWorkers);
        };

        std::thread capThread([&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            activeWorkerCap.store(2, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            activeWorkerCap.store(0, std::memory_order_relaxed);
        });

        const auto result = Batch::BatchSearchService::Run(request, callbacks);
        capThread.join();

        bool sawReduced = false;
        bool sawRecovered = false;
        for (int value : observedWorkers) {
            if (value <= 2) {
                sawReduced = true;
            }
            if (sawReduced && value >= 4) {
                sawRecovered = true;
            }
        }

        Expect(!result.failed, "worker cap run should not fail", failures);
        Expect(!result.cancelled, "worker cap run should not cancel", failures);
        Expect(sawReduced, "worker cap run should observe reduced active workers", failures);
        Expect(sawRecovered, "worker cap run should observe recovered active workers", failures);
    }

    {
        Batch::SearchRequest request;
        request.seedStart = 1;
        request.seedEnd = 3600;
        request.workerCount = 6;
        request.chunkSize = 4;
        request.progressInterval = 4;
        request.sampleWindow = std::chrono::milliseconds(40);
        request.maxRunDuration = std::chrono::milliseconds(0);
        request.enableAdaptive = true;
        request.adaptiveConfig.enabled = true;
        request.adaptiveConfig.minWorkers = 3;
        request.adaptiveConfig.dropThreshold = 0.20;
        request.adaptiveConfig.consecutiveDropWindows = 2;
        request.adaptiveConfig.cooldown = std::chrono::milliseconds(20);
        request.enableRecovery = true;
        request.recoveryConfig.enabled = true;
        request.recoveryConfig.stableWindows = 1;
        request.recoveryConfig.retentionRatio = 0.95;
        request.recoveryConfig.cooldown = std::chrono::milliseconds(15);

        std::atomic<int> counter{0};
        request.evaluateSeed = [&](int) {
            Batch::SearchSeedEvaluation evaluation;
            evaluation.generated = true;
            const int index = counter.fetch_add(1, std::memory_order_relaxed);
            if (index < 240) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } else if (index < 1200) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return evaluation;
        };

        std::vector<int> observedWorkers;
        std::mutex observedWorkersMutex;

        Batch::SearchEventCallbacks callbacks;
        callbacks.onProgress = [&](const Batch::SearchProgressEvent &event) {
            if (!event.hasWindowSample) {
                return;
            }
            std::lock_guard<std::mutex> lock(observedWorkersMutex);
            observedWorkers.push_back(event.activeWorkers);
        };

        const auto result = Batch::BatchSearchService::Run(request, callbacks);

        bool sawReduced = false;
        bool sawRecoveredToInitial = false;
        int maxObservedWorkers = 0;
        for (int value : observedWorkers) {
            maxObservedWorkers = std::max(maxObservedWorkers, value);
            if (value < static_cast<int>(request.workerCount)) {
                sawReduced = true;
            }
            if (sawReduced && value == static_cast<int>(request.workerCount)) {
                sawRecoveredToInitial = true;
            }
        }

        Expect(!result.failed, "bounded recovery run should not fail", failures);
        Expect(!result.cancelled, "bounded recovery run should not cancel", failures);
        Expect(result.processedSeeds == result.totalSeeds,
               "bounded recovery run should process all seeds",
               failures);
        Expect(sawReduced,
               "bounded recovery run should reduce active workers after throughput drops",
               failures);
        Expect(sawRecoveredToInitial,
               "bounded recovery run should recover back to the initial worker count",
               failures);
        Expect(maxObservedWorkers <= static_cast<int>(request.workerCount),
               "bounded recovery run should never exceed the initial worker count",
               failures);
    }

    if (failures == 0) {
        std::cout << "[PASS] test_batch_search_smoke" << std::endl;
        return 0;
    }
    return 1;
}
