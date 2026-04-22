#include "BatchCpu/SearchCpuPlan.hpp"
#include "BatchCpu/CpuOptimization.hpp"

#include <iostream>
#include <vector>

namespace {

bool Expect(bool condition, const char *message, int &failures)
{
    if (condition) {
        return true;
    }
    std::cerr << "[FAIL] " << message << std::endl;
    ++failures;
    return false;
}

BatchCpu::CpuTopologyFacts BuildSyntheticHeterogeneousTopology()
{
    BatchCpu::CpuTopologyFacts topology;
    topology.detectionSucceeded = true;
    topology.isHeterogeneous = true;
    topology.diagnostics = "synthetic heterogeneous topology";

    topology.physicalCoresBySystemOrder = {
        {
            .physicalCoreIndex = 0,
            .group = 1,
            .efficiencyClass = 8,
            .isHighPerformance = true,
            .logicalThreads = {
                {.logicalIndex = 8, .group = 1, .cpuSetId = 108, .isPrimaryThread = true},
                {.logicalIndex = 9, .group = 1, .cpuSetId = 109, .isPrimaryThread = false},
            },
        },
        {
            .physicalCoreIndex = 1,
            .group = 0,
            .efficiencyClass = 8,
            .isHighPerformance = true,
            .logicalThreads = {
                {.logicalIndex = 0, .group = 0, .cpuSetId = 100, .isPrimaryThread = true},
                {.logicalIndex = 1, .group = 0, .cpuSetId = 101, .isPrimaryThread = false},
            },
        },
        {
            .physicalCoreIndex = 2,
            .group = 1,
            .efficiencyClass = 8,
            .isHighPerformance = true,
            .logicalThreads = {
                {.logicalIndex = 10, .group = 1, .cpuSetId = 110, .isPrimaryThread = true},
                {.logicalIndex = 11, .group = 1, .cpuSetId = 111, .isPrimaryThread = false},
            },
        },
        {
            .physicalCoreIndex = 3,
            .group = 0,
            .efficiencyClass = 0,
            .isHighPerformance = false,
            .logicalThreads = {
                {.logicalIndex = 2, .group = 0, .cpuSetId = 200, .isPrimaryThread = true},
                {.logicalIndex = 3, .group = 0, .cpuSetId = 201, .isPrimaryThread = false},
            },
        },
    };

    return topology;
}

BatchCpu::CpuTopologyFacts BuildSyntheticNonSmtTopology()
{
    BatchCpu::CpuTopologyFacts topology;
    topology.detectionSucceeded = true;
    topology.isHeterogeneous = false;
    topology.diagnostics = "synthetic non-smt topology";
    topology.physicalCoresBySystemOrder = {
        {
            .physicalCoreIndex = 0,
            .group = 0,
            .efficiencyClass = 0,
            .isHighPerformance = true,
            .logicalThreads = {
                {.logicalIndex = 4, .group = 0, .cpuSetId = 304, .isPrimaryThread = true},
            },
        },
        {
            .physicalCoreIndex = 1,
            .group = 0,
            .efficiencyClass = 0,
            .isHighPerformance = true,
            .logicalThreads = {
                {.logicalIndex = 0, .group = 0, .cpuSetId = 300, .isPrimaryThread = true},
            },
        },
        {
            .physicalCoreIndex = 2,
            .group = 1,
            .efficiencyClass = 0,
            .isHighPerformance = true,
            .logicalThreads = {
                {.logicalIndex = 8, .group = 1, .cpuSetId = 308, .isPrimaryThread = true},
            },
        },
    };
    return topology;
}

void ExpectLogicalOrder(const std::vector<BatchCpu::WorkerBindingSlot> &slots,
                        const std::vector<uint32_t> &expectedLogicalIndices,
                        int &failures)
{
    Expect(slots.size() == expectedLogicalIndices.size(),
           "worker slot count should match expected logical order size",
           failures);
    const size_t count = std::min(slots.size(), expectedLogicalIndices.size());
    for (size_t i = 0; i < count; ++i) {
        Expect(slots[i].logicalIndex == expectedLogicalIndices[i],
               "worker slot logical order should preserve physical-core-first order",
               failures);
    }
}

void ExpectCpuSetOrder(const std::vector<uint32_t> &cpuSetIds,
                       const std::vector<uint32_t> &expectedCpuSetIds,
                       int &failures)
{
    Expect(cpuSetIds.size() == expectedCpuSetIds.size(),
           "cpu set count should match expected size",
           failures);
    const size_t count = std::min(cpuSetIds.size(), expectedCpuSetIds.size());
    for (size_t i = 0; i < count; ++i) {
        Expect(cpuSetIds[i] == expectedCpuSetIds[i],
               "cpu set order should preserve planned logical thread order",
               failures);
    }
}

void ExpectBindingTarget(const BatchCpu::CpuPlacementPlan &plan,
                         uint32_t workerIndex,
                         uint16_t expectedGroup,
                         uint32_t expectedLogicalIndex,
                         int &failures)
{
    const auto target = BatchCpu::ResolveThreadBindingTarget(plan, workerIndex);
    Expect(target.has_value(),
           "worker binding target should exist for selected worker index",
           failures);
    if (!target.has_value()) {
        return;
    }
    Expect(target->group == expectedGroup,
           "binding target should preserve worker slot group",
           failures);
    Expect(target->logicalIndex == expectedLogicalIndex,
           "binding target should preserve worker slot logical index",
           failures);
}

} // namespace

int RunAllTests()
{
    int failures = 0;

    {
        Expect(BatchCpu::IsHighPerformanceEfficiencyClass(true, 8, 8),
               "heterogeneous topology should treat highest efficiency class as high performance",
               failures);
        Expect(!BatchCpu::IsHighPerformanceEfficiencyClass(true, 0, 8),
               "heterogeneous topology should not treat lower efficiency class as high performance",
               failures);
        Expect(BatchCpu::IsHighPerformanceEfficiencyClass(false, 0, 8),
               "homogeneous topology should treat all cores as high performance",
               failures);
    }

    {
        const auto topology = BuildSyntheticHeterogeneousTopology();
        BatchCpu::CpuPolicySpec spec;
        spec.mode = BatchCpu::CpuMode::Balanced;
        spec.allowSmt = true;
        spec.allowLowPerf = false;
        spec.binding = BatchCpu::PlacementMode::Strict;

        const auto plan = BatchCpu::CompileSearchCpuPlan(topology, spec);
        Expect(plan.envelope.eligiblePhysicalCoreCount == 3,
               "balanced should only consider high-performance physical cores",
               failures);
        Expect(plan.envelope.reservedPhysicalCoreCount == 1,
               "balanced should reserve one eligible physical core",
               failures);
        Expect(plan.envelope.absolutePhysicalCoreCap == 2,
               "balanced cap should clamp ceil(eligible * 0.8) with reserve",
               failures);
        Expect(plan.envelope.startupPhysicalCoreCount == 2,
               "startup physical cores should equal absolute cap",
               failures);
        Expect(plan.envelope.absoluteWorkerCap == 4,
               "allowSmt=true should expose both logical threads from selected cores",
               failures);
        Expect(plan.placement.plannedCoresByPriority.size() == 2,
               "placement should only include capped physical cores",
               failures);
        Expect(plan.placement.plannedCoresByPriority[0].physicalCoreIndex == 0,
               "placement should preserve first physical core order",
               failures);
        Expect(plan.placement.plannedCoresByPriority[1].physicalCoreIndex == 1,
               "placement should preserve second physical core order",
               failures);
        ExpectLogicalOrder(plan.placement.workerSlotsByPriority, {8, 9, 0, 1}, failures);
        Expect(plan.placement.workerSlotsByPriority[0].cpuSetId.has_value() &&
                   plan.placement.workerSlotsByPriority[0].cpuSetId.value() == 108,
               "worker slot should preserve first cpu set id",
               failures);
        const auto allowedCpuSetIds = BatchCpu::ResolveAllowedCpuSetIds(plan);
        ExpectCpuSetOrder(allowedCpuSetIds, {108, 109, 100, 101}, failures);
        const auto workerCpuSetId = BatchCpu::ResolveWorkerCpuSetId(plan.placement, 2);
        Expect(workerCpuSetId.has_value() && workerCpuSetId.value() == 100,
               "worker cpu set lookup should preserve slot cpu set id",
               failures);
        const auto directive = BatchCpu::ResolveThreadPlacementDirective(plan, 2);
        Expect(directive.has_value(),
               "heterogeneous strict placement should resolve directive for selected worker",
               failures);
        if (directive.has_value()) {
            ExpectCpuSetOrder(directive->selectedCpuSetIds, {100}, failures);
            Expect(directive->bindingTarget.has_value() &&
                       directive->bindingTarget->group == 0 &&
                       directive->bindingTarget->logicalIndex == 0,
                   "heterogeneous strict placement should keep exact binding target together with cpu set",
                   failures);
        }
        ExpectBindingTarget(plan.placement, 0, 1, 8, failures);
        ExpectBindingTarget(plan.placement, 2, 0, 0, failures);
    }

    {
        const auto topology = BuildSyntheticHeterogeneousTopology();
        BatchCpu::CpuPolicySpec spec;
        spec.mode = BatchCpu::CpuMode::Turbo;
        spec.allowSmt = true;
        spec.allowLowPerf = true;
        spec.binding = BatchCpu::PlacementMode::Strict;

        const auto plan = BatchCpu::CompileSearchCpuPlan(topology, spec);
        Expect(plan.envelope.eligiblePhysicalCoreCount == 4,
               "turbo + allowLowPerf should include all physical cores",
               failures);
        Expect(plan.envelope.absolutePhysicalCoreCap == 4,
               "turbo cap should equal eligible physical core count",
               failures);
        Expect(plan.envelope.absoluteWorkerCap == 8,
               "turbo should flatten all allowed logical threads",
               failures);
        ExpectLogicalOrder(plan.placement.workerSlotsByPriority, {8, 9, 0, 1, 10, 11, 2, 3}, failures);
        ExpectBindingTarget(plan.placement, 4, 1, 10, failures);
        ExpectBindingTarget(plan.placement, 7, 0, 3, failures);
    }

    {
        const auto topology = BuildSyntheticNonSmtTopology();
        BatchCpu::CpuPolicySpec spec;
        spec.mode = BatchCpu::CpuMode::Turbo;
        spec.allowSmt = true;
        spec.allowLowPerf = true;
        spec.binding = BatchCpu::PlacementMode::Preferred;

        const auto plan = BatchCpu::CompileSearchCpuPlan(topology, spec);
        Expect(plan.envelope.absolutePhysicalCoreCap == 3,
               "non-SMT topology should still include all physical cores in turbo",
               failures);
        Expect(plan.envelope.absoluteWorkerCap == 3,
               "non-SMT topology should contribute one worker per physical core",
               failures);
        ExpectLogicalOrder(plan.placement.workerSlotsByPriority, {4, 0, 8}, failures);
        Expect(BatchCpu::ResolveAllowedCpuSetIds(plan).empty(),
               "homogeneous topology should not emit low-perf cpu set restrictions",
               failures);
        const auto directive = BatchCpu::ResolveThreadPlacementDirective(plan, 1);
        Expect(directive.has_value(),
               "homogeneous placement should still resolve directive",
               failures);
        if (directive.has_value()) {
            Expect(directive->selectedCpuSetIds.empty(),
                   "homogeneous placement should not add cpu set restrictions",
                   failures);
            Expect(directive->bindingTarget.has_value() &&
                       directive->bindingTarget->group == 0 &&
                       directive->bindingTarget->logicalIndex == 0,
                   "homogeneous placement should keep binding target only",
                   failures);
        }
    }

    {
        const auto topology = BuildSyntheticHeterogeneousTopology();
        BatchCpu::CpuPolicySpec spec;
        spec.mode = BatchCpu::CpuMode::Balanced;
        spec.allowSmt = false;
        spec.allowLowPerf = false;
        spec.binding = BatchCpu::PlacementMode::Strict;

        const auto plan = BatchCpu::CompileSearchCpuPlan(topology, spec);
        Expect(plan.envelope.absoluteWorkerCap == 2,
               "allowSmt=false should expose only one worker slot per physical core",
               failures);
        ExpectLogicalOrder(plan.placement.workerSlotsByPriority, {8, 0}, failures);
        for (const auto &slot : plan.placement.workerSlotsByPriority) {
            Expect(slot.logicalIndex != 9 && slot.logicalIndex != 1,
                   "allowSmt=false should not generate sibling logical thread slots",
                   failures);
        }
        ExpectBindingTarget(plan.placement, 0, 1, 8, failures);
        ExpectBindingTarget(plan.placement, 1, 0, 0, failures);
        const auto outOfRangeTarget = BatchCpu::ResolveThreadBindingTarget(plan.placement, 2);
        Expect(!outOfRangeTarget.has_value(),
               "binding target lookup should reject out-of-range worker indices",
               failures);
    }

    if (failures == 0) {
        std::cout << "[PASS] test_search_cpu_plan" << std::endl;
        return 0;
    }
    return 1;
}
