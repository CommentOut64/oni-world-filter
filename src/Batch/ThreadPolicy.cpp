#include "Batch/ThreadPolicy.hpp"

#include <algorithm>

#include "Batch/FilterConfig.hpp"

namespace Batch {

namespace {

BatchCpu::CpuMode ToCpuMode(ThreadPolicyMode mode)
{
    switch (mode) {
    case ThreadPolicyMode::Throughput:
        return BatchCpu::CpuMode::Turbo;
    case ThreadPolicyMode::Custom:
        return BatchCpu::CpuMode::Custom;
    case ThreadPolicyMode::Conservative:
        return BatchCpu::CpuMode::Conservative;
    case ThreadPolicyMode::Balanced:
    default:
        return BatchCpu::CpuMode::Balanced;
    }
}

ThreadPolicyMode ParseThreadPolicyMode(const FilterConfig &cfg)
{
    if (!cfg.hasCpuSection) {
        return ThreadPolicyMode::Balanced;
    }
    const auto mode = BatchCpu::ParseCpuMode(cfg.cpu.mode);
    switch (mode) {
    case BatchCpu::CpuMode::Turbo:
        return ThreadPolicyMode::Throughput;
    case BatchCpu::CpuMode::Custom:
        return ThreadPolicyMode::Custom;
    case BatchCpu::CpuMode::Conservative:
        return ThreadPolicyMode::Conservative;
    case BatchCpu::CpuMode::Balanced:
    default:
        return ThreadPolicyMode::Balanced;
    }
}

} // namespace

BatchCpu::PlannerInput BuildPlannerInput(const ThreadPolicyRequest &request,
                                         const CpuTopology &topology)
{
    BatchCpu::PlannerInput input;
    input.topology = &topology;
    input.mode = ToCpuMode(request.mode);
    input.legacyThreadOverride = request.legacyThreads;
    input.customWorkers = request.customWorkers;
    input.customAllowSmt = request.customAllowSmt;
    input.customAllowLowPerf = request.customAllowLowPerf;
    input.customPlacement = request.customPlacement;
    return input;
}

std::vector<BatchCpu::ThreadPolicy> BuildThreadPolicyCandidates(
    const ThreadPolicyRequest &request,
    const CpuTopology &topology)
{
    const auto plannerInput = BuildPlannerInput(request, topology);
    auto candidates = BatchCpu::ThreadPolicyPlanner::BuildCandidates(plannerInput);
    if (candidates.empty()) {
        candidates.push_back(BatchCpu::ThreadPolicyPlanner::BuildConservativePolicy(topology));
    }
    return candidates;
}

ThreadPolicyRequest BuildThreadPolicyRequestFromFilter(const FilterConfig &cfg)
{
    ThreadPolicyRequest request;

    const bool legacyThreadsOnly = !cfg.hasCpuSection && cfg.threads > 0;
    if (legacyThreadsOnly) {
        request.mode = ThreadPolicyMode::Custom;
        request.customWorkers = static_cast<uint32_t>(cfg.threads);
        request.customAllowSmt = true;
        request.customAllowLowPerf = true;
        request.customPlacement = BatchCpu::PlacementMode::Preferred;
        return request;
    }

    request.mode = ParseThreadPolicyMode(cfg);
    request.customWorkers = static_cast<uint32_t>(std::max(0, cfg.cpu.workers));
    request.customAllowSmt = cfg.cpu.allowSmt;
    request.customAllowLowPerf = cfg.cpu.allowLowPerf;
    request.customPlacement = BatchCpu::ParsePlacementMode(cfg.cpu.placement);
    return request;
}

} // namespace Batch

