#include "Batch/SessionWarmupPlanner.hpp"

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

BatchCpu::ThroughputStats BuildStats(double average,
                                     double stddev,
                                     uint64_t processedSeeds = 256,
                                     bool valid = true)
{
    BatchCpu::ThroughputStats stats;
    stats.averageSeedsPerSecond = average;
    stats.stddevSeedsPerSecond = stddev;
    stats.processedSeeds = processedSeeds;
    stats.valid = valid;
    return stats;
}

double LookupAverage(const BatchCpu::ThreadPolicy &policy,
                     const Batch::WarmupSampleSegment &segment)
{
    if (policy.name == "spiky") {
        if (segment.seedStart <= 100) {
            return 600.0;
        }
        return 100.0;
    }
    if (policy.name == "steady") {
        if (segment.seedStart <= 100) {
            return 220.0;
        }
        if (segment.seedEnd >= 1000) {
            return 215.0;
        }
        return 210.0;
    }
    if (policy.name == "small") {
        return 100.0;
    }
    if (policy.name == "large") {
        return 102.0;
    }
    return 80.0;
}

} // namespace

int RunAllTests()
{
    int failures = 0;

    {
        Batch::ResetSessionWarmupPlannerCache();

        std::vector<BatchCpu::ThreadPolicy> candidates{
            BuildPolicy("baseline", 4),
            BuildPolicy("compare", 6),
        };

        Batch::SessionWarmupPlannerOptions options;
        options.enableWarmup = true;
        options.warmupSeedCount = 90;
        options.segmentCount = 3;
        options.totalBudget = std::chrono::milliseconds(900);
        options.perCandidateBudget = std::chrono::milliseconds(300);

        std::vector<Batch::WarmupSampleSegment> seenSegments;
        const auto result = Batch::SelectThreadPolicyWithSessionWarmup(
            "planner-segments",
            100,
            1099,
            candidates,
            options,
            [&](const BatchCpu::ThreadPolicy &policy,
                const Batch::WarmupSampleSegment &segment,
                std::chrono::milliseconds) {
                if (policy.name == "baseline") {
                    seenSegments.push_back(segment);
                }
                return BuildStats(120.0, 5.0);
            });

        Expect(!result.usedSessionCache, "first segmented warmup should not use cache", failures);
        Expect(result.segments.size() == 3, "long range should produce three warmup segments", failures);
        Expect(seenSegments.size() == 3, "evaluator should receive each planned segment", failures);
        Expect(result.segments.front().seedStart == 100,
               "first warmup segment should start at search start",
               failures);
        Expect(result.segments.back().seedEnd == 1099,
               "last warmup segment should end at search end",
               failures);
        Expect(result.segments[1].seedStart > result.segments.front().seedStart &&
                   result.segments[1].seedEnd < result.segments.back().seedEnd,
               "middle warmup segment should be sampled away from the edges",
               failures);
    }

    {
        Batch::ResetSessionWarmupPlannerCache();

        std::vector<BatchCpu::ThreadPolicy> candidates{
            BuildPolicy("small", 4),
            BuildPolicy("large", 6),
        };

        Batch::SessionWarmupPlannerOptions options;
        options.enableWarmup = true;
        options.warmupSeedCount = 120;
        options.segmentCount = 3;
        options.tieToleranceRatio = 0.03;

        const auto result = Batch::SelectThreadPolicyWithSessionWarmup(
            "planner-tie",
            1,
            3000,
            candidates,
            options,
            [&](const BatchCpu::ThreadPolicy &policy,
                const Batch::WarmupSampleSegment &segment,
                std::chrono::milliseconds) {
                return BuildStats(LookupAverage(policy, segment), 10.0);
            });

        Expect(result.selectedPolicy.name == "small",
               "tie range should prefer the candidate with fewer workers",
               failures);
    }

    {
        Batch::ResetSessionWarmupPlannerCache();

        std::vector<BatchCpu::ThreadPolicy> candidates{
            BuildPolicy("spiky", 8),
            BuildPolicy("steady", 6),
        };

        Batch::SessionWarmupPlannerOptions options;
        options.enableWarmup = true;
        options.warmupSeedCount = 150;
        options.segmentCount = 3;
        options.tieToleranceRatio = 0.02;

        const auto result = Batch::SelectThreadPolicyWithSessionWarmup(
            "planner-spiky-vs-steady",
            100,
            1099,
            candidates,
            options,
            [&](const BatchCpu::ThreadPolicy &policy,
                const Batch::WarmupSampleSegment &segment,
                std::chrono::milliseconds) {
                return BuildStats(LookupAverage(policy, segment), 12.0);
            });

        Expect(result.selectedPolicy.name == "steady",
               "single hot segment should not dominate the aggregated warmup result",
               failures);
    }

    {
        Batch::ResetSessionWarmupPlannerCache();

        std::vector<BatchCpu::ThreadPolicy> candidates{
            BuildPolicy("under-sampled-fast", 8),
            BuildPolicy("qualified-steady", 6),
        };

        Batch::SessionWarmupPlannerOptions options;
        options.enableWarmup = true;
        options.warmupSeedCount = 150;
        options.segmentCount = 3;
        options.minProcessedSeeds = 180;

        const auto result = Batch::SelectThreadPolicyWithSessionWarmup(
            "planner-min-sampled-seeds",
            100,
            1099,
            candidates,
            options,
            [&](const BatchCpu::ThreadPolicy &policy,
                const Batch::WarmupSampleSegment &segment,
                std::chrono::milliseconds) {
                if (policy.name == "under-sampled-fast") {
                    return BuildStats(500.0, 4.0, 40);
                }
                return BuildStats(210.0 + static_cast<double>(segment.seedStart % 3), 6.0, 80);
            });

        Expect(result.selectedPolicy.name == "qualified-steady",
               "aggregated sampled seeds below threshold should invalidate an otherwise faster candidate",
               failures);
    }

    {
        Batch::ResetSessionWarmupPlannerCache();

        std::vector<BatchCpu::ThreadPolicy> candidates{
            BuildPolicy("cached", 5),
            BuildPolicy("fallback", 4),
        };

        Batch::SessionWarmupPlannerOptions options;
        options.enableWarmup = true;
        options.warmupSeedCount = 90;
        options.segmentCount = 3;

        std::atomic<int> evaluateCount{0};
        const auto evaluator = [&](const BatchCpu::ThreadPolicy &policy,
                                   const Batch::WarmupSampleSegment &segment,
                                   std::chrono::milliseconds) {
            (void)policy;
            (void)segment;
            evaluateCount.fetch_add(1, std::memory_order_relaxed);
            return BuildStats(180.0, 8.0);
        };

        const auto first = Batch::SelectThreadPolicyWithSessionWarmup(
            "planner-cache",
            1,
            1000,
            candidates,
            options,
            evaluator);
        const int firstCount = evaluateCount.load(std::memory_order_relaxed);
        const auto second = Batch::SelectThreadPolicyWithSessionWarmup(
            "planner-cache",
            1,
            1000,
            candidates,
            options,
            evaluator);

        Expect(!first.usedSessionCache, "first planner selection should miss cache", failures);
        Expect(second.usedSessionCache, "second planner selection should hit cache", failures);
        Expect(firstCount > 0, "first planner selection should evaluate at least one segment", failures);
        Expect(evaluateCount.load(std::memory_order_relaxed) == firstCount,
               "cached planner selection should not re-evaluate segments",
               failures);
    }

    {
        Batch::ResetSessionWarmupPlannerCache();

        std::vector<BatchCpu::ThreadPolicy> candidates{
            BuildPolicy("short", 2),
        };

        Batch::SessionWarmupPlannerOptions options;
        options.enableWarmup = true;
        options.warmupSeedCount = 4000;
        options.segmentCount = 3;

        const auto result = Batch::SelectThreadPolicyWithSessionWarmup(
            "planner-short-range",
            5,
            6,
            candidates,
            options,
            [&](const BatchCpu::ThreadPolicy &policy,
                const Batch::WarmupSampleSegment &segment,
                std::chrono::milliseconds) {
                (void)policy;
                return BuildStats(90.0 + static_cast<double>(segment.seedStart), 3.0, 1);
            });

        Expect(result.segments.size() == 2,
               "two-seed range should collapse to the minimal legal segment count",
               failures);
        for (const auto &segment : result.segments) {
            Expect(segment.seedStart >= 5 && segment.seedEnd <= 6,
                   "short range segment should stay within the requested bounds",
                   failures);
            Expect(segment.seedStart <= segment.seedEnd,
                   "short range segment should remain valid after collapsing",
                   failures);
        }
    }

    Batch::ResetSessionWarmupPlannerCache();

    if (failures == 0) {
        std::cout << "[PASS] test_session_warmup_planner" << std::endl;
        return 0;
    }
    return 1;
}
