#include "Batch/SessionWarmupPlanner.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <unordered_map>

#include "BatchCpu/CpuOptimization.hpp"

namespace Batch {

namespace {

std::mutex g_cacheMutex;
std::unordered_map<std::string, BatchCpu::ThreadPolicy> g_selectedPolicyCache;

double ComputeMedian(std::vector<double> values)
{
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const size_t middle = values.size() / 2;
    if ((values.size() % 2) == 1) {
        return values[middle];
    }
    return (values[middle - 1] + values[middle]) / 2.0;
}

std::vector<WarmupSampleSegment> BuildSegments(int seedStart,
                                               int seedEnd,
                                               const SessionWarmupPlannerOptions &options)
{
    std::vector<WarmupSampleSegment> segments;
    if (seedEnd < seedStart) {
        return segments;
    }

    const int totalSeeds = seedEnd - seedStart + 1;
    const int desiredSegments = std::clamp(options.segmentCount, 1, totalSeeds);
    const int requestedWarmupSeeds = std::max(1, options.warmupSeedCount);
    const int perSegmentByBudget = std::max(1, requestedWarmupSeeds / desiredSegments);
    const int maxSegmentLengthForCoverage = std::max(1, totalSeeds / desiredSegments);
    const int segmentLength = std::max(
        1,
        std::min({totalSeeds, perSegmentByBudget, maxSegmentLengthForCoverage}));
    const int maxStartOffset = std::max(0, totalSeeds - segmentLength);

    for (int index = 0; index < desiredSegments; ++index) {
        int offset = 0;
        if (desiredSegments > 1) {
            const double ratio =
                static_cast<double>(index) / static_cast<double>(desiredSegments - 1);
            offset = static_cast<int>(std::llround(ratio * maxStartOffset));
        }
        WarmupSampleSegment segment;
        segment.seedStart = seedStart + offset;
        segment.seedEnd = segment.seedStart + segmentLength - 1;
        if (segment.seedEnd > seedEnd) {
            segment.seedEnd = seedEnd;
            segment.seedStart = std::max(seedStart, segment.seedEnd - segmentLength + 1);
        }
        if (!segments.empty() && segments.back().seedStart == segment.seedStart &&
            segments.back().seedEnd == segment.seedEnd) {
            continue;
        }
        segments.push_back(segment);
    }

    if (segments.empty()) {
        segments.push_back(WarmupSampleSegment{
            .seedStart = seedStart,
            .seedEnd = seedStart,
        });
    }
    return segments;
}

BatchCpu::ThroughputStats AggregateStats(const std::vector<BatchCpu::ThroughputStats> &segmentStats,
                                         int minValidSegments,
                                         uint64_t minProcessedSeeds)
{
    BatchCpu::ThroughputStats aggregated;
    std::vector<double> averages;
    std::vector<double> stddevs;
    uint64_t totalProcessedSeeds = 0;
    for (const auto &stats : segmentStats) {
        if (!stats.valid) {
            continue;
        }
        averages.push_back(stats.averageSeedsPerSecond);
        stddevs.push_back(stats.stddevSeedsPerSecond);
        totalProcessedSeeds += stats.processedSeeds;
    }

    if (static_cast<int>(averages.size()) < std::max(1, minValidSegments)) {
        return aggregated;
    }

    aggregated.averageSeedsPerSecond = ComputeMedian(std::move(averages));
    aggregated.stddevSeedsPerSecond = ComputeMedian(std::move(stddevs));
    aggregated.processedSeeds = totalProcessedSeeds;
    aggregated.valid = totalProcessedSeeds >= minProcessedSeeds;
    return aggregated;
}

} // namespace

SessionWarmupResult SelectThreadPolicyWithSessionWarmup(
    const std::string &sessionKey,
    int seedStart,
    int seedEnd,
    const std::vector<BatchCpu::ThreadPolicy> &candidates,
    const SessionWarmupPlannerOptions &options,
    const SessionWarmupEvaluator &evaluator)
{
    SessionWarmupResult result;
    if (candidates.empty()) {
        return result;
    }

    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        const auto itr = g_selectedPolicyCache.find(sessionKey);
        if (!sessionKey.empty() && itr != g_selectedPolicyCache.end()) {
            result.selectedPolicy = itr->second;
            result.usedSessionCache = true;
            return result;
        }
    }

    result.segments = BuildSegments(seedStart, seedEnd, options);
    if (!options.enableWarmup || result.segments.empty()) {
        result.selectedPolicy = candidates.front();
        return result;
    }

    const auto totalBudget = std::max(options.totalBudget, std::chrono::milliseconds(1));
    const auto perCandidateBudget =
        std::max(options.perCandidateBudget, std::chrono::milliseconds(1));
    const auto candidateBudget = std::min(
        perCandidateBudget,
        std::chrono::duration_cast<std::chrono::milliseconds>(
            totalBudget / static_cast<int>(std::max<size_t>(1, candidates.size()))));
    const auto segmentBudget = std::max(
        std::chrono::milliseconds(1),
        std::chrono::duration_cast<std::chrono::milliseconds>(
            candidateBudget / static_cast<int>(std::max<size_t>(1, result.segments.size()))));

    std::vector<BatchCpu::WarmupCandidateResult> summarizedResults;
    summarizedResults.reserve(candidates.size());
    result.warmupResults.reserve(candidates.size());

    for (const auto &candidate : candidates) {
        SessionWarmupCandidateResult candidateResult;
        candidateResult.policy = candidate;
        candidateResult.segmentStats.reserve(result.segments.size());

        for (const auto &segment : result.segments) {
            candidateResult.segmentStats.push_back(evaluator(candidate, segment, segmentBudget));
        }

        candidateResult.aggregatedStats =
            AggregateStats(candidateResult.segmentStats,
                           options.minValidSegments,
                           options.minProcessedSeeds);
        result.warmupResults.push_back(candidateResult);

        BatchCpu::WarmupCandidateResult summarized;
        summarized.policy = candidate;
        summarized.stats = candidateResult.aggregatedStats;
        summarizedResults.push_back(summarized);
    }

    if (summarizedResults.empty()) {
        result.selectedPolicy = candidates.front();
    } else {
        const size_t selectedIndex = BatchCpu::ThroughputCalibrator::PickBestIndex(
            summarizedResults,
            options.tieToleranceRatio);
        if (selectedIndex < result.warmupResults.size()) {
            result.selectedPolicy = result.warmupResults[selectedIndex].policy;
        } else {
            result.selectedPolicy = candidates.front();
        }
    }

    if (!sessionKey.empty()) {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        g_selectedPolicyCache[sessionKey] = result.selectedPolicy;
    }
    return result;
}

void ResetSessionWarmupPlannerCache()
{
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    g_selectedPolicyCache.clear();
}

} // namespace Batch
