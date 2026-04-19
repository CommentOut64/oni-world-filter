#include "Batch/CpuTopology.hpp"
#include "Batch/ThreadPolicy.hpp"

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

    if (failures == 0) {
        std::cout << "[PASS] test_cpu_topology" << std::endl;
        return 0;
    }
    return 1;
}

