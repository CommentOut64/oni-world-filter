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

    if (failures == 0) {
        std::cout << "[PASS] test_batch_search_smoke" << std::endl;
        return 0;
    }
    return 1;
}
