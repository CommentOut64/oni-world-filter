#include "Batch/ThroughputCalibration.hpp"

#include <mutex>
#include <unordered_map>

namespace Batch {

namespace {

std::mutex g_cacheMutex;
std::unordered_map<std::string, BatchCpu::ThreadPolicy> g_selectedPolicyCache;

} // namespace

ThroughputCalibrationResult SelectThreadPolicyWithWarmup(
    const std::string &sessionKey,
    const std::vector<BatchCpu::ThreadPolicy> &candidates,
    const ThroughputCalibrationOptions &options,
    const ThroughputEvaluator &evaluator)
{
    ThroughputCalibrationResult result;
    if (candidates.empty()) {
        return result;
    }

    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        const auto itr = g_selectedPolicyCache.find(sessionKey);
        if (itr != g_selectedPolicyCache.end()) {
            result.selectedPolicy = itr->second;
            result.usedSessionCache = true;
            return result;
        }
    }

    BatchCpu::WarmupConfig warmup;
    warmup.enabled = options.enableWarmup;
    warmup.totalBudget = options.totalBudget;
    warmup.perCandidateBudget = options.perCandidateBudget;
    warmup.tieToleranceRatio = options.tieToleranceRatio;

    result.warmupResults = BatchCpu::ThroughputCalibrator::Evaluate(
        candidates,
        warmup,
        evaluator);

    if (result.warmupResults.empty()) {
        result.selectedPolicy = candidates.front();
    } else {
        const size_t selectedIndex = BatchCpu::ThroughputCalibrator::PickBestIndex(
            result.warmupResults,
            options.tieToleranceRatio);
        if (selectedIndex < result.warmupResults.size()) {
            result.selectedPolicy = result.warmupResults[selectedIndex].policy;
        } else {
            result.selectedPolicy = result.warmupResults.front().policy;
        }
    }

    if (!sessionKey.empty()) {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        g_selectedPolicyCache[sessionKey] = result.selectedPolicy;
    }

    return result;
}

void ResetThroughputCalibrationSession()
{
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    g_selectedPolicyCache.clear();
}

} // namespace Batch

