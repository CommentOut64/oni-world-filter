#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace BatchCpu {

enum class CpuMode {
    Balanced,
    Turbo,
    Custom,
    Conservative
};

enum class PlacementMode {
    None,
    Preferred,
    Strict
};

struct LogicalProcessorInfo {
    uint32_t logicalIndex = 0;
    uint16_t group = 0;
    uint16_t coreIndex = 0;
    uint16_t numaNodeIndex = 0;
    uint8_t efficiencyClass = 0;
    bool parked = false;
    bool allocated = false;
};

struct CpuTopology {
    bool detectionSucceeded = false;
    bool usedFallback = false;
    bool isHeterogeneous = false;
    uint32_t physicalCoreCount = 0;
    uint32_t logicalThreadCount = 0;
    std::vector<LogicalProcessorInfo> logicalProcessors;
    std::vector<uint32_t> highPerfLogicalIndices;
    std::vector<uint32_t> lowPerfLogicalIndices;
    std::vector<uint32_t> physicalPreferredLogicalIndices;
    std::vector<uint16_t> processorGroups;
    std::vector<uint16_t> numaNodes;
    std::string diagnostics;
};

struct ThreadPolicy {
    std::string name;
    uint32_t workerCount = 1;
    bool allowSmt = false;
    bool allowLowPerf = false;
    PlacementMode placement = PlacementMode::Preferred;
    std::vector<uint32_t> targetLogicalProcessors;
};

struct PlannerInput {
    const CpuTopology *topology = nullptr;
    CpuMode mode = CpuMode::Balanced;
    uint32_t legacyThreadOverride = 0;
    uint32_t customWorkers = 0;
    bool customAllowSmt = true;
    bool customAllowLowPerf = true;
    PlacementMode customPlacement = PlacementMode::Preferred;
};

class CpuTopologyDetector
{
public:
    static CpuTopology Detect();
};

class ThreadPolicyPlanner
{
public:
    static std::vector<ThreadPolicy> BuildCandidates(const PlannerInput &input);
    static ThreadPolicy BuildConservativePolicy(const CpuTopology &topology);
};

struct ThroughputStats {
    double averageSeedsPerSecond = 0.0;
    double stddevSeedsPerSecond = 0.0;
    uint64_t processedSeeds = 0;
    bool valid = false;
};

struct WarmupConfig {
    bool enabled = true;
    std::chrono::milliseconds perCandidateBudget{2500};
    std::chrono::milliseconds totalBudget{10000};
    double tieToleranceRatio = 0.03;
};

struct WarmupCandidateResult {
    ThreadPolicy policy;
    ThroughputStats stats;
};

class ThroughputCalibrator
{
public:
    using Evaluator = std::function<ThroughputStats(
        const ThreadPolicy &, std::chrono::milliseconds)>;

    static std::vector<WarmupCandidateResult> Evaluate(
        const std::vector<ThreadPolicy> &candidates,
        const WarmupConfig &config,
        const Evaluator &evaluator);

    static size_t PickBestIndex(const std::vector<WarmupCandidateResult> &results,
                                double tieToleranceRatio);
};

struct AdaptiveConfig {
    bool enabled = true;
    uint32_t minWorkers = 1;
    double dropThreshold = 0.12;
    int consecutiveDropWindows = 3;
    std::chrono::milliseconds cooldown{8000};
};

class AdaptiveConcurrencyController
{
public:
    AdaptiveConcurrencyController(AdaptiveConfig config, uint32_t initialWorkers);

    std::optional<uint32_t> Observe(double currentSeedsPerSecond,
                                    uint32_t currentWorkers,
                                    std::chrono::steady_clock::time_point now);

    uint32_t ReductionCount() const { return m_reductionCount; }
    double PeakSeedsPerSecond() const { return m_peakSeedsPerSecond; }

private:
    AdaptiveConfig m_config{};
    uint32_t m_initialWorkers = 1;
    uint32_t m_reductionCount = 0;
    double m_peakSeedsPerSecond = 0.0;
    double m_stagePeakSeedsPerSecond = 0.0;
    int m_consecutiveDrops = 0;
    std::chrono::steady_clock::time_point m_lastAdjustment{};
};

CpuMode ParseCpuMode(const std::string &value);
PlacementMode ParsePlacementMode(const std::string &value);
const char *ToString(CpuMode mode);
const char *ToString(PlacementMode mode);
std::string JoinLogicalList(const std::vector<uint32_t> &values,
                            size_t maxItems = 16);

bool ApplyThreadPlacement(const ThreadPolicy &policy,
                          uint32_t workerIndex,
                          std::string *errorMessage = nullptr);

} // namespace BatchCpu
