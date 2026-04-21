#include "Batch/FilterConfig.hpp"
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

BatchCpu::CpuTopologyFacts BuildSmt2Topology()
{
    BatchCpu::CpuTopologyFacts topology;
    topology.detectionSucceeded = true;
    topology.isHeterogeneous = false;
    topology.diagnostics = "runtime topology";
    topology.physicalCoresBySystemOrder = {
        {.physicalCoreIndex = 0,
         .group = 0,
         .coreIndex = 0,
         .numaNodeIndex = 0,
         .efficiencyClass = 0,
         .isHighPerformance = true,
         .logicalThreads = {
             {.logicalIndex = 0, .group = 0, .coreIndex = 0, .numaNodeIndex = 0, .isPrimaryThread = true},
             {.logicalIndex = 1, .group = 0, .coreIndex = 0, .numaNodeIndex = 0, .isPrimaryThread = false},
         }},
        {.physicalCoreIndex = 1,
         .group = 0,
         .coreIndex = 1,
         .numaNodeIndex = 0,
         .efficiencyClass = 0,
         .isHighPerformance = true,
         .logicalThreads = {
             {.logicalIndex = 2, .group = 0, .coreIndex = 1, .numaNodeIndex = 0, .isPrimaryThread = true},
             {.logicalIndex = 3, .group = 0, .coreIndex = 1, .numaNodeIndex = 0, .isPrimaryThread = false},
         }},
    };
    return topology;
}

} // namespace

int RunAllTests()
{
    int failures = 0;
    const auto topology = BuildSmt2Topology();

    {
        Batch::FilterConfig cfg;
        cfg.hasCpuSection = true;
        cfg.cpu.mode = "custom";
        cfg.cpu.allowSmt = false;
        cfg.cpu.allowLowPerf = true;
        cfg.cpu.placement = "strict";

        const auto runtime = Batch::CompileSearchCpuRuntime(cfg, topology);
        Expect(runtime.cpuPlan.policy.mode == BatchCpu::CpuMode::Turbo,
               "legacy custom mode should normalize to turbo",
               failures);
        Expect(!runtime.cpuPlan.policy.allowSmt,
               "normalized runtime should keep allowSmt",
               failures);
        Expect(runtime.cpuPlan.policy.allowLowPerf,
               "normalized runtime should keep allowLowPerf",
               failures);
        Expect(runtime.cpuPlan.policy.binding == BatchCpu::PlacementMode::Strict,
               "normalized runtime should keep strict binding",
               failures);
    }

    {
        Batch::FilterConfig cfg;
        cfg.hasCpuSection = true;
        cfg.cpu.mode = "conservative";

        const auto runtime = Batch::CompileSearchCpuRuntime(cfg, topology);
        Expect(runtime.cpuPlan.policy.mode == BatchCpu::CpuMode::Balanced,
               "legacy conservative mode should normalize to balanced",
               failures);
    }

    {
        Batch::FilterConfig cfg;
        cfg.threads = 3;

        const auto runtime = Batch::CompileSearchCpuRuntime(cfg, topology);
        Expect(runtime.cpuPlan.policy.mode == BatchCpu::CpuMode::Turbo,
               "legacy threads-only config should normalize to turbo",
               failures);
        Expect(runtime.cpuPlan.policy.binding == BatchCpu::PlacementMode::Preferred,
               "threads-only config should use preferred binding",
               failures);
    }

    {
        Batch::FilterConfig cfg;
        cfg.hasCpuSection = true;
        cfg.cpu.mode = "turbo";
        cfg.cpu.allowSmt = true;
        cfg.cpu.allowLowPerf = false;
        cfg.cpu.placement = "strict";

        const auto runtime = Batch::CompileSearchCpuRuntime(cfg, topology);
        const auto summary = Batch::DescribeCompiledSearchCpuPlan(runtime.cpuPlan);
        Expect(summary.find("mode=turbo") != std::string::npos,
               "summary should include normalized cpu mode",
               failures);
        Expect(summary.find("binding=strict") != std::string::npos,
               "summary should include binding",
               failures);
        Expect(summary.find("workers=4") != std::string::npos,
               "summary should include absolute worker cap",
               failures);
        Expect(summary.find("cores=[g0:c0->0+1, g0:c1->2+3]") != std::string::npos,
               "summary should include placement order from compiled plan",
               failures);
    }

    {
        Batch::FilterConfig cfg;
        cfg.hasCpuSection = true;
        cfg.cpu.mode = "turbo";
        cfg.cpu.allowSmt = true;

        const auto runtime = Batch::CompileSearchCpuRuntime(cfg, topology);
        Expect(Batch::ResolveActivePhysicalCoreCapFromWorkerLimit(runtime.cpuPlan, 0) == 0,
               "worker cap 0 should map to 0 physical cores",
               failures);
        Expect(Batch::ResolveActivePhysicalCoreCapFromWorkerLimit(runtime.cpuPlan, 1) == 1,
               "worker cap 1 should keep at least one physical core active",
               failures);
        Expect(Batch::ResolveActivePhysicalCoreCapFromWorkerLimit(runtime.cpuPlan, 3) == 1,
               "worker cap 3 should still map to one SMT2 physical core",
               failures);
        Expect(Batch::ResolveActivePhysicalCoreCapFromWorkerLimit(runtime.cpuPlan, 4) == 2,
               "worker cap 4 should map to both physical cores",
               failures);
    }

    if (failures == 0) {
        std::cout << "[PASS] test_search_cpu_runtime" << std::endl;
        return 0;
    }
    return 1;
}
