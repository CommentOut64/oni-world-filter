#include "BatchCpu/SearchCpuPlan.hpp"

#include <algorithm>
#include <cmath>

namespace BatchCpu {
namespace {

std::vector<const PhysicalCoreFacts *> CollectEligibleCores(const CpuTopologyFacts &topology,
                                                            const CpuPolicySpec &spec)
{
    std::vector<const PhysicalCoreFacts *> eligible;
    eligible.reserve(topology.physicalCoresBySystemOrder.size());
    for (const auto &core : topology.physicalCoresBySystemOrder) {
        if (!topology.isHeterogeneous || spec.allowLowPerf || core.isHighPerformance) {
            eligible.push_back(&core);
        }
    }
    if (eligible.empty()) {
        for (const auto &core : topology.physicalCoresBySystemOrder) {
            eligible.push_back(&core);
        }
    }
    return eligible;
}

uint32_t ComputeReservedPhysicalCores(CpuMode mode, uint32_t eligiblePhysicalCoreCount)
{
    if (eligiblePhysicalCoreCount <= 1) {
        return 0;
    }
    switch (mode) {
    case CpuMode::Balanced:
        return 1;
    case CpuMode::Turbo:
    default:
        return 0;
    }
}

double ResolveModeRatio(CpuMode mode)
{
    switch (mode) {
    case CpuMode::Balanced:
        return 0.8;
    case CpuMode::Turbo:
    default:
        return 1.0;
    }
}

uint32_t ComputeAbsolutePhysicalCoreCap(uint32_t eligiblePhysicalCoreCount,
                                        uint32_t reservedPhysicalCoreCount,
                                        CpuMode mode)
{
    if (eligiblePhysicalCoreCount == 0) {
        return 0;
    }

    const double rawCap = std::ceil((double)eligiblePhysicalCoreCount * ResolveModeRatio(mode));
    uint32_t cap = std::clamp<uint32_t>((uint32_t)rawCap, 1, eligiblePhysicalCoreCount);
    const uint32_t reserveClamp = eligiblePhysicalCoreCount > reservedPhysicalCoreCount
        ? (eligiblePhysicalCoreCount - reservedPhysicalCoreCount)
        : 1U;
    cap = std::min(cap, reserveClamp);
    return std::max<uint32_t>(1, cap);
}

std::vector<LogicalThreadFacts> BuildAllowedLogicalThreads(const PhysicalCoreFacts &core,
                                                           bool allowSmt)
{
    std::vector<LogicalThreadFacts> ordered;
    ordered.reserve(core.logicalThreads.size());

    const auto primaryIt = std::find_if(core.logicalThreads.begin(),
                                        core.logicalThreads.end(),
                                        [](const LogicalThreadFacts &thread) {
                                            return thread.isPrimaryThread;
                                        });
    if (primaryIt != core.logicalThreads.end()) {
        ordered.push_back(*primaryIt);
    } else if (!core.logicalThreads.empty()) {
        ordered.push_back(core.logicalThreads.front());
    }

    if (!allowSmt) {
        return ordered;
    }

    for (const auto &thread : core.logicalThreads) {
        if (!ordered.empty() && thread.logicalIndex == ordered.front().logicalIndex) {
            continue;
        }
        ordered.push_back(thread);
    }
    return ordered;
}

} // namespace

CompiledSearchCpuPlan CompileSearchCpuPlan(const CpuTopologyFacts &topology,
                                           const CpuPolicySpec &spec)
{
    CompiledSearchCpuPlan plan;
    plan.isHeterogeneous = topology.isHeterogeneous;
    plan.policy = spec;

    const auto eligible = CollectEligibleCores(topology, spec);
    plan.envelope.eligiblePhysicalCoreCount = static_cast<uint32_t>(eligible.size());
    plan.envelope.reservedPhysicalCoreCount = ComputeReservedPhysicalCores(
        spec.mode,
        plan.envelope.eligiblePhysicalCoreCount);
    plan.envelope.absolutePhysicalCoreCap = ComputeAbsolutePhysicalCoreCap(
        plan.envelope.eligiblePhysicalCoreCount,
        plan.envelope.reservedPhysicalCoreCount,
        spec.mode);
    plan.envelope.startupPhysicalCoreCount = plan.envelope.absolutePhysicalCoreCap;

    const size_t selectedCoreCount = std::min<size_t>(eligible.size(),
                                                      plan.envelope.absolutePhysicalCoreCap);
    plan.placement.plannedCoresByPriority.reserve(selectedCoreCount);
    for (size_t i = 0; i < selectedCoreCount; ++i) {
        const auto &core = *eligible[i];
        PlannedCore plannedCore;
        plannedCore.physicalCoreIndex = core.physicalCoreIndex;
        plannedCore.group = core.group;
        plannedCore.coreIndex = core.coreIndex;
        plannedCore.numaNodeIndex = core.numaNodeIndex;
        plannedCore.isHighPerformance = core.isHighPerformance;
        plannedCore.allowedLogicalThreads = BuildAllowedLogicalThreads(core, spec.allowSmt);
        plan.placement.plannedCoresByPriority.push_back(std::move(plannedCore));
    }

    for (const auto &core : plan.placement.plannedCoresByPriority) {
        for (const auto &thread : core.allowedLogicalThreads) {
            WorkerBindingSlot slot;
            slot.workerIndex = static_cast<uint32_t>(plan.placement.workerSlotsByPriority.size());
            slot.physicalCoreIndex = core.physicalCoreIndex;
            slot.logicalIndex = thread.logicalIndex;
            slot.group = thread.group;
            slot.coreIndex = thread.coreIndex;
            slot.numaNodeIndex = thread.numaNodeIndex;
            slot.cpuSetId = thread.cpuSetId;
            slot.isPrimaryThread = thread.isPrimaryThread;
            plan.placement.workerSlotsByPriority.push_back(slot);
        }
    }

    plan.envelope.absoluteWorkerCap =
        static_cast<uint32_t>(plan.placement.workerSlotsByPriority.size());
    return plan;
}

} // namespace BatchCpu
