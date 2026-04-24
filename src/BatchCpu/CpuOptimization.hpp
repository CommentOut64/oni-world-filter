#pragma once

#include "BatchCpu/SearchCpuPlan.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace BatchCpu {

struct ThreadBindingTarget {
    uint16_t group = 0;
    uint32_t logicalIndex = 0;
    uint16_t coreIndex = 0;
    uint16_t numaNodeIndex = 0;
};

class CpuTopologyDetector
{
public:
    static CpuTopologyFacts DetectFacts();
};

struct ThroughputStats {
    double averageSeedsPerSecond = 0.0;
    double stddevSeedsPerSecond = 0.0;
    uint64_t processedSeeds = 0;
    bool valid = false;
};

struct ThreadPlacementDirective {
    std::vector<uint32_t> selectedCpuSetIds;
    std::optional<ThreadBindingTarget> bindingTarget;
};

CpuMode ParseCpuMode(const std::string &value);
PlacementMode ParsePlacementMode(const std::string &value);
const char *ToString(CpuMode mode);
const char *ToString(PlacementMode mode);
std::string JoinLogicalList(const std::vector<uint32_t> &values,
                            size_t maxItems = 16);
bool IsHighPerformanceEfficiencyClass(bool isHeterogeneous,
                                      uint8_t efficiencyClass,
                                      uint8_t highestEfficiencyClass);

std::optional<ThreadBindingTarget> ResolveThreadBindingTarget(const CpuPlacementPlan &plan,
                                                              uint32_t workerIndex);
std::optional<uint32_t> ResolveWorkerCpuSetId(const CpuPlacementPlan &plan,
                                              uint32_t workerIndex);
std::vector<uint32_t> ResolveAllowedCpuSetIds(const CompiledSearchCpuPlan &plan);
std::optional<std::vector<uint32_t>> GetProcessDefaultCpuSetIds(
    std::string *errorMessage = nullptr);
bool SetProcessDefaultCpuSets(const std::vector<uint32_t> &cpuSetIds,
                              std::string *errorMessage = nullptr);
std::optional<ThreadPlacementDirective> ResolveThreadPlacementDirective(
    const CompiledSearchCpuPlan &plan,
    uint32_t workerIndex);

bool ApplyThreadPlacement(const WorkerBindingSlot &slot,
                          PlacementMode placement,
                          std::string *errorMessage = nullptr);

bool ApplyThreadPlacement(const CpuPlacementPlan &plan,
                          PlacementMode placement,
                          uint32_t workerIndex,
                          std::string *errorMessage = nullptr);

bool ApplyThreadPlacement(const CompiledSearchCpuPlan &plan,
                          uint32_t workerIndex,
                          std::string *errorMessage = nullptr);

} // namespace BatchCpu
