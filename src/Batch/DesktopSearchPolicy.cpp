#include "Batch/DesktopSearchPolicy.hpp"

#include <algorithm>
#include <string_view>

namespace Batch {

namespace {

constexpr uint32_t kStartupRatioNumerator = 9;
constexpr uint32_t kStartupRatioDenominator = 10;

const BatchCpu::ThreadPolicy *FindByName(const std::vector<BatchCpu::ThreadPolicy> &candidates,
                                         std::string_view name)
{
    const auto itr = std::find_if(candidates.begin(),
                                  candidates.end(),
                                  [name](const BatchCpu::ThreadPolicy &candidate) {
                                      return candidate.name == name;
                                  });
    return itr == candidates.end() ? nullptr : &(*itr);
}

BatchCpu::ThreadPolicy SelectLargestCandidate(const std::vector<BatchCpu::ThreadPolicy> &candidates)
{
    if (candidates.empty()) {
        return BatchCpu::ThreadPolicy{};
    }
    const auto itr = std::max_element(candidates.begin(),
                                      candidates.end(),
                                      [](const BatchCpu::ThreadPolicy &left,
                                         const BatchCpu::ThreadPolicy &right) {
                                          if (left.workerCount != right.workerCount) {
                                              return left.workerCount < right.workerCount;
                                          }
                                          return left.name < right.name;
                                      });
    return *itr;
}

BatchCpu::RecoveryConfig BuildDesktopRecoveryConfig()
{
    BatchCpu::RecoveryConfig config;
    config.enabled = true;
    config.stableWindows = 1;
    config.retentionRatio = 0.95;
    config.cooldown = std::chrono::milliseconds(500);
    return config;
}

DesktopSearchExecutionPlan BuildSinglePolicyPlan(const BatchCpu::ThreadPolicy &policy)
{
    DesktopSearchExecutionPlan plan;
    plan.runtimePolicy = policy;
    plan.initialActiveWorkers = std::max<uint32_t>(1u, policy.workerCount);
    plan.enableRecovery = false;
    return plan;
}

BatchCpu::ThreadPolicy SelectBalancedRuntimePolicy(
    const CpuTopology &topology,
    const std::vector<BatchCpu::ThreadPolicy> &candidates)
{
    if (topology.isHeterogeneous) {
        if (const auto *full = FindByName(candidates, "balanced-p-core-plus-smt")) {
            return *full;
        }
        if (const auto *base = FindByName(candidates, "balanced-p-core")) {
            return *base;
        }
    } else {
        if (const auto *full = FindByName(candidates, "balanced-physical-plus-smt")) {
            return *full;
        }
        if (const auto *base = FindByName(candidates, "balanced-physical")) {
            return *base;
        }
    }

    return SelectLargestCandidate(candidates);
}

BatchCpu::ThreadPolicy SelectTurboDesktopPolicy(
    const std::vector<BatchCpu::ThreadPolicy> &candidates)
{
    if (const auto *turboAllCandidates = FindByName(candidates, "turbo-all-candidates")) {
        return *turboAllCandidates;
    }
    if (const auto *turboAllLogical = FindByName(candidates, "turbo-all-logical")) {
        return *turboAllLogical;
    }
    return SelectLargestCandidate(candidates);
}

uint32_t ComputeStartupWorkers(uint32_t runtimeWorkers)
{
    return std::max<uint32_t>(
        1u,
        (runtimeWorkers * kStartupRatioNumerator + (kStartupRatioDenominator - 1)) /
            kStartupRatioDenominator);
}

DesktopSearchExecutionPlan FinalizePlan(const BatchCpu::ThreadPolicy &runtimePolicy,
                                        uint32_t initialActiveWorkers)
{
    DesktopSearchExecutionPlan plan;
    plan.runtimePolicy = runtimePolicy;
    const uint32_t runtimeWorkers = std::max<uint32_t>(1u, runtimePolicy.workerCount);
    plan.initialActiveWorkers = std::clamp(initialActiveWorkers, 1u, runtimeWorkers);
    plan.enableRecovery = plan.initialActiveWorkers < runtimeWorkers;
    if (plan.enableRecovery) {
        plan.recoveryConfig = BuildDesktopRecoveryConfig();
    }
    return plan;
}

} // namespace

DesktopSearchExecutionPlan BuildDesktopSearchExecutionPlan(
    const ThreadPolicyRequest &request,
    const CpuTopology &topology,
    const std::vector<BatchCpu::ThreadPolicy> &candidates)
{
    if (candidates.empty()) {
        return BuildSinglePolicyPlan(BatchCpu::ThreadPolicy{});
    }

    switch (request.mode) {
    case ThreadPolicyMode::Throughput: {
        const auto runtimePolicy = SelectTurboDesktopPolicy(candidates);
        return FinalizePlan(runtimePolicy, ComputeStartupWorkers(runtimePolicy.workerCount));
    }
    case ThreadPolicyMode::Custom:
    case ThreadPolicyMode::Conservative:
        return BuildSinglePolicyPlan(candidates.front());
    case ThreadPolicyMode::Balanced:
    default: {
        const auto runtimePolicy = SelectBalancedRuntimePolicy(topology, candidates);
        return FinalizePlan(runtimePolicy, ComputeStartupWorkers(runtimePolicy.workerCount));
    }
    }
}

BatchCpu::ThreadPolicy SelectDesktopSearchPolicy(
    const ThreadPolicyRequest &request,
    const CpuTopology &topology,
    const std::vector<BatchCpu::ThreadPolicy> &candidates)
{
    return BuildDesktopSearchExecutionPlan(request, topology, candidates).runtimePolicy;
}

} // namespace Batch
