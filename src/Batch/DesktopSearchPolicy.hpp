#pragma once

#include <vector>

#include "Batch/CpuTopology.hpp"
#include "Batch/ThreadPolicy.hpp"

namespace Batch {

struct DesktopSearchExecutionPlan {
    BatchCpu::ThreadPolicy runtimePolicy;
    uint32_t initialActiveWorkers = 1;
    bool enableRecovery = false;
    BatchCpu::RecoveryConfig recoveryConfig{};
};

DesktopSearchExecutionPlan BuildDesktopSearchExecutionPlan(
    const ThreadPolicyRequest &request,
    const CpuTopology &topology,
    const std::vector<BatchCpu::ThreadPolicy> &candidates);

BatchCpu::ThreadPolicy SelectDesktopSearchPolicy(
    const ThreadPolicyRequest &request,
    const CpuTopology &topology,
    const std::vector<BatchCpu::ThreadPolicy> &candidates);

} // namespace Batch
