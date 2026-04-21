#include "BatchCpu/SearchCpuGovernor.hpp"

#include <algorithm>

namespace BatchCpu {

SearchCpuGovernor::SearchCpuGovernor(const CompiledSearchCpuPlan &plan,
                                     SearchCpuGovernorConfig config)
    : m_config(std::move(config)),
      m_startupActivePhysicalCores(std::max<uint32_t>(1, plan.envelope.startupPhysicalCoreCount)),
      m_lastAdjustment(std::chrono::steady_clock::time_point::min())
{
    if (m_config.minActivePhysicalCores == 0) {
        m_config.minActivePhysicalCores = 1;
    }
    if (m_config.scaleDownThreshold < 0.0) {
        m_config.scaleDownThreshold = 0.0;
    }
    if (m_config.scaleDownWindowCount < 1) {
        m_config.scaleDownWindowCount = 1;
    }
    if (m_config.scaleUpWindowCount < 1) {
        m_config.scaleUpWindowCount = 1;
    }
    m_config.scaleUpRetentionRatio = std::clamp(m_config.scaleUpRetentionRatio, 0.0, 1.0);
    m_config.minActivePhysicalCores =
        std::min(m_config.minActivePhysicalCores, m_startupActivePhysicalCores);

    m_cumulativeWorkersByPhysicalCoreCount.push_back(0);
    uint32_t runningTotal = 0;
    for (const auto &core : plan.placement.plannedCoresByPriority) {
        runningTotal += static_cast<uint32_t>(core.allowedLogicalThreads.size());
        m_cumulativeWorkersByPhysicalCoreCount.push_back(runningTotal);
    }
    if (m_cumulativeWorkersByPhysicalCoreCount.size() <= m_startupActivePhysicalCores) {
        m_cumulativeWorkersByPhysicalCoreCount.resize(static_cast<size_t>(m_startupActivePhysicalCores) + 1,
                                                      runningTotal);
    }
}

uint32_t SearchCpuGovernor::ClampActivePhysicalCores(uint32_t activePhysicalCores) const
{
    return std::clamp(activePhysicalCores,
                      m_config.minActivePhysicalCores,
                      m_startupActivePhysicalCores);
}

uint32_t SearchCpuGovernor::ActiveWorkerCountFor(uint32_t activePhysicalCores) const
{
    const uint32_t clamped = ClampActivePhysicalCores(activePhysicalCores);
    if (clamped >= m_cumulativeWorkersByPhysicalCoreCount.size()) {
        return m_cumulativeWorkersByPhysicalCoreCount.back();
    }
    return m_cumulativeWorkersByPhysicalCoreCount[clamped];
}

std::optional<uint32_t> SearchCpuGovernor::Observe(
    double seedsPerSecond,
    uint32_t currentActivePhysicalCores,
    std::chrono::steady_clock::time_point now)
{
    if (!m_config.enabled || !(seedsPerSecond > 0.0)) {
        return std::nullopt;
    }

    const uint32_t current = ClampActivePhysicalCores(currentActivePhysicalCores);
    if (m_lastObservedPhysicalCores != current) {
        m_lastObservedPhysicalCores = current;
        m_stagePeakSeedsPerSecond = seedsPerSecond;
        m_stageBaselineSeedsPerSecond = seedsPerSecond;
        m_consecutiveDropWindows = 0;
        m_consecutiveStableWindows = 0;
        m_skipNextStageSample = false;
        return std::nullopt;
    }

    if (m_skipNextStageSample) {
        m_skipNextStageSample = false;
        return std::nullopt;
    }

    if (!(m_stageBaselineSeedsPerSecond > 0.0)) {
        m_stagePeakSeedsPerSecond = seedsPerSecond;
        m_stageBaselineSeedsPerSecond = seedsPerSecond;
        m_consecutiveDropWindows = 0;
        m_consecutiveStableWindows = 0;
        return std::nullopt;
    }

    m_stagePeakSeedsPerSecond = std::max(m_stagePeakSeedsPerSecond, seedsPerSecond);

    if (current > m_config.minActivePhysicalCores &&
        seedsPerSecond <= m_stagePeakSeedsPerSecond * (1.0 - m_config.scaleDownThreshold)) {
        ++m_consecutiveDropWindows;
    } else {
        m_consecutiveDropWindows = 0;
    }

    if (current < m_startupActivePhysicalCores &&
        seedsPerSecond >= m_stageBaselineSeedsPerSecond * m_config.scaleUpRetentionRatio) {
        ++m_consecutiveStableWindows;
    } else {
        m_consecutiveStableWindows = 0;
    }

    const bool coolingDown =
        m_lastAdjustment != std::chrono::steady_clock::time_point::min() &&
        now - m_lastAdjustment < m_config.cooldown;
    if (coolingDown) {
        return std::nullopt;
    }

    if (m_consecutiveDropWindows >= m_config.scaleDownWindowCount) {
        const uint32_t next = current - 1;
        m_lastObservedPhysicalCores = next;
        m_stagePeakSeedsPerSecond = 0.0;
        m_stageBaselineSeedsPerSecond = 0.0;
        m_consecutiveDropWindows = 0;
        m_consecutiveStableWindows = 0;
        m_skipNextStageSample = true;
        m_lastAdjustment = now;
        return next;
    }

    if (m_consecutiveStableWindows >= m_config.scaleUpWindowCount) {
        const uint32_t next = current + 1;
        m_lastObservedPhysicalCores = next;
        m_stagePeakSeedsPerSecond = 0.0;
        m_stageBaselineSeedsPerSecond = 0.0;
        m_consecutiveDropWindows = 0;
        m_consecutiveStableWindows = 0;
        m_skipNextStageSample = true;
        m_lastAdjustment = now;
        return next;
    }

    return std::nullopt;
}

} // namespace BatchCpu
