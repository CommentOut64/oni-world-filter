#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace BatchCpu {

enum class CpuMode {
    Balanced,
    Turbo
};

enum class PlacementMode {
    None,
    Preferred,
    Strict
};

struct LogicalThreadFacts {
    uint32_t logicalIndex = 0;
    uint16_t group = 0;
    uint16_t coreIndex = 0;
    uint16_t numaNodeIndex = 0;
    std::optional<uint32_t> cpuSetId;
    uint8_t efficiencyClass = 0;
    bool parked = false;
    bool allocated = false;
    bool isPrimaryThread = false;
};

struct PhysicalCoreFacts {
    uint32_t physicalCoreIndex = 0;
    uint16_t group = 0;
    uint16_t coreIndex = 0;
    uint16_t numaNodeIndex = 0;
    uint8_t efficiencyClass = 0;
    bool isHighPerformance = true;
    std::vector<LogicalThreadFacts> logicalThreads;
};

struct CpuTopologyFacts {
    bool detectionSucceeded = false;
    bool usedFallback = false;
    bool isHeterogeneous = false;
    std::vector<PhysicalCoreFacts> physicalCoresBySystemOrder;
    std::vector<uint16_t> processorGroups;
    std::vector<uint16_t> numaNodes;
    std::string diagnostics;
};

struct CpuPolicySpec {
    CpuMode mode = CpuMode::Balanced;
    bool allowSmt = true;
    bool allowLowPerf = true;
    PlacementMode binding = PlacementMode::Preferred;
};

struct CpuExecutionEnvelope {
    uint32_t eligiblePhysicalCoreCount = 0;
    uint32_t reservedPhysicalCoreCount = 0;
    uint32_t absolutePhysicalCoreCap = 0;
    uint32_t startupPhysicalCoreCount = 0;
    uint32_t absoluteWorkerCap = 0;
};

struct PlannedCore {
    uint32_t physicalCoreIndex = 0;
    uint16_t group = 0;
    uint16_t coreIndex = 0;
    uint16_t numaNodeIndex = 0;
    bool isHighPerformance = true;
    std::vector<LogicalThreadFacts> allowedLogicalThreads;
};

struct WorkerBindingSlot {
    uint32_t workerIndex = 0;
    uint32_t physicalCoreIndex = 0;
    uint32_t logicalIndex = 0;
    uint16_t group = 0;
    uint16_t coreIndex = 0;
    uint16_t numaNodeIndex = 0;
    std::optional<uint32_t> cpuSetId;
    bool isPrimaryThread = false;
};

struct CpuPlacementPlan {
    std::vector<PlannedCore> plannedCoresByPriority;
    std::vector<WorkerBindingSlot> workerSlotsByPriority;
};

struct CompiledSearchCpuPlan {
    bool isHeterogeneous = false;
    CpuPolicySpec policy{};
    CpuExecutionEnvelope envelope{};
    CpuPlacementPlan placement{};
};

CompiledSearchCpuPlan CompileSearchCpuPlan(const CpuTopologyFacts &topology,
                                           const CpuPolicySpec &spec);

inline std::optional<WorkerBindingSlot> ResolveWorkerBindingSlot(const CpuPlacementPlan &plan,
                                                                 uint32_t workerIndex)
{
    if (workerIndex >= plan.workerSlotsByPriority.size()) {
        return std::nullopt;
    }
    return plan.workerSlotsByPriority[workerIndex];
}

} // namespace BatchCpu
