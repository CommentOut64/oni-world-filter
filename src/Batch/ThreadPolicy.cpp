#include "Batch/ThreadPolicy.hpp"

#include <sstream>

#include "Batch/FilterConfig.hpp"

namespace Batch {

namespace {

BatchCpu::CpuMode NormalizeSearchCpuMode(const FilterConfig &cfg)
{
    if (!cfg.hasCpuSection) {
        return cfg.threads > 0 ? BatchCpu::CpuMode::Turbo : BatchCpu::CpuMode::Balanced;
    }

    switch (BatchCpu::ParseCpuMode(cfg.cpu.mode)) {
    case BatchCpu::CpuMode::Turbo:
        return BatchCpu::CpuMode::Turbo;
    case BatchCpu::CpuMode::Custom:
        // 旧 custom 模式在统一模型里不再公开，兼容阶段向 turbo 归一化。
        return BatchCpu::CpuMode::Turbo;
    case BatchCpu::CpuMode::Conservative:
        // 旧 conservative 模式在统一模型里不再公开，兼容阶段向 balanced 归一化。
        return BatchCpu::CpuMode::Balanced;
    case BatchCpu::CpuMode::Balanced:
    default:
        return BatchCpu::CpuMode::Balanced;
    }
}

std::vector<uint32_t> BuildCumulativeWorkersByPhysicalCoreCount(
    const BatchCpu::CompiledSearchCpuPlan &plan)
{
    std::vector<uint32_t> cumulative;
    cumulative.reserve(plan.placement.plannedCoresByPriority.size() + 1);
    cumulative.push_back(0);

    uint32_t runningTotal = 0;
    for (const auto &core : plan.placement.plannedCoresByPriority) {
        runningTotal += static_cast<uint32_t>(core.allowedLogicalThreads.size());
        cumulative.push_back(runningTotal);
    }
    return cumulative;
}

} // namespace

CompiledSearchCpuRuntime CompileSearchCpuRuntime(const FilterConfig &cfg,
                                                 const CpuTopologyFacts &topology)
{
    BatchCpu::CpuPolicySpec spec;
    spec.mode = NormalizeSearchCpuMode(cfg);
    spec.allowSmt = cfg.hasCpuSection ? cfg.cpu.allowSmt : true;
    spec.allowLowPerf = cfg.hasCpuSection ? cfg.cpu.allowLowPerf : false;
    spec.binding = cfg.hasCpuSection
        ? BatchCpu::ParsePlacementMode(cfg.cpu.placement)
        : BatchCpu::PlacementMode::Preferred;

    CompiledSearchCpuRuntime runtime;
    runtime.cpuPlan = BatchCpu::CompileSearchCpuPlan(topology, spec);
    return runtime;
}

int ResolveActivePhysicalCoreCapFromWorkerLimit(const BatchCpu::CompiledSearchCpuPlan &plan,
                                                int requestedActiveWorkers)
{
    if (requestedActiveWorkers <= 0) {
        return 0;
    }

    const auto cumulativeWorkers = BuildCumulativeWorkersByPhysicalCoreCount(plan);
    if (cumulativeWorkers.size() <= 1) {
        return 1;
    }

    int bestPhysicalCoreCap = 0;
    for (size_t coreCount = 1; coreCount < cumulativeWorkers.size(); ++coreCount) {
        if (static_cast<int>(cumulativeWorkers[coreCount]) <= requestedActiveWorkers) {
            bestPhysicalCoreCap = static_cast<int>(coreCount);
        }
    }

    return bestPhysicalCoreCap > 0 ? bestPhysicalCoreCap : 1;
}

std::string DescribeCompiledSearchCpuPlan(const BatchCpu::CompiledSearchCpuPlan &plan)
{
    std::ostringstream stream;
    stream << "mode=" << BatchCpu::ToString(plan.policy.mode)
           << " binding=" << BatchCpu::ToString(plan.policy.binding)
           << " eligible_phys=" << plan.envelope.eligiblePhysicalCoreCount
           << " reserved_phys=" << plan.envelope.reservedPhysicalCoreCount
           << " phys_cap=" << plan.envelope.absolutePhysicalCoreCap
           << " startup_phys=" << plan.envelope.startupPhysicalCoreCount
           << " workers=" << plan.envelope.absoluteWorkerCap;

    stream << " cores=[";
    bool firstCore = true;
    for (const auto &core : plan.placement.plannedCoresByPriority) {
        if (!firstCore) {
            stream << ", ";
        }
        firstCore = false;
        stream << "g" << core.group << ":c" << core.coreIndex << "->";
        bool firstThread = true;
        for (const auto &thread : core.allowedLogicalThreads) {
            if (!firstThread) {
                stream << '+';
            }
            firstThread = false;
            stream << thread.logicalIndex;
        }
    }
    stream << "]";
    return stream.str();
}

} // namespace Batch

