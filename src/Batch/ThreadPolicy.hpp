#pragma once

#include <string>

#include "Batch/CpuTopology.hpp"
#include "BatchCpu/SearchCpuGovernor.hpp"

namespace Batch {

struct FilterConfig;

struct CompiledSearchCpuRuntime {
    BatchCpu::CompiledSearchCpuPlan cpuPlan{};
    BatchCpu::SearchCpuGovernorConfig cpuGovernorConfig{};
};

CompiledSearchCpuRuntime CompileSearchCpuRuntime(const FilterConfig &cfg,
                                                const CpuTopologyFacts &topology);

int ResolveActivePhysicalCoreCapFromWorkerLimit(const BatchCpu::CompiledSearchCpuPlan &plan,
                                                int requestedActiveWorkers);

std::string DescribeCompiledSearchCpuPlan(const BatchCpu::CompiledSearchCpuPlan &plan);

} // namespace Batch
