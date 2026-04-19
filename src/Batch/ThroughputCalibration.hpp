#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include "Batch/ThreadPolicy.hpp"

namespace Batch {

struct ThroughputCalibrationOptions {
    bool enableWarmup = true;
    std::chrono::milliseconds totalBudget{10000};
    std::chrono::milliseconds perCandidateBudget{2500};
    double tieToleranceRatio = 0.03;
};

struct ThroughputCalibrationResult {
    BatchCpu::ThreadPolicy selectedPolicy;
    std::vector<BatchCpu::WarmupCandidateResult> warmupResults;
    bool usedSessionCache = false;
};

using ThroughputEvaluator = std::function<BatchCpu::ThroughputStats(
    const BatchCpu::ThreadPolicy &policy,
    std::chrono::milliseconds budget)>;

ThroughputCalibrationResult SelectThreadPolicyWithWarmup(
    const std::string &sessionKey,
    const std::vector<BatchCpu::ThreadPolicy> &candidates,
    const ThroughputCalibrationOptions &options,
    const ThroughputEvaluator &evaluator);

void ResetThroughputCalibrationSession();

} // namespace Batch

