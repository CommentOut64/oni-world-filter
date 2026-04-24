#include "Batch/BatchSearchService.hpp"
#include "BatchCpu/SearchCpuGovernor.hpp"
#include "BatchCpu/SearchCpuPlan.hpp"

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

BatchCpu::CpuTopologyFacts BuildSmt2Topology()
{
    BatchCpu::CpuTopologyFacts topology;
    topology.detectionSucceeded = true;
    topology.isHeterogeneous = false;
    topology.diagnostics = "adaptive smt2 topology";
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
    topology.diagnostics = "adaptive non-smt topology";
    topology.physicalCoresBySystemOrder = std::vector<BatchCpu::PhysicalCoreFacts>{
        {.physicalCoreIndex = 0,
         .group = 0,
         .efficiencyClass = 0,
         .isHighPerformance = true,
         .logicalThreads = {{.logicalIndex = 0, .group = 0, .isPrimaryThread = true}}},
        {.physicalCoreIndex = 1,
         .group = 0,
         .efficiencyClass = 0,
         .isHighPerformance = true,
         .logicalThreads = {{.logicalIndex = 1, .group = 0, .isPrimaryThread = true}}},
        {.physicalCoreIndex = 2,
         .group = 0,
         .efficiencyClass = 0,
         .isHighPerformance = true,
         .logicalThreads = {{.logicalIndex = 2, .group = 0, .isPrimaryThread = true}}},
        {.physicalCoreIndex = 3,
         .group = 0,
         .efficiencyClass = 0,
         .isHighPerformance = true,
         .logicalThreads = {{.logicalIndex = 3, .group = 0, .isPrimaryThread = true}}},
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
    request.seedEnd = 400;
    request.cpuPlan = plan;
    request.chunkSize = 8;
    request.progressInterval = 8;
    request.sampleWindow = std::chrono::milliseconds(80);
    request.cpuGovernorConfig.enabled = true;
    request.cpuGovernorConfig.minActivePhysicalCores = 1;
    request.cpuGovernorConfig.scaleDownThreshold = 0.08;
    request.cpuGovernorConfig.scaleDownWindowCount = 1;
    request.cpuGovernorConfig.scaleUpWindowCount = 100;
    request.cpuGovernorConfig.scaleUpRetentionRatio = 0.95;
    request.cpuGovernorConfig.cooldown = std::chrono::milliseconds(50);
    return request;
}

} // namespace

int RunAllTests()
{
    int failures = 0;

    {
        auto request = BuildBaseRequest(BuildBalancedPlan(BuildSmt2Topology(), true));
        request.seedEnd = 100000;
        request.maxRunDuration = std::chrono::milliseconds(700);
        request.cpuGovernorConfig.enabled = false;

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
            activeWorkerCap.store(2, std::memory_order_relaxed);
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
               "external host cap should not trigger governor downscale",
               failures);
        Expect(result.finalActiveWorkers == 6,
               "active workers should recover to the startup worker count after host cap is released",
               failures);
        Expect(reducedEvents.load(std::memory_order_relaxed) == 0,
               "host cap windows should not emit governor reduction events",
               failures);
    }

    {
        auto request = BuildBaseRequest(BuildBalancedPlan(BuildNonSmtTopology(), false));
        request.progressInterval = 4;

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
        std::vector<int> observedWorkers;
        Batch::SearchEventCallbacks callbacks;
        callbacks.onProgress = [&](const Batch::SearchProgressEvent &event) {
            if (event.hasWindowSample) {
                observedWorkers.push_back(event.activeWorkers);
            }
            if (event.activeWorkersReduced) {
                reducedEvents.fetch_add(1, std::memory_order_relaxed);
            }
        };

        const auto result = Batch::BatchSearchService::Run(request, callbacks);
        bool sawSingleCoreReduction = false;
        for (size_t i = 1; i < observedWorkers.size(); ++i) {
            if (observedWorkers[i - 1] == 3 && observedWorkers[i] == 2) {
                sawSingleCoreReduction = true;
                break;
            }
        }

        Expect(!result.failed, "non-SMT adaptive run should not fail", failures);
        Expect(!result.cancelled, "non-SMT adaptive run should not cancel", failures);
        Expect(result.processedSeeds == result.totalSeeds, "non-SMT adaptive run should process all seeds", failures);
        Expect(result.autoFallbackCount > 0, "non-SMT adaptive run should trigger governor reduction", failures);
        Expect(sawSingleCoreReduction,
               "non-SMT adaptive run should contain a 3->2 worker transition for one physical-core reduction",
               failures);
        Expect(reducedEvents.load(std::memory_order_relaxed) > 0,
               "non-SMT adaptive run should emit reduced progress events",
               failures);
    }

    {
        auto request = BuildBaseRequest(BuildBalancedPlan(BuildSmt2Topology(), true));

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
        std::vector<int> observedWorkers;
        Batch::SearchEventCallbacks callbacks;
        callbacks.onProgress = [&](const Batch::SearchProgressEvent &event) {
            if (event.hasWindowSample) {
                observedWorkers.push_back(event.activeWorkers);
            }
            if (event.activeWorkersReduced) {
                reducedEvents.fetch_add(1, std::memory_order_relaxed);
            }
        };

        const auto result = Batch::BatchSearchService::Run(request, callbacks);
        bool sawTwoWorkerReduction = false;
        for (size_t i = 1; i < observedWorkers.size(); ++i) {
            if (observedWorkers[i - 1] == 6 && observedWorkers[i] == 4) {
                sawTwoWorkerReduction = true;
                break;
            }
        }

        Expect(!result.failed, "adaptive run should not fail", failures);
        Expect(!result.cancelled, "adaptive run should not cancel", failures);
        Expect(result.processedSeeds == result.totalSeeds, "adaptive run should process all seeds", failures);
        Expect(result.totalSeeds == 400, "adaptive run total seed count mismatch", failures);
        Expect(result.autoFallbackCount > 0, "adaptive run should trigger governor reduction", failures);
        Expect(sawTwoWorkerReduction,
               "adaptive run should contain a 6->4 worker transition for one physical-core reduction on SMT=2 topology",
               failures);
        Expect(reducedEvents.load(std::memory_order_relaxed) > 0,
               "adaptive run should emit reduced progress events",
               failures);
    }

    if (failures == 0) {
        std::cout << "[PASS] test_adaptive_concurrency" << std::endl;
        return 0;
    }
    return 1;
}
