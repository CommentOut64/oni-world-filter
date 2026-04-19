#include "Batch/ThroughputCalibration.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
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

BatchCpu::ThreadPolicy BuildPolicy(const std::string &name, uint32_t workers)
{
    BatchCpu::ThreadPolicy policy;
    policy.name = name;
    policy.workerCount = workers;
    for (uint32_t i = 0; i < workers; ++i) {
        policy.targetLogicalProcessors.push_back(i);
    }
    return policy;
}

} // namespace

int RunAllTests()
{
    int failures = 0;

    Batch::ResetThroughputCalibrationSession();

    std::vector<BatchCpu::ThreadPolicy> candidates;
    candidates.push_back(BuildPolicy("balanced", 4));
    candidates.push_back(BuildPolicy("throughput", 6));

    std::atomic<int> evaluateCount{0};
    Batch::ThroughputCalibrationOptions options;
    options.enableWarmup = true;
    options.totalBudget = std::chrono::milliseconds(1200);
    options.perCandidateBudget = std::chrono::milliseconds(600);
    options.tieToleranceRatio = 0.01;

    auto evaluator = [&](const BatchCpu::ThreadPolicy &policy, std::chrono::milliseconds) {
        evaluateCount.fetch_add(1, std::memory_order_relaxed);
        BatchCpu::ThroughputStats stats;
        stats.valid = true;
        stats.processedSeeds = 4096;
        stats.averageSeedsPerSecond =
            policy.name == "throughput" ? 1600.0 : 1200.0;
        stats.stddevSeedsPerSecond =
            policy.name == "throughput" ? 30.0 : 25.0;
        return stats;
    };

    const auto first = Batch::SelectThreadPolicyWithWarmup(
        "throughput-calibration-test-session",
        candidates,
        options,
        evaluator);
    Expect(!first.warmupResults.empty(), "warmup results should not be empty", failures);
    Expect(first.selectedPolicy.name == "throughput", "throughput policy should be selected", failures);
    Expect(!first.usedSessionCache, "first selection should not use cache", failures);
    Expect(evaluateCount.load(std::memory_order_relaxed) >= 2, "first selection should evaluate candidates", failures);

    const int firstCount = evaluateCount.load(std::memory_order_relaxed);
    const auto second = Batch::SelectThreadPolicyWithWarmup(
        "throughput-calibration-test-session",
        candidates,
        options,
        evaluator);
    Expect(second.usedSessionCache, "second selection should use cache", failures);
    Expect(second.selectedPolicy.name == "throughput", "cached selected policy should be throughput", failures);
    Expect(evaluateCount.load(std::memory_order_relaxed) == firstCount,
           "cached selection should not call evaluator again",
           failures);

    Batch::ResetThroughputCalibrationSession();

    if (failures == 0) {
        std::cout << "[PASS] test_throughput_calibration" << std::endl;
        return 0;
    }
    return 1;
}

