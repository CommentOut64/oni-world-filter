#include "Batch/CpuTopology.hpp"

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

    const auto topology = Batch::DetectCpuTopologyFacts();
    Expect(!topology.diagnostics.empty(), "topology diagnostics should not be empty", failures);
    Expect(topology.physicalCoresBySystemOrder.size() >= 1,
           "physicalCoresBySystemOrder should contain at least one physical core",
           failures);

    for (const auto &core : topology.physicalCoresBySystemOrder) {
        Expect(!core.logicalThreads.empty(),
               "each physical core should expose at least one logical thread",
               failures);
        bool hasPrimaryThread = false;
        for (const auto &thread : core.logicalThreads) {
            if (thread.isPrimaryThread) {
                hasPrimaryThread = true;
                break;
            }
        }
        Expect(hasPrimaryThread,
               "each physical core should contain one primary logical thread",
               failures);
    }

    if (failures == 0) {
        std::cout << "[PASS] test_cpu_topology" << std::endl;
        return 0;
    }
    return 1;
}
