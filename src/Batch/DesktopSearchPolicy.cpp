#include "Batch/DesktopSearchPolicy.hpp"

#include <algorithm>
#include <string_view>

namespace Batch {

namespace {

constexpr uint32_t kPartialSmtMinimumWorkers = 4;
constexpr uint32_t kTurboHeadroomDivisor = 4;

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

BatchCpu::ThreadPolicy SelectBalancedStartupPolicy(
    const CpuTopology &topology,
    const std::vector<BatchCpu::ThreadPolicy> &candidates)
{
    if (candidates.empty()) {
        return BatchCpu::ThreadPolicy{};
    }

    if (topology.isHeterogeneous) {
        const auto *base = FindByName(candidates, "balanced-p-core");
        const auto *partial = FindByName(candidates, "balanced-p-core-plus-smt-partial");
        if (base != nullptr && partial != nullptr &&
            base->workerCount >= kPartialSmtMinimumWorkers) {
            return *partial;
        }
        if (base != nullptr) {
            return *base;
        }
    } else {
        const auto *base = FindByName(candidates, "balanced-physical");
        const auto *partial = FindByName(candidates, "balanced-physical-plus-smt-partial");
        if (base != nullptr && partial != nullptr &&
            base->workerCount >= kPartialSmtMinimumWorkers) {
            return *partial;
        }
        if (base != nullptr) {
            return *base;
        }
    }

    return SelectLargestCandidate(candidates);
}

BatchCpu::ThreadPolicy SelectBalancedRuntimePolicy(
    const CpuTopology &topology,
    const std::vector<BatchCpu::ThreadPolicy> &candidates)
{
    if (topology.isHeterogeneous) {
        if (const auto *full = FindByName(candidates, "balanced-p-core-plus-smt")) {
            return *full;
        }
    } else {
        if (const auto *full = FindByName(candidates, "balanced-physical-plus-smt")) {
            return *full;
        }
    }

    return SelectBalancedStartupPolicy(topology, candidates);
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

uint32_t ComputeTurboStartupWorkers(uint32_t runtimeWorkers)
{
    if (runtimeWorkers <= 4) {
        return std::max<uint32_t>(1u, runtimeWorkers);
    }
    return std::max<uint32_t>(1u,
                              (runtimeWorkers * (kTurboHeadroomDivisor - 1) +
                               (kTurboHeadroomDivisor - 1)) /
                                  kTurboHeadroomDivisor);
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
        return FinalizePlan(runtimePolicy, ComputeTurboStartupWorkers(runtimePolicy.workerCount));
    }
    case ThreadPolicyMode::Custom:
    case ThreadPolicyMode::Conservative:
        return BuildSinglePolicyPlan(candidates.front());
    case ThreadPolicyMode::Balanced:
    default: {
        const auto startupPolicy = SelectBalancedStartupPolicy(topology, candidates);
        const auto runtimePolicy = SelectBalancedRuntimePolicy(topology, candidates);
        return FinalizePlan(runtimePolicy, startupPolicy.workerCount);
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
