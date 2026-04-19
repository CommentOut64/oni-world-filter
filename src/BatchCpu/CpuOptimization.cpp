#include "BatchCpu/CpuOptimization.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <thread>
#include <tuple>
#include <unordered_set>

#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef NOGDI
#define NOGDI
#endif
#include <windows.h>
#endif

namespace BatchCpu {
namespace {

std::string ToLower(const std::string &value)
{
    std::string out = value;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return (char)std::tolower(c);
    });
    return out;
}

uint32_t SafeHardwareConcurrency()
{
    const auto hw = std::thread::hardware_concurrency();
    return hw > 0 ? hw : 4U;
}

std::vector<uint32_t> UniqueSorted(std::vector<uint32_t> values)
{
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

CpuTopology BuildFallbackTopology(const std::string &reason)
{
    CpuTopology fallback;
    fallback.detectionSucceeded = false;
    fallback.usedFallback = true;
    fallback.isHeterogeneous = false;
    fallback.logicalThreadCount = SafeHardwareConcurrency();
    fallback.physicalCoreCount = std::max<uint32_t>(1, fallback.logicalThreadCount / 2);
    fallback.highPerfLogicalIndices.reserve(fallback.logicalThreadCount);
    fallback.physicalPreferredLogicalIndices.reserve(fallback.physicalCoreCount);

    for (uint32_t i = 0; i < fallback.logicalThreadCount; ++i) {
        LogicalProcessorInfo info;
        info.logicalIndex = i;
        info.group = 0;
        info.coreIndex = (uint16_t)(i / 2);
        info.numaNodeIndex = 0;
        info.efficiencyClass = 0;
        fallback.logicalProcessors.push_back(info);
        fallback.highPerfLogicalIndices.push_back(i);
        if (i % 2 == 0 && fallback.physicalPreferredLogicalIndices.size() < fallback.physicalCoreCount) {
            fallback.physicalPreferredLogicalIndices.push_back(i);
        }
    }
    if (fallback.physicalPreferredLogicalIndices.empty() && !fallback.highPerfLogicalIndices.empty()) {
        fallback.physicalPreferredLogicalIndices.push_back(fallback.highPerfLogicalIndices.front());
    }
    fallback.processorGroups.push_back(0);
    fallback.numaNodes.push_back(0);
    fallback.diagnostics = "fallback topology: " + reason;
    return fallback;
}

std::vector<uint32_t> BuildAllLogicalIndices(const CpuTopology &topology)
{
    std::vector<uint32_t> all;
    all.reserve(topology.logicalProcessors.size());
    for (const auto &lp : topology.logicalProcessors) {
        all.push_back(lp.logicalIndex);
    }
    return UniqueSorted(std::move(all));
}

std::vector<uint32_t> Intersect(const std::vector<uint32_t> &a,
                                const std::vector<uint32_t> &b)
{
    std::vector<uint32_t> result;
    result.reserve(std::min(a.size(), b.size()));
    std::set_intersection(a.begin(), a.end(),
                          b.begin(), b.end(),
                          std::back_inserter(result));
    return result;
}

std::vector<uint32_t> Difference(const std::vector<uint32_t> &a,
                                 const std::vector<uint32_t> &b)
{
    std::vector<uint32_t> result;
    result.reserve(a.size());
    std::set_difference(a.begin(), a.end(),
                        b.begin(), b.end(),
                        std::back_inserter(result));
    return result;
}

std::vector<uint32_t> Prefix(const std::vector<uint32_t> &values, size_t count)
{
    if (values.size() <= count) {
        return values;
    }
    return std::vector<uint32_t>(values.begin(), values.begin() + (ptrdiff_t)count);
}

std::vector<uint32_t> Concat(const std::vector<uint32_t> &a,
                             const std::vector<uint32_t> &b)
{
    std::vector<uint32_t> out = a;
    out.insert(out.end(), b.begin(), b.end());
    return UniqueSorted(std::move(out));
}

std::string PolicySignature(const ThreadPolicy &policy)
{
    std::ostringstream oss;
    oss << policy.workerCount << "|" << (int)policy.allowSmt << "|" << (int)policy.allowLowPerf
        << "|" << (int)policy.placement << "|";
    for (size_t i = 0; i < policy.targetLogicalProcessors.size(); ++i) {
        if (i > 0) {
            oss << ",";
        }
        oss << policy.targetLogicalProcessors[i];
    }
    return oss.str();
}

ThreadPolicy BuildPolicy(const std::string &name,
                         const std::vector<uint32_t> &targets,
                         bool allowSmt,
                         bool allowLowPerf,
                         PlacementMode placement)
{
    ThreadPolicy policy;
    policy.name = name;
    policy.allowSmt = allowSmt;
    policy.allowLowPerf = allowLowPerf;
    policy.placement = placement;
    policy.targetLogicalProcessors = UniqueSorted(targets);
    policy.workerCount = std::max<uint32_t>(
        1, policy.targetLogicalProcessors.empty() ? 1U : (uint32_t)policy.targetLogicalProcessors.size());
    return policy;
}

} // namespace

CpuTopology CpuTopologyDetector::Detect()
{
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
    using GetSystemCpuSetInformationFn = BOOL (WINAPI *)(
        PSYSTEM_CPU_SET_INFORMATION, ULONG, PULONG, HANDLE, ULONG);

    auto *kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (kernel32 == nullptr) {
        return BuildFallbackTopology("GetModuleHandleW(kernel32.dll) failed");
    }

    auto rawProc = GetProcAddress(kernel32, "GetSystemCpuSetInformation");
    GetSystemCpuSetInformationFn getCpuSetInfo = nullptr;
    static_assert(sizeof(rawProc) == sizeof(getCpuSetInfo));
    std::memcpy(&getCpuSetInfo, &rawProc, sizeof(getCpuSetInfo));
    if (getCpuSetInfo == nullptr) {
        return BuildFallbackTopology("GetSystemCpuSetInformation unavailable");
    }

    ULONG bytesNeeded = 0;
    SetLastError(0);
    const BOOL probeOk = getCpuSetInfo(nullptr, 0, &bytesNeeded, nullptr, 0);
    if (probeOk != FALSE || bytesNeeded == 0) {
        return BuildFallbackTopology("failed to probe cpu set buffer size");
    }
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        return BuildFallbackTopology("unexpected GetSystemCpuSetInformation probe status");
    }

    std::vector<unsigned char> buffer(bytesNeeded);
    if (getCpuSetInfo(reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buffer.data()),
                      bytesNeeded,
                      &bytesNeeded,
                      nullptr,
                      0) == FALSE) {
        return BuildFallbackTopology("GetSystemCpuSetInformation call failed");
    }

    CpuTopology topology;
    topology.detectionSucceeded = true;
    topology.usedFallback = false;

    std::set<uint16_t> groups;
    std::set<uint16_t> numaNodes;
    std::set<uint8_t> efficiencyClasses;

    size_t offset = 0;
    while (offset + sizeof(SYSTEM_CPU_SET_INFORMATION) <= buffer.size()) {
        const auto *raw =
            reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION *>(buffer.data() + offset);
        if (raw->Size == 0) {
            break;
        }
        if (raw->Type == CpuSetInformation) {
            const auto &cpu = raw->CpuSet;
            LogicalProcessorInfo info;
            info.logicalIndex = cpu.LogicalProcessorIndex;
            info.group = cpu.Group;
            info.coreIndex = cpu.CoreIndex;
            info.numaNodeIndex = cpu.NumaNodeIndex;
            info.efficiencyClass = cpu.EfficiencyClass;
            info.parked = cpu.Parked != 0;
            info.allocated = cpu.Allocated != 0;
            topology.logicalProcessors.push_back(info);
            groups.insert(info.group);
            numaNodes.insert(info.numaNodeIndex);
            efficiencyClasses.insert(info.efficiencyClass);
        }
        offset += raw->Size;
    }

    if (topology.logicalProcessors.empty()) {
        return BuildFallbackTopology("cpu set list empty");
    }

    std::sort(topology.logicalProcessors.begin(), topology.logicalProcessors.end(),
              [](const LogicalProcessorInfo &a, const LogicalProcessorInfo &b) {
                  if (a.group != b.group) {
                      return a.group < b.group;
                  }
                  if (a.logicalIndex != b.logicalIndex) {
                      return a.logicalIndex < b.logicalIndex;
                  }
                  return a.coreIndex < b.coreIndex;
              });

    topology.logicalThreadCount = (uint32_t)topology.logicalProcessors.size();
    topology.isHeterogeneous = efficiencyClasses.size() > 1;
    topology.processorGroups.assign(groups.begin(), groups.end());
    topology.numaNodes.assign(numaNodes.begin(), numaNodes.end());

    std::map<std::tuple<uint16_t, uint16_t>, std::vector<const LogicalProcessorInfo *>> byCore;
    for (const auto &lp : topology.logicalProcessors) {
        byCore[{lp.group, lp.coreIndex}].push_back(&lp);
    }
    topology.physicalCoreCount = (uint32_t)byCore.size();

    std::vector<const LogicalProcessorInfo *> sortedByPreference;
    sortedByPreference.reserve(topology.logicalProcessors.size());
    for (const auto &lp : topology.logicalProcessors) {
        sortedByPreference.push_back(&lp);
    }
    std::sort(sortedByPreference.begin(), sortedByPreference.end(),
              [](const LogicalProcessorInfo *a, const LogicalProcessorInfo *b) {
                  if (a->efficiencyClass != b->efficiencyClass) {
                      return a->efficiencyClass < b->efficiencyClass;
                  }
                  if (a->group != b->group) {
                      return a->group < b->group;
                  }
                  if (a->coreIndex != b->coreIndex) {
                      return a->coreIndex < b->coreIndex;
                  }
                  return a->logicalIndex < b->logicalIndex;
              });

    std::set<std::tuple<uint16_t, uint16_t>> seenCore;
    for (const auto *lp : sortedByPreference) {
        const auto key = std::tuple<uint16_t, uint16_t>{lp->group, lp->coreIndex};
        if (seenCore.insert(key).second) {
            topology.physicalPreferredLogicalIndices.push_back(lp->logicalIndex);
        }
    }
    topology.physicalPreferredLogicalIndices = UniqueSorted(topology.physicalPreferredLogicalIndices);

    const uint8_t minEfficiencyClass = sortedByPreference.front()->efficiencyClass;
    for (const auto &lp : topology.logicalProcessors) {
        if (!topology.isHeterogeneous || lp.efficiencyClass == minEfficiencyClass) {
            topology.highPerfLogicalIndices.push_back(lp.logicalIndex);
        } else {
            topology.lowPerfLogicalIndices.push_back(lp.logicalIndex);
        }
    }

    topology.highPerfLogicalIndices = UniqueSorted(topology.highPerfLogicalIndices);
    topology.lowPerfLogicalIndices = UniqueSorted(topology.lowPerfLogicalIndices);
    if (topology.highPerfLogicalIndices.empty()) {
        topology.highPerfLogicalIndices = BuildAllLogicalIndices(topology);
    }

    std::ostringstream summary;
    summary << "cpu topology: logical=" << topology.logicalThreadCount
            << ", physical=" << topology.physicalCoreCount
            << ", groups=" << topology.processorGroups.size()
            << ", numa=" << topology.numaNodes.size()
            << ", hetero=" << (topology.isHeterogeneous ? "yes" : "no")
            << ", high_perf=" << topology.highPerfLogicalIndices.size()
            << ", low_perf=" << topology.lowPerfLogicalIndices.size();
    topology.diagnostics = summary.str();

    return topology;
#else
    return BuildFallbackTopology("non-windows build");
#endif
}

ThreadPolicy ThreadPolicyPlanner::BuildConservativePolicy(const CpuTopology &topology)
{
    const auto allLogical = BuildAllLogicalIndices(topology);
    auto target = topology.physicalPreferredLogicalIndices;
    if (target.empty()) {
        target = allLogical;
    }
    if (target.empty()) {
        target.push_back(0);
    }
    const uint32_t conservativeWorkers = std::max<uint32_t>(
        1, std::min<uint32_t>((uint32_t)target.size(),
                              std::max<uint32_t>(1, topology.logicalThreadCount / 2)));
    target = Prefix(target, conservativeWorkers);
    return BuildPolicy("conservative", target, false, false, PlacementMode::Preferred);
}

std::vector<ThreadPolicy> ThreadPolicyPlanner::BuildCandidates(const PlannerInput &input)
{
    const CpuTopology fallback = BuildFallbackTopology("planner topology missing");
    const CpuTopology &topology = input.topology != nullptr ? *input.topology : fallback;

    const auto allLogical = BuildAllLogicalIndices(topology);
    auto physical = topology.physicalPreferredLogicalIndices;
    if (physical.empty()) {
        physical = allLogical;
    }
    auto highPerf = topology.highPerfLogicalIndices;
    if (highPerf.empty()) {
        highPerf = allLogical;
    }
    auto lowPerf = topology.lowPerfLogicalIndices;

    std::vector<ThreadPolicy> candidates;
    std::unordered_set<std::string> dedup;

    auto pushCandidate = [&](ThreadPolicy policy) {
        if (policy.targetLogicalProcessors.empty()) {
            policy.targetLogicalProcessors = allLogical;
            policy.workerCount = std::max<uint32_t>(1, (uint32_t)policy.targetLogicalProcessors.size());
        }
        const auto signature = PolicySignature(policy);
        if (dedup.insert(signature).second) {
            candidates.push_back(std::move(policy));
        }
    };

    if (input.legacyThreadOverride > 0) {
        const uint32_t workers = std::min<uint32_t>(
            input.legacyThreadOverride, std::max<uint32_t>(1, (uint32_t)allLogical.size()));
        pushCandidate(BuildPolicy("legacy-threads", Prefix(allLogical, workers),
                                  true, true, PlacementMode::Preferred));
        return candidates;
    }

    if (input.mode == CpuMode::Conservative) {
        pushCandidate(BuildConservativePolicy(topology));
        return candidates;
    }

    if (input.mode == CpuMode::Custom) {
        std::vector<uint32_t> base = allLogical;
        if (!input.customAllowLowPerf && topology.isHeterogeneous) {
            base = highPerf;
        }
        if (!input.customAllowSmt) {
            base = Intersect(base, physical);
        }
        if (base.empty()) {
            base = allLogical;
        }
        if (base.empty()) {
            base.push_back(0);
        }
        const uint32_t workers = std::max<uint32_t>(
            1, std::min<uint32_t>(
                   input.customWorkers > 0 ? input.customWorkers : (uint32_t)base.size(),
                   (uint32_t)base.size()));
        auto target = Prefix(base, workers);
        ThreadPolicy custom = BuildPolicy("custom", target,
                                          input.customAllowSmt,
                                          input.customAllowLowPerf,
                                          input.customPlacement);
        custom.workerCount = workers;
        pushCandidate(custom);
        return candidates;
    }

    const auto highPhysical = Intersect(highPerf, physical);
    const auto highSmt = Difference(highPerf, highPhysical);
    const auto lowPhysical = Difference(physical, highPhysical);
    const auto fullSmt = Difference(allLogical, physical);

    if (topology.isHeterogeneous) {
        auto pOnly = !highPhysical.empty() ? highPhysical : physical;
        if (!pOnly.empty()) {
            pushCandidate(BuildPolicy("balanced-p-core", pOnly, false, false, PlacementMode::Preferred));
        }
        if (!highSmt.empty() && !pOnly.empty()) {
            auto pPlusPartialSmt = Concat(pOnly, Prefix(highSmt, std::max<size_t>(1, highSmt.size() / 2)));
            pushCandidate(BuildPolicy("balanced-p-core-plus-smt-partial",
                                      pPlusPartialSmt, true, false, PlacementMode::Preferred));
            auto pPlusFullSmt = Concat(pOnly, highSmt);
            pushCandidate(BuildPolicy("balanced-p-core-plus-smt",
                                      pPlusFullSmt, true, false, PlacementMode::Preferred));
        }
        if (!lowPhysical.empty() && !pOnly.empty()) {
            auto pPlusLow = Concat(pOnly, lowPhysical);
            pushCandidate(BuildPolicy("balanced-p-core-plus-low-core",
                                      pPlusLow, false, true, PlacementMode::Preferred));
        }
        if (input.mode == CpuMode::Turbo) {
            auto turbo = Concat(highPerf, lowPerf);
            if (turbo.empty()) {
                turbo = allLogical;
            }
            pushCandidate(BuildPolicy("turbo-all-candidates", turbo, true, true, PlacementMode::Strict));
        }
    } else {
        if (!physical.empty()) {
            pushCandidate(BuildPolicy("balanced-physical", physical, false, false, PlacementMode::Preferred));
        }
        if (!fullSmt.empty() && !physical.empty()) {
            auto partialSmt = Concat(physical, Prefix(fullSmt, std::max<size_t>(1, fullSmt.size() / 2)));
            pushCandidate(BuildPolicy("balanced-physical-plus-smt-partial",
                                      partialSmt, true, false, PlacementMode::Preferred));
            auto full = Concat(physical, fullSmt);
            pushCandidate(BuildPolicy("balanced-physical-plus-smt",
                                      full, true, false, PlacementMode::Preferred));
        }
        if (input.mode == CpuMode::Turbo) {
            auto full = allLogical;
            if (full.empty()) {
                full.push_back(0);
            }
            pushCandidate(BuildPolicy("turbo-all-logical", full, true, true, PlacementMode::Strict));
        }
    }

    if (candidates.empty()) {
        pushCandidate(BuildConservativePolicy(topology));
    }

    return candidates;
}

std::vector<WarmupCandidateResult> ThroughputCalibrator::Evaluate(
    const std::vector<ThreadPolicy> &candidates,
    const WarmupConfig &config,
    const Evaluator &evaluator)
{
    std::vector<WarmupCandidateResult> results;
    if (candidates.empty()) {
        return results;
    }
    if (!config.enabled) {
        for (const auto &candidate : candidates) {
            WarmupCandidateResult item;
            item.policy = candidate;
            item.stats = evaluator(candidate, std::chrono::milliseconds(0));
            results.push_back(std::move(item));
        }
        return results;
    }

    const auto startedAt = std::chrono::steady_clock::now();
    const size_t minCompareCandidates = std::min<size_t>(2, candidates.size());
    for (const auto &candidate : candidates) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startedAt);
        if (elapsed >= config.totalBudget && results.size() >= minCompareCandidates) {
            break;
        }
        auto budgetLeft = config.totalBudget - elapsed;
        auto candidateBudget = std::min(config.perCandidateBudget, budgetLeft);
        if (candidateBudget.count() <= 0) {
            candidateBudget = config.perCandidateBudget;
        }
        WarmupCandidateResult item;
        item.policy = candidate;
        item.stats = evaluator(candidate, candidateBudget);
        results.push_back(std::move(item));
    }

    if (results.empty()) {
        WarmupCandidateResult item;
        item.policy = candidates.front();
        item.stats = evaluator(candidates.front(), config.perCandidateBudget);
        results.push_back(std::move(item));
    }

    return results;
}

size_t ThroughputCalibrator::PickBestIndex(const std::vector<WarmupCandidateResult> &results,
                                           double tieToleranceRatio)
{
    if (results.empty()) {
        return 0;
    }
    size_t bestIndex = 0;
    bool foundValid = false;
    for (size_t i = 0; i < results.size(); ++i) {
        if (!results[i].stats.valid) {
            continue;
        }
        if (!foundValid) {
            bestIndex = i;
            foundValid = true;
            continue;
        }
        const auto &best = results[bestIndex];
        const auto &cur = results[i];
        const double bestAvg = best.stats.averageSeedsPerSecond;
        const double curAvg = cur.stats.averageSeedsPerSecond;
        if (curAvg > bestAvg * (1.0 + tieToleranceRatio)) {
            bestIndex = i;
            continue;
        }
        const bool inTieRange = std::fabs(curAvg - bestAvg) <= bestAvg * tieToleranceRatio;
        if (inTieRange) {
            if (cur.policy.workerCount < best.policy.workerCount) {
                bestIndex = i;
                continue;
            }
            if (cur.policy.workerCount == best.policy.workerCount &&
                cur.stats.stddevSeedsPerSecond < best.stats.stddevSeedsPerSecond) {
                bestIndex = i;
            }
        }
    }
    return bestIndex;
}

AdaptiveConcurrencyController::AdaptiveConcurrencyController(AdaptiveConfig config,
                                                             uint32_t initialWorkers)
    : m_config(std::move(config)),
      m_initialWorkers(std::max<uint32_t>(1, initialWorkers)),
      m_lastAdjustment(std::chrono::steady_clock::time_point::min())
{
    if (m_config.minWorkers == 0) {
        m_config.minWorkers = 1;
    }
    if (m_config.consecutiveDropWindows < 1) {
        m_config.consecutiveDropWindows = 1;
    }
    if (m_config.dropThreshold < 0.0) {
        m_config.dropThreshold = 0.0;
    }
}

std::optional<uint32_t> AdaptiveConcurrencyController::Observe(
    double currentSeedsPerSecond,
    uint32_t currentWorkers,
    std::chrono::steady_clock::time_point now)
{
    if (!m_config.enabled || currentWorkers <= m_config.minWorkers) {
        return std::nullopt;
    }
    if (!(currentSeedsPerSecond > 0.0)) {
        return std::nullopt;
    }

    m_peakSeedsPerSecond = std::max(m_peakSeedsPerSecond, currentSeedsPerSecond);
    if (m_peakSeedsPerSecond <= 0.0) {
        return std::nullopt;
    }

    if (!(m_stagePeakSeedsPerSecond > 0.0)) {
        m_stagePeakSeedsPerSecond = currentSeedsPerSecond;
        m_consecutiveDrops = 0;
        return std::nullopt;
    }

    m_stagePeakSeedsPerSecond = std::max(m_stagePeakSeedsPerSecond, currentSeedsPerSecond);
    const double triggerLine = m_stagePeakSeedsPerSecond * (1.0 - m_config.dropThreshold);
    if (currentSeedsPerSecond <= triggerLine) {
        ++m_consecutiveDrops;
    } else {
        m_consecutiveDrops = 0;
        return std::nullopt;
    }

    if (m_consecutiveDrops < m_config.consecutiveDropWindows) {
        return std::nullopt;
    }
    if (m_lastAdjustment != std::chrono::steady_clock::time_point::min() &&
        now - m_lastAdjustment < m_config.cooldown) {
        return std::nullopt;
    }

    m_consecutiveDrops = 0;
    m_lastAdjustment = now;
    ++m_reductionCount;
    m_stagePeakSeedsPerSecond = 0.0;
    const uint32_t nextWorkers = std::max<uint32_t>(m_config.minWorkers, currentWorkers - 1);
    if (nextWorkers >= currentWorkers) {
        return std::nullopt;
    }
    return nextWorkers;
}

CpuMode ParseCpuMode(const std::string &value)
{
    const auto lower = ToLower(value);
    if (lower == "turbo" || lower == "极速") {
        return CpuMode::Turbo;
    }
    if (lower == "custom" || lower == "自定义") {
        return CpuMode::Custom;
    }
    if (lower == "conservative" || lower == "保守") {
        return CpuMode::Conservative;
    }
    return CpuMode::Balanced;
}

PlacementMode ParsePlacementMode(const std::string &value)
{
    const auto lower = ToLower(value);
    if (lower == "none") {
        return PlacementMode::None;
    }
    if (lower == "strict") {
        return PlacementMode::Strict;
    }
    return PlacementMode::Preferred;
}

const char *ToString(CpuMode mode)
{
    switch (mode) {
    case CpuMode::Balanced:
        return "balanced";
    case CpuMode::Turbo:
        return "turbo";
    case CpuMode::Custom:
        return "custom";
    case CpuMode::Conservative:
        return "conservative";
    default:
        return "balanced";
    }
}

const char *ToString(PlacementMode mode)
{
    switch (mode) {
    case PlacementMode::None:
        return "none";
    case PlacementMode::Preferred:
        return "preferred";
    case PlacementMode::Strict:
        return "strict";
    default:
        return "preferred";
    }
}

std::string JoinLogicalList(const std::vector<uint32_t> &values, size_t maxItems)
{
    if (values.empty()) {
        return "(none)";
    }
    std::ostringstream oss;
    const size_t displayCount = std::min(maxItems, values.size());
    for (size_t i = 0; i < displayCount; ++i) {
        if (i > 0) {
            oss << ",";
        }
        oss << values[i];
    }
    if (displayCount < values.size()) {
        oss << ",...+" << (values.size() - displayCount);
    }
    return oss.str();
}

bool ApplyThreadPlacement(const ThreadPolicy &policy,
                          uint32_t workerIndex,
                          std::string *errorMessage)
{
    if (policy.placement == PlacementMode::None ||
        policy.targetLogicalProcessors.empty()) {
        return true;
    }
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
    const uint32_t logicalIndex = policy.targetLogicalProcessors[
        workerIndex % policy.targetLogicalProcessors.size()];
    if (logicalIndex >= sizeof(DWORD_PTR) * 8) {
        if (errorMessage != nullptr) {
            *errorMessage = "logical processor index exceeds affinity mask width";
        }
        return false;
    }

    HANDLE threadHandle = GetCurrentThread();
    if (policy.placement == PlacementMode::Strict) {
        const DWORD_PTR mask = (DWORD_PTR)1 << logicalIndex;
        const DWORD_PTR prevMask = SetThreadAffinityMask(threadHandle, mask);
        if (prevMask == 0) {
            if (errorMessage != nullptr) {
                *errorMessage = "SetThreadAffinityMask failed";
            }
            return false;
        }
        return true;
    }

    SetLastError(0);
    const DWORD previous = SetThreadIdealProcessor(threadHandle, logicalIndex);
    if (previous == MAXDWORD && GetLastError() != 0) {
        if (errorMessage != nullptr) {
            *errorMessage = "SetThreadIdealProcessor failed";
        }
        return false;
    }
    return true;
#else
    (void)workerIndex;
    if (errorMessage != nullptr) {
        *errorMessage = "thread placement unsupported on current platform";
    }
    return false;
#endif
}

} // namespace BatchCpu
