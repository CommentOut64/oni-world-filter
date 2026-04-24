#include "Batch/BatchSearchService.hpp"
#include "BatchCpu/SearchCpuGovernor.hpp"
#include "BatchCpu/SearchCpuPlan.hpp"

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

void BusyWaitFor(std::chrono::microseconds duration)
{
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < deadline) {
    }
}

BatchCpu::CpuTopologyFacts BuildSmt2Topology()
{
    BatchCpu::CpuTopologyFacts topology;
    topology.detectionSucceeded = true;
    topology.isHeterogeneous = false;
    topology.diagnostics = "smoke smt2 topology";
    topology.physicalCoresBySystemOrder = std::vector<BatchCpu::PhysicalCoreFacts>{
        {.physicalCoreIndex = 0,
         .group = 0,
         .efficiencyClass = 0,
         .isHighPerformance = true,
         .logicalThreads = {
             {.logicalIndex = 0, .group = 0, .isPrimaryThread = true},
             {.logicalIndex = 1, .group = 0, .isPrimaryThread = false},
         }},
        {.physicalCoreIndex = 1,
         .group = 0,
         .efficiencyClass = 0,
         .isHighPerformance = true,
         .logicalThreads = {
             {.logicalIndex = 2, .group = 0, .isPrimaryThread = true},
             {.logicalIndex = 3, .group = 0, .isPrimaryThread = false},
         }},
        {.physicalCoreIndex = 2,
         .group = 0,
         .efficiencyClass = 0,
         .isHighPerformance = true,
         .logicalThreads = {
             {.logicalIndex = 4, .group = 0, .isPrimaryThread = true},
             {.logicalIndex = 5, .group = 0, .isPrimaryThread = false},
         }},
        {.physicalCoreIndex = 3,
         .group = 0,
         .efficiencyClass = 0,
         .isHighPerformance = true,
         .logicalThreads = {
             {.logicalIndex = 6, .group = 0, .isPrimaryThread = true},
             {.logicalIndex = 7, .group = 0, .isPrimaryThread = false},
         }},
    };
    return topology;
}

BatchCpu::CpuTopologyFacts BuildNonSmtTopology()
{
    BatchCpu::CpuTopologyFacts topology;
    topology.detectionSucceeded = true;
    topology.isHeterogeneous = false;
    topology.diagnostics = "smoke non-smt topology";
    topology.physicalCoresBySystemOrder = std::vector<BatchCpu::PhysicalCoreFacts>{
        {.physicalCoreIndex = 0,
         .group = 0,
         .efficiencyClass = 0,
         .isHighPerformance = true,
         .logicalThreads = {
             {.logicalIndex = 0, .group = 0, .isPrimaryThread = true},
         }},
        {.physicalCoreIndex = 1,
         .group = 0,
         .efficiencyClass = 0,
         .isHighPerformance = true,
         .logicalThreads = {
             {.logicalIndex = 1, .group = 0, .isPrimaryThread = true},
         }},
        {.physicalCoreIndex = 2,
         .group = 0,
         .efficiencyClass = 0,
         .isHighPerformance = true,
         .logicalThreads = {
             {.logicalIndex = 2, .group = 0, .isPrimaryThread = true},
         }},
        {.physicalCoreIndex = 3,
         .group = 0,
         .efficiencyClass = 0,
         .isHighPerformance = true,
         .logicalThreads = {
             {.logicalIndex = 3, .group = 0, .isPrimaryThread = true},
         }},
    };
    return topology;
}

BatchCpu::CompiledSearchCpuPlan BuildBalancedPlan(const BatchCpu::CpuTopologyFacts &topology,
                                                  bool allowSmt)
{
    BatchCpu::CpuPolicySpec spec;
    spec.mode = BatchCpu::CpuMode::Balanced;
    spec.allowSmt = allowSmt;
    spec.allowLowPerf = true;
    spec.binding = BatchCpu::PlacementMode::None;
    return BatchCpu::CompileSearchCpuPlan(topology, spec);
}

Batch::SearchRequest BuildBaseRequest(const BatchCpu::CompiledSearchCpuPlan &plan)
{
    Batch::SearchRequest request;
    request.seedStart = 1;
    request.seedEnd = 20;
    request.cpuPlan = plan;
    request.cpuGovernorConfig.enabled = false;
    request.chunkSize = 4;
    request.progressInterval = 2;
    request.sampleWindow = std::chrono::milliseconds(10);
    request.maxRunDuration = std::chrono::milliseconds(0);
    return request;
}

} // namespace

int RunAllTests()
{
    int failures = 0;

    {
        const auto plan = BuildBalancedPlan(BuildSmt2Topology(), true);
        auto request = BuildBaseRequest(plan);
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
        std::atomic<int> startedWorkerCount{0};
        std::atomic<int> progressCount{0};
        std::atomic<int> matchCount{0};
        std::atomic<int> completedCount{0};
        std::atomic<int> failedCount{0};
        std::atomic<int> cancelledCount{0};
        std::vector<int> matchedSeeds;
        std::mutex matchedSeedsMutex;

        Batch::SearchEventCallbacks callbacks;
        callbacks.onStarted = [&](const Batch::SearchStartedEvent &event) {
            startedCount.fetch_add(1, std::memory_order_relaxed);
            startedWorkerCount.store(event.workerCount, std::memory_order_relaxed);
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
        Expect(result.finalActiveWorkers == 6,
               "stable run final active workers should equal startup physical cores derived worker count",
               failures);
        Expect(startedWorkerCount.load(std::memory_order_relaxed) == 6,
               "started event workerCount should equal absolute worker cap",
               failures);
        Expect(startedCount.load(std::memory_order_relaxed) == 1, "started event count mismatch", failures);
        Expect(completedCount.load(std::memory_order_relaxed) == 1, "completed event count mismatch", failures);
        Expect(failedCount.load(std::memory_order_relaxed) == 0, "failed event should be zero", failures);
        Expect(cancelledCount.load(std::memory_order_relaxed) == 0, "cancelled event should be zero", failures);
        Expect(matchCount.load(std::memory_order_relaxed) == 2, "match event count mismatch", failures);
        Expect(progressCount.load(std::memory_order_relaxed) > 0, "progress event should be emitted", failures);
        Expect(matchedSeeds.size() == 2 && matchedSeeds[0] == 7 && matchedSeeds[1] == 14,
               "matched seeds should be stable",
               failures);
    }

    {
        const auto plan = BuildBalancedPlan(BuildNonSmtTopology(), false);
        auto request = BuildBaseRequest(plan);
        request.seedEnd = 32;
        request.chunkSize = 1;
        request.progressInterval = 1;
        request.sampleWindow = std::chrono::milliseconds(5);
        std::atomic<int> initializedWorkers{0};
        std::atomic<int> startedObservedInitializedWorkers{-1};
        request.initializeWorker = [&]() {
            initializedWorkers.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        };
        request.evaluateSeed = [](int) {
            Batch::SearchSeedEvaluation evaluation;
            evaluation.generated = true;
            return evaluation;
        };

        Batch::SearchEventCallbacks callbacks;
        callbacks.onStarted = [&](const Batch::SearchStartedEvent &) {
            startedObservedInitializedWorkers.store(
                initializedWorkers.load(std::memory_order_relaxed),
                std::memory_order_relaxed);
        };

        const auto result = Batch::BatchSearchService::Run(request, callbacks);

        Expect(!result.failed, "startup barrier run should not fail", failures);
        Expect(!result.cancelled, "startup barrier run should not cancel", failures);
        Expect(result.processedSeeds == result.totalSeeds,
               "startup barrier run should process all seeds",
               failures);
        Expect(startedObservedInitializedWorkers.load(std::memory_order_relaxed) ==
                   static_cast<int>(plan.envelope.absoluteWorkerCap),
               "started event should fire after every worker finishes initialization",
               failures);
    }

    {
        const auto plan = BuildBalancedPlan(BuildNonSmtTopology(), false);
        auto request = BuildBaseRequest(plan);
        request.seedEnd = 16;
        std::atomic<bool> placementApplied{false};
        request.applyThreadPlacement = [&](uint32_t, std::string *) {
            placementApplied.store(true, std::memory_order_relaxed);
            return true;
        };
        request.initializeWorker = [&]() {
            if (!placementApplied.load(std::memory_order_relaxed)) {
                throw std::runtime_error("placement should run before initializeWorker");
            }
        };
        request.evaluateSeed = [](int) {
            Batch::SearchSeedEvaluation evaluation;
            evaluation.generated = true;
            return evaluation;
        };

        const auto result = Batch::BatchSearchService::Run(request, {});

        Expect(!result.failed,
               "thread placement should run before initializeWorker",
               failures);
        Expect(result.processedSeeds == result.totalSeeds,
               "placement-before-init run should process all seeds",
               failures);
    }

    {
        const auto plan = BuildBalancedPlan(BuildSmt2Topology(), true);
        auto request = BuildBaseRequest(plan);
        request.seedEnd = 500;
        request.chunkSize = 8;
        request.progressInterval = 1;
        std::atomic<bool> cancelRequested{false};
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
        const auto plan = BuildBalancedPlan(BuildSmt2Topology(), true);
        auto request = BuildBaseRequest(plan);
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
        const auto plan = BuildBalancedPlan(BuildNonSmtTopology(), false);
        auto request = BuildBaseRequest(plan);
        request.seedEnd = 320;
        request.chunkSize = 2;
        request.evaluateSeed = [](int) {
            Batch::SearchSeedEvaluation evaluation;
            evaluation.generated = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            return evaluation;
        };

        std::vector<int> observedWorkers;
        std::mutex observedWorkersMutex;

        Batch::SearchEventCallbacks callbacks;
        callbacks.onProgress = [&](const Batch::SearchProgressEvent &event) {
            std::lock_guard<std::mutex> lock(observedWorkersMutex);
            observedWorkers.push_back(event.activeWorkers);
        };

        const auto result = Batch::BatchSearchService::Run(request, callbacks);

        int maxObservedWorkers = 0;
        for (int value : observedWorkers) {
            maxObservedWorkers = std::max(maxObservedWorkers, value);
        }

        Expect(!result.failed, "non-SMT startup run should not fail", failures);
        Expect(!result.cancelled, "non-SMT startup run should not cancel", failures);
        Expect(result.processedSeeds == result.totalSeeds,
               "non-SMT startup run should process all seeds",
               failures);
        Expect(maxObservedWorkers <= 3,
               "non-SMT startup run should expose one worker per active physical core",
               failures);
    }

    {
        const auto plan = BuildBalancedPlan(BuildSmt2Topology(), true);
        auto request = BuildBaseRequest(plan);
        request.seedEnd = 240;
        request.chunkSize = 1;
        request.progressInterval = 1;
        std::atomic<int> activeWorkerCap{0};
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
            if (value <= 4) {
                sawReduced = true;
            }
            if (sawReduced && value >= 6) {
                sawRecovered = true;
            }
        }

        Expect(!result.failed, "worker cap run should not fail", failures);
        Expect(!result.cancelled, "worker cap run should not cancel", failures);
        Expect(sawReduced, "worker cap run should observe reduced active workers from active physical core cap", failures);
        Expect(sawRecovered, "worker cap run should observe recovered active workers after cap release", failures);
    }

    {
        const auto plan = BuildBalancedPlan(BuildSmt2Topology(), true);
        auto request = BuildBaseRequest(plan);
        request.seedEnd = 3600;
        request.progressInterval = 4;
        request.sampleWindow = std::chrono::milliseconds(40);
        request.cpuGovernorConfig.enabled = true;
        request.cpuGovernorConfig.minActivePhysicalCores = 1;
        request.cpuGovernorConfig.scaleDownThreshold = 0.20;
        request.cpuGovernorConfig.scaleDownWindowCount = 1;
        request.cpuGovernorConfig.scaleUpWindowCount = 2;
        request.cpuGovernorConfig.scaleUpRetentionRatio = 0.95;
        request.cpuGovernorConfig.cooldown = std::chrono::milliseconds(15);

        std::atomic<int> counter{0};
        request.evaluateSeed = [&](int) {
            Batch::SearchSeedEvaluation evaluation;
            evaluation.generated = true;
            const int index = counter.fetch_add(1, std::memory_order_relaxed);
            // 用 busy-wait 避免 Windows sleep 粒度把“快/慢/快”三段负载压平。
            if (index < 240) {
                BusyWaitFor(std::chrono::microseconds(2000));
            } else if (index < 1200) {
                BusyWaitFor(std::chrono::microseconds(8000));
            } else {
                BusyWaitFor(std::chrono::microseconds(2000));
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
        bool sawRecoveredToStartup = false;
        for (int value : observedWorkers) {
            if (value <= 4) {
                sawReduced = true;
            }
            if (sawReduced && value >= 6) {
                sawRecoveredToStartup = true;
            }
        }

        Expect(!result.failed, "governor recovery run should not fail", failures);
        Expect(!result.cancelled, "governor recovery run should not cancel", failures);
        Expect(result.processedSeeds == result.totalSeeds,
               "governor recovery run should process all seeds",
               failures);
        Expect(sawReduced,
               "governor recovery run should reduce active workers after throughput drops",
               failures);
        Expect(sawRecoveredToStartup,
               "governor recovery run should recover back to startup worker count",
               failures);
    }

    if (failures == 0) {
        std::cout << "[PASS] test_batch_search_smoke" << std::endl;
        return 0;
    }
    return 1;
}
