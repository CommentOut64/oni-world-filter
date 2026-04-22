#pragma once

#include "BatchCpu/SearchCpuPlan.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

namespace BatchCpu {

struct SearchCpuGovernorConfig {
    bool enabled = true;
    uint32_t minActivePhysicalCores = 1;
    double scaleDownThreshold = 0.12;
    int scaleDownWindowCount = 3;
    int scaleUpWindowCount = 4;
    double scaleUpRetentionRatio = 0.97;
    std::chrono::milliseconds cooldown{12000};
};

class SearchCpuGovernor
{
public:
    SearchCpuGovernor(const CompiledSearchCpuPlan &plan,
                      SearchCpuGovernorConfig config);

    uint32_t StartupActivePhysicalCores() const { return m_startupActivePhysicalCores; }
    uint32_t ActiveWorkerCountFor(uint32_t activePhysicalCores) const;

    std::optional<uint32_t> Observe(double seedsPerSecond,
                                    uint32_t currentActivePhysicalCores,
                                    std::chrono::steady_clock::time_point now);

private:
    uint32_t ClampActivePhysicalCores(uint32_t activePhysicalCores) const;

    SearchCpuGovernorConfig m_config{};
    uint32_t m_startupActivePhysicalCores = 1;
    std::vector<uint32_t> m_cumulativeWorkersByPhysicalCoreCount;
    uint32_t m_lastObservedPhysicalCores = 0;
    double m_stagePeakSeedsPerSecond = 0.0;
    double m_stageBaselineSeedsPerSecond = 0.0;
    int m_consecutiveDropWindows = 0;
    int m_consecutiveStableWindows = 0;
    bool m_skipNextStageSample = false;
    std::chrono::steady_clock::time_point m_lastAdjustment{};
};

} // namespace BatchCpu
