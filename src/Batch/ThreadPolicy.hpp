#pragma once

#include <vector>

#include "Batch/CpuTopology.hpp"

namespace Batch {

struct FilterConfig;

enum class ThreadPolicyMode {
    Balanced,
    Throughput,
    Custom,
    Conservative,
};

struct ThreadPolicyRequest {
    ThreadPolicyMode mode = ThreadPolicyMode::Balanced;
    uint32_t legacyThreads = 0;
    uint32_t customWorkers = 0;
    bool customAllowSmt = true;
    bool customAllowLowPerf = true;
    BatchCpu::PlacementMode customPlacement = BatchCpu::PlacementMode::Preferred;
};

BatchCpu::PlannerInput BuildPlannerInput(const ThreadPolicyRequest &request,
                                         const CpuTopology &topology);

std::vector<BatchCpu::ThreadPolicy> BuildThreadPolicyCandidates(
    const ThreadPolicyRequest &request,
    const CpuTopology &topology);

ThreadPolicyRequest BuildThreadPolicyRequestFromFilter(
    const FilterConfig &cfg);

} // namespace Batch
