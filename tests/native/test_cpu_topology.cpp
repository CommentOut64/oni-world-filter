#include "Batch/CpuTopology.hpp"
#include "Batch/ThreadPolicy.hpp"

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

Batch::CpuTopology BuildSyntheticHeterogeneousTopology()
{
    Batch::CpuTopology topology;
    topology.detectionSucceeded = true;
    topology.isHeterogeneous = true;
    topology.physicalCoreCount = 3;
    topology.logicalThreadCount = 6;
    topology.highPerfLogicalIndices = {0, 1, 2, 3};
    topology.lowPerfLogicalIndices = {4, 5};
    topology.physicalPreferredLogicalIndices = {0, 2, 4};
    topology.diagnostics = "synthetic heterogeneous topology";
    return topology;
}

bool ContainsPolicy(const std::vector<BatchCpu::ThreadPolicy> &candidates, const char *name)
{
    for (const auto &candidate : candidates) {
        if (candidate.name == name) {
            return true;
        }
    }
    return false;
}

bool UsesAnyLogical(const BatchCpu::ThreadPolicy &policy, const std::vector<uint32_t> &logicalIndices)
{
    for (uint32_t logicalIndex : policy.targetLogicalProcessors) {
        for (uint32_t forbidden : logicalIndices) {
            if (logicalIndex == forbidden) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

int RunAllTests()
{
    int failures = 0;

    const auto topology = Batch::DetectCpuTopology();
    Expect(topology.logicalThreadCount >= 1, "logicalThreadCount should be >= 1", failures);
    Expect(topology.physicalCoreCount >= 1, "physicalCoreCount should be >= 1", failures);
    Expect(!topology.diagnostics.empty(), "topology diagnostics should not be empty", failures);

    Batch::ThreadPolicyRequest balancedRequest;
    balancedRequest.mode = Batch::ThreadPolicyMode::Balanced;
    const auto balancedCandidates = Batch::BuildThreadPolicyCandidates(balancedRequest, topology);
    Expect(!balancedCandidates.empty(), "balanced candidates should not be empty", failures);

    Batch::ThreadPolicyRequest throughputRequest;
    throughputRequest.mode = Batch::ThreadPolicyMode::Throughput;
    const auto throughputCandidates = Batch::BuildThreadPolicyCandidates(throughputRequest, topology);
    Expect(!throughputCandidates.empty(), "throughput candidates should not be empty", failures);

    Batch::ThreadPolicyRequest customRequest;
    customRequest.mode = Batch::ThreadPolicyMode::Custom;
    customRequest.customWorkers = 2;
    customRequest.customAllowLowPerf = false;
    customRequest.customAllowSmt = false;
    const auto customCandidates = Batch::BuildThreadPolicyCandidates(customRequest, topology);
    Expect(customCandidates.size() == 1, "custom mode should return exactly one candidate", failures);
    if (!customCandidates.empty()) {
        Expect(customCandidates[0].workerCount >= 1, "custom worker count should be >= 1", failures);
    }

    {
        const auto syntheticTopology = BuildSyntheticHeterogeneousTopology();

        Batch::ThreadPolicyRequest balancedNoLowPerfRequest;
        balancedNoLowPerfRequest.mode = Batch::ThreadPolicyMode::Balanced;
        balancedNoLowPerfRequest.customAllowLowPerf = false;
        const auto noLowPerfCandidates =
            Batch::BuildThreadPolicyCandidates(balancedNoLowPerfRequest, syntheticTopology);
        Expect(!ContainsPolicy(noLowPerfCandidates, "balanced-p-core-plus-low-core"),
               "balanced should not generate low-core candidate when allowLowPerf is false",
               failures);
        for (const auto &candidate : noLowPerfCandidates) {
            Expect(!UsesAnyLogical(candidate, syntheticTopology.lowPerfLogicalIndices),
                   "balanced should keep all low-perf logical processors out when allowLowPerf is false",
                   failures);
        }

        Batch::ThreadPolicyRequest balancedAllowLowPerfRequest;
        balancedAllowLowPerfRequest.mode = Batch::ThreadPolicyMode::Balanced;
        balancedAllowLowPerfRequest.customAllowLowPerf = true;
        const auto allowLowPerfCandidates =
            Batch::BuildThreadPolicyCandidates(balancedAllowLowPerfRequest, syntheticTopology);
        Expect(ContainsPolicy(allowLowPerfCandidates, "balanced-p-core-plus-low-core"),
               "balanced should generate low-core candidate when allowLowPerf is true",
               failures);
    }

    if (failures == 0) {
        std::cout << "[PASS] test_cpu_topology" << std::endl;
        return 0;
    }
    return 1;
}

