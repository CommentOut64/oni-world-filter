#include "BatchCpu/SearchCpuGovernor.hpp"
#include "BatchCpu/SearchCpuPlan.hpp"

#include <chrono>
#include <iostream>

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

BatchCpu::CpuTopologyFacts BuildSmt2Topology()
{
    BatchCpu::CpuTopologyFacts topology;
    topology.detectionSucceeded = true;
    topology.isHeterogeneous = false;
    topology.diagnostics = "synthetic smt2 topology";
    topology.physicalCoresBySystemOrder = {
        {
            .physicalCoreIndex = 0,
            .group = 0,
            .efficiencyClass = 0,
            .isHighPerformance = true,
            .logicalThreads = {
                {.logicalIndex = 0, .group = 0, .isPrimaryThread = true},
                {.logicalIndex = 1, .group = 0, .isPrimaryThread = false},
            },
        },
        {
            .physicalCoreIndex = 1,
            .group = 0,
            .efficiencyClass = 0,
            .isHighPerformance = true,
            .logicalThreads = {
                {.logicalIndex = 2, .group = 0, .isPrimaryThread = true},
                {.logicalIndex = 3, .group = 0, .isPrimaryThread = false},
            },
        },
        {
            .physicalCoreIndex = 2,
            .group = 0,
            .efficiencyClass = 0,
            .isHighPerformance = true,
            .logicalThreads = {
                {.logicalIndex = 4, .group = 0, .isPrimaryThread = true},
                {.logicalIndex = 5, .group = 0, .isPrimaryThread = false},
            },
        },
        {
            .physicalCoreIndex = 3,
            .group = 0,
            .efficiencyClass = 0,
            .isHighPerformance = true,
            .logicalThreads = {
                {.logicalIndex = 6, .group = 0, .isPrimaryThread = true},
                {.logicalIndex = 7, .group = 0, .isPrimaryThread = false},
            },
        },
    };
    return topology;
}

BatchCpu::CpuTopologyFacts BuildNonSmtTopology()
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
                {.logicalIndex = 0, .group = 0, .isPrimaryThread = true},
            },
        },
        {
            .physicalCoreIndex = 1,
            .group = 0,
            .efficiencyClass = 0,
            .isHighPerformance = true,
            .logicalThreads = {
                {.logicalIndex = 1, .group = 0, .isPrimaryThread = true},
            },
        },
        {
            .physicalCoreIndex = 2,
            .group = 0,
            .efficiencyClass = 0,
            .isHighPerformance = true,
            .logicalThreads = {
                {.logicalIndex = 2, .group = 0, .isPrimaryThread = true},
            },
        },
        {
            .physicalCoreIndex = 3,
            .group = 0,
            .efficiencyClass = 0,
            .isHighPerformance = true,
            .logicalThreads = {
                {.logicalIndex = 3, .group = 0, .isPrimaryThread = true},
            },
        },
    };
    return topology;
}

BatchCpu::CompiledSearchCpuPlan BuildBalancedPlan(const BatchCpu::CpuTopologyFacts &topology,
                                                  bool allowSmt)
{
    BatchCpu::CpuPolicySpec spec;
    spec.mode = BatchCpu::CpuMode::Balanced;
    spec.allowSmt = allowSmt;
    spec.allowLowPerf = true;
    spec.binding = BatchCpu::PlacementMode::Preferred;
    return BatchCpu::CompileSearchCpuPlan(topology, spec);
}

} // namespace

int RunAllTests()
{
    int failures = 0;

    {
        const auto plan = BuildBalancedPlan(BuildSmt2Topology(), true);
        BatchCpu::SearchCpuGovernorConfig config;
        config.minActivePhysicalCores = 1;
        config.scaleDownThreshold = 0.20;
        config.scaleDownWindowCount = 1;
        config.scaleUpWindowCount = 2;
        config.scaleUpRetentionRatio = 0.95;
        config.cooldown = std::chrono::milliseconds(10);

        BatchCpu::SearchCpuGovernor governor(plan, config);
        const auto t0 = std::chrono::steady_clock::now();

        Expect(governor.StartupActivePhysicalCores() == plan.envelope.startupPhysicalCoreCount,
               "governor startup physical core count should match compiled plan",
               failures);
        Expect(governor.ActiveWorkerCountFor(3) == 6,
               "SMT=2 topology should contribute two workers per physical core",
               failures);
        Expect(governor.ActiveWorkerCountFor(2) == 4,
               "SMT=2 topology should reduce workers by two when dropping one physical core",
               failures);
        Expect(!governor.Observe(100.0, 3, t0).has_value(),
               "initial governor sample should establish baseline without changing cores",
               failures);
        const auto firstDown = governor.Observe(70.0, 3, t0 + std::chrono::milliseconds(20));
        Expect(firstDown.has_value() && firstDown.value() == 2,
               "governor should reduce active physical cores by one on throughput drop",
               failures);
        Expect(!governor.Observe(70.0, 2, t0 + std::chrono::milliseconds(22)).has_value(),
               "governor should ignore the first mixed window after a reduction",
               failures);
        Expect(!governor.Observe(69.0, 2, t0 + std::chrono::milliseconds(24)).has_value(),
               "governor should rebuild the reduced-stage baseline before recovery",
               failures);
        Expect(!governor.Observe(68.0, 2, t0 + std::chrono::milliseconds(31)).has_value(),
               "governor should still require stable windows after rebuilding the baseline",
               failures);
        const auto firstUp = governor.Observe(68.0, 2, t0 + std::chrono::milliseconds(42));
        Expect(firstUp.has_value() && firstUp.value() == 3,
               "governor should recover one physical core at a time after stable windows",
               failures);
    }

    {
        const auto plan = BuildBalancedPlan(BuildNonSmtTopology(), true);
        BatchCpu::SearchCpuGovernorConfig config;
        config.minActivePhysicalCores = 1;
        config.scaleDownThreshold = 0.20;
        config.scaleDownWindowCount = 1;
        config.scaleUpWindowCount = 2;
        config.scaleUpRetentionRatio = 0.95;
        config.cooldown = std::chrono::milliseconds(10);

        BatchCpu::SearchCpuGovernor governor(plan, config);
        Expect(governor.ActiveWorkerCountFor(3) == 3,
               "non-SMT topology should contribute one worker per physical core",
               failures);
        Expect(governor.ActiveWorkerCountFor(2) == 2,
               "non-SMT topology should change workers by one when dropping one physical core",
               failures);
    }

    {
        const auto plan = BuildBalancedPlan(BuildSmt2Topology(), true);
        BatchCpu::SearchCpuGovernorConfig config;
        config.minActivePhysicalCores = 1;
        config.scaleDownThreshold = 0.20;
        config.scaleDownWindowCount = 1;
        config.scaleUpWindowCount = 2;
        config.scaleUpRetentionRatio = 0.95;
        config.cooldown = std::chrono::milliseconds(10);

        BatchCpu::SearchCpuGovernor governor(plan, config);
        const auto t0 = std::chrono::steady_clock::now();

        Expect(!governor.Observe(100.0, 3, t0).has_value(),
               "mixed-window recovery case should establish startup baseline first",
               failures);
        const auto firstDown = governor.Observe(70.0, 3, t0 + std::chrono::milliseconds(20));
        Expect(firstDown.has_value() && firstDown.value() == 2,
               "mixed-window recovery case should still reduce by one physical core",
               failures);
        Expect(!governor.Observe(60.0, 2, t0 + std::chrono::milliseconds(40)).has_value(),
               "first post-adjustment window should be ignored as mixed in-flight work",
               failures);
        Expect(!governor.Observe(35.0, 2, t0 + std::chrono::milliseconds(60)).has_value(),
               "steady post-adjustment window should establish reduced-stage baseline",
               failures);
        Expect(!governor.Observe(35.0, 2, t0 + std::chrono::milliseconds(80)).has_value(),
               "recovery should still require enough stable windows after baseline resets",
               failures);
        const auto recovered = governor.Observe(35.0, 2, t0 + std::chrono::milliseconds(100));
        Expect(recovered.has_value() && recovered.value() == 3,
               "governor should recover once steady reduced-stage throughput is confirmed",
               failures);
    }

    if (failures == 0) {
        std::cout << "[PASS] test_search_cpu_governor" << std::endl;
        return 0;
    }
    return 1;
}
