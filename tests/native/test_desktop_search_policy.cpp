#include "Batch/CpuTopology.hpp"
#include "Batch/DesktopSearchPolicy.hpp"
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

BatchCpu::ThreadPolicy BuildPolicy(const std::string &name, uint32_t workers)
{
    BatchCpu::ThreadPolicy policy;
    policy.name = name;
    policy.workerCount = workers;
    for (uint32_t index = 0; index < workers; ++index) {
        policy.targetLogicalProcessors.push_back(index);
    }
    return policy;
}

} // namespace

int RunAllTests()
{
    int failures = 0;

    {
        Batch::ThreadPolicyRequest request;
        request.mode = Batch::ThreadPolicyMode::Balanced;

        Batch::CpuTopology topology;
        topology.isHeterogeneous = true;

        const std::vector<BatchCpu::ThreadPolicy> candidates{
            BuildPolicy("balanced-p-core", 8),
            BuildPolicy("balanced-p-core-plus-smt-partial", 12),
            BuildPolicy("balanced-p-core-plus-smt", 16),
            BuildPolicy("balanced-p-core-plus-low-core", 12),
        };

        const auto plan = Batch::BuildDesktopSearchExecutionPlan(request, topology, candidates);
        Expect(plan.runtimePolicy.name == "balanced-p-core-plus-smt",
               "heterogeneous balanced should use full SMT as the runtime ceiling",
               failures);
        Expect(plan.initialActiveWorkers == 12,
               "heterogeneous balanced should start from partial SMT workers",
               failures);
        Expect(plan.enableRecovery,
               "heterogeneous balanced should enable recovery when startup workers are below ceiling",
               failures);
    }

    {
        Batch::ThreadPolicyRequest request;
        request.mode = Batch::ThreadPolicyMode::Balanced;

        Batch::CpuTopology topology;
        topology.isHeterogeneous = true;

        const std::vector<BatchCpu::ThreadPolicy> candidates{
            BuildPolicy("balanced-p-core", 2),
            BuildPolicy("balanced-p-core-plus-smt-partial", 3),
        };

        const auto plan = Batch::BuildDesktopSearchExecutionPlan(request, topology, candidates);
        Expect(plan.runtimePolicy.name == "balanced-p-core",
               "heterogeneous balanced should stay on p-core only when worker count is small",
               failures);
        Expect(plan.initialActiveWorkers == 2,
               "heterogeneous balanced fallback should keep startup workers at the runtime ceiling",
               failures);
        Expect(!plan.enableRecovery,
               "heterogeneous balanced fallback should not enable recovery without extra headroom",
               failures);
    }

    {
        Batch::ThreadPolicyRequest request;
        request.mode = Batch::ThreadPolicyMode::Balanced;

        Batch::CpuTopology topology;
        topology.isHeterogeneous = false;

        const std::vector<BatchCpu::ThreadPolicy> candidates{
            BuildPolicy("balanced-physical", 8),
            BuildPolicy("balanced-physical-plus-smt-partial", 12),
            BuildPolicy("balanced-physical-plus-smt", 16),
        };

        const auto plan = Batch::BuildDesktopSearchExecutionPlan(request, topology, candidates);
        Expect(plan.runtimePolicy.name == "balanced-physical-plus-smt",
               "homogeneous balanced should use full SMT as the runtime ceiling",
               failures);
        Expect(plan.initialActiveWorkers == 12,
               "homogeneous balanced should start from partial SMT workers",
               failures);
        Expect(plan.enableRecovery,
               "homogeneous balanced should enable recovery when startup workers are below ceiling",
               failures);
    }

    {
        Batch::ThreadPolicyRequest request;
        request.mode = Batch::ThreadPolicyMode::Balanced;

        Batch::CpuTopology topology;
        topology.isHeterogeneous = false;

        const std::vector<BatchCpu::ThreadPolicy> candidates{
            BuildPolicy("balanced-physical", 2),
            BuildPolicy("balanced-physical-plus-smt-partial", 3),
        };

        const auto plan = Batch::BuildDesktopSearchExecutionPlan(request, topology, candidates);
        Expect(plan.runtimePolicy.name == "balanced-physical",
               "homogeneous balanced should stay on physical cores when worker count is small",
               failures);
        Expect(plan.initialActiveWorkers == 2,
               "homogeneous balanced fallback should keep startup workers at the runtime ceiling",
               failures);
        Expect(!plan.enableRecovery,
               "homogeneous balanced fallback should not enable recovery without extra headroom",
               failures);
    }

    {
        Batch::ThreadPolicyRequest request;
        request.mode = Batch::ThreadPolicyMode::Throughput;

        Batch::CpuTopology topology;

        const std::vector<BatchCpu::ThreadPolicy> candidates{
            BuildPolicy("balanced-physical", 8),
            BuildPolicy("turbo-all-logical", 16),
        };

        const auto plan = Batch::BuildDesktopSearchExecutionPlan(request, topology, candidates);
        Expect(plan.runtimePolicy.name == "turbo-all-logical",
               "turbo mode should prefer explicit turbo candidate",
               failures);
        Expect(plan.initialActiveWorkers == 12,
               "turbo mode should keep 25 percent headroom for runtime recovery on large CPUs",
               failures);
        Expect(plan.enableRecovery,
               "turbo mode should enable recovery when startup workers are below the runtime ceiling",
               failures);
    }

    {
        Batch::ThreadPolicyRequest request;
        request.mode = Batch::ThreadPolicyMode::Throughput;

        Batch::CpuTopology topology;

        const std::vector<BatchCpu::ThreadPolicy> candidates{
            BuildPolicy("balanced-physical", 8),
            BuildPolicy("balanced-physical-plus-smt-partial", 12),
            BuildPolicy("balanced-physical-plus-smt", 16),
        };

        const auto plan = Batch::BuildDesktopSearchExecutionPlan(request, topology, candidates);
        Expect(plan.runtimePolicy.name == "balanced-physical-plus-smt",
               "turbo mode should fall back to the largest candidate when turbo candidate is absent",
               failures);
        Expect(plan.initialActiveWorkers == 12,
               "turbo fallback should still keep 25 percent headroom for runtime recovery",
               failures);
        Expect(plan.enableRecovery,
               "turbo fallback should enable recovery when startup workers are below the runtime ceiling",
               failures);
    }

    {
        Batch::ThreadPolicyRequest request;
        request.mode = Batch::ThreadPolicyMode::Balanced;

        Batch::CpuTopology topology;

        const std::vector<BatchCpu::ThreadPolicy> candidates{
            BuildPolicy("unknown-a", 6),
            BuildPolicy("unknown-b", 10),
            BuildPolicy("unknown-c", 8),
        };

        const auto plan = Batch::BuildDesktopSearchExecutionPlan(request, topology, candidates);
        Expect(plan.runtimePolicy.name == "unknown-b",
               "unknown candidate names should fall back to the largest candidate",
               failures);
        Expect(plan.initialActiveWorkers == 10,
               "unknown candidate fallback should keep startup workers at the runtime ceiling",
               failures);
        Expect(!plan.enableRecovery,
               "unknown candidate fallback should not invent recovery headroom",
               failures);
    }

    {
        Batch::ThreadPolicyRequest request;
        request.mode = Batch::ThreadPolicyMode::Custom;

        Batch::CpuTopology topology;

        const std::vector<BatchCpu::ThreadPolicy> candidates{
            BuildPolicy("custom", 7),
        };

        const auto plan = Batch::BuildDesktopSearchExecutionPlan(request, topology, candidates);
        Expect(plan.runtimePolicy.name == "custom",
               "custom mode should keep the caller-provided candidate",
               failures);
        Expect(plan.initialActiveWorkers == 7,
               "custom mode should start at the runtime ceiling",
               failures);
        Expect(!plan.enableRecovery,
               "custom mode should not enable recovery by default",
               failures);
    }

    {
        Batch::ThreadPolicyRequest request;
        request.mode = Batch::ThreadPolicyMode::Balanced;

        Batch::CpuTopology topology;
        const std::vector<BatchCpu::ThreadPolicy> candidates;

        const auto plan = Batch::BuildDesktopSearchExecutionPlan(request, topology, candidates);
        Expect(plan.runtimePolicy.name.empty(),
               "empty candidate list should return default runtime policy",
               failures);
        Expect(plan.runtimePolicy.workerCount == 1,
               "default runtime policy worker count should stay at default value",
               failures);
        Expect(plan.initialActiveWorkers == 1,
               "empty candidate list should keep default startup workers",
               failures);
        Expect(!plan.enableRecovery,
               "empty candidate list should not enable recovery",
               failures);
    }

    if (failures == 0) {
        std::cout << "[PASS] test_desktop_search_policy" << std::endl;
        return 0;
    }
    return 1;
}
