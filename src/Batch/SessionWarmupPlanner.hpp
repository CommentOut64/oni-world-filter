#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include "Batch/ThreadPolicy.hpp"

namespace Batch {

struct WarmupSampleSegment {
    int seedStart = 0;
    int seedEnd = -1;
};

struct SessionWarmupPlannerOptions {
    bool enableWarmup = true;
    int warmupSeedCount = 4000;
    int segmentCount = 3;
    int minValidSegments = 1;
    uint64_t minProcessedSeeds = 0;
    std::chrono::milliseconds totalBudget{10000};
    std::chrono::milliseconds perCandidateBudget{2500};
    double tieToleranceRatio = 0.03;
};

struct SessionWarmupCandidateResult {
    BatchCpu::ThreadPolicy policy;
    BatchCpu::ThroughputStats aggregatedStats;
    std::vector<BatchCpu::ThroughputStats> segmentStats;
};

struct SessionWarmupResult {
    BatchCpu::ThreadPolicy selectedPolicy;
    std::vector<WarmupSampleSegment> segments;
    std::vector<SessionWarmupCandidateResult> warmupResults;
    bool usedSessionCache = false;
};

using SessionWarmupEvaluator = std::function<BatchCpu::ThroughputStats(
    const BatchCpu::ThreadPolicy &policy,
    const WarmupSampleSegment &segment,
    std::chrono::milliseconds budget)>;

SessionWarmupResult SelectThreadPolicyWithSessionWarmup(
    const std::string &sessionKey,
    int seedStart,
    int seedEnd,
    const std::vector<BatchCpu::ThreadPolicy> &candidates,
    const SessionWarmupPlannerOptions &options,
    const SessionWarmupEvaluator &evaluator);

void ResetSessionWarmupPlannerCache();

} // namespace Batch
