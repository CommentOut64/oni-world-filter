#include "BatchCpu/CpuOptimization.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
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

std::vector<uint32_t> BuildAllLogicalIndices(const CpuTopology &topology);

CpuTopologyFacts BuildFallbackFacts(const std::string &reason)
{
    CpuTopologyFacts fallback;
    fallback.detectionSucceeded = false;
    fallback.usedFallback = true;
    fallback.isHeterogeneous = false;
    const uint32_t logicalThreadCount = SafeHardwareConcurrency();
    const uint32_t physicalCoreCount = std::max<uint32_t>(1, logicalThreadCount / 2);
    uint32_t nextLogicalIndex = 0;

    fallback.processorGroups.push_back(0);
    fallback.numaNodes.push_back(0);
    fallback.physicalCoresBySystemOrder.reserve(physicalCoreCount);

    for (uint32_t coreIndex = 0; coreIndex < physicalCoreCount; ++coreIndex) {
        PhysicalCoreFacts core;
        core.physicalCoreIndex = coreIndex;
        core.group = 0;
        core.coreIndex = static_cast<uint16_t>(coreIndex);
        core.numaNodeIndex = 0;
        core.efficiencyClass = 0;
        core.isHighPerformance = true;

        LogicalThreadFacts primary;
        primary.logicalIndex = nextLogicalIndex++;
        primary.group = 0;
        primary.coreIndex = core.coreIndex;
        primary.numaNodeIndex = 0;
        primary.efficiencyClass = 0;
        primary.isPrimaryThread = true;
        core.logicalThreads.push_back(primary);

        if (nextLogicalIndex < logicalThreadCount) {
            LogicalThreadFacts sibling;
            sibling.logicalIndex = nextLogicalIndex++;
            sibling.group = 0;
            sibling.coreIndex = core.coreIndex;
            sibling.numaNodeIndex = 0;
            sibling.efficiencyClass = 0;
            sibling.isPrimaryThread = false;
            core.logicalThreads.push_back(sibling);
        }

        fallback.physicalCoresBySystemOrder.push_back(std::move(core));
    }

    fallback.diagnostics = "fallback topology: " + reason;
    return fallback;
}

CpuTopology BuildLegacyTopologyFromFacts(const CpuTopologyFacts &facts)
{
    CpuTopology topology;
    topology.detectionSucceeded = facts.detectionSucceeded;
    topology.usedFallback = facts.usedFallback;
    topology.isHeterogeneous = facts.isHeterogeneous;
    topology.physicalCoreCount = static_cast<uint32_t>(facts.physicalCoresBySystemOrder.size());
    topology.processorGroups = facts.processorGroups;
    topology.numaNodes = facts.numaNodes;
    topology.diagnostics = facts.diagnostics;

    for (const auto &core : facts.physicalCoresBySystemOrder) {
        uint32_t preferredLogicalIndex = 0;
        bool hasPreferredLogicalIndex = false;
        for (const auto &thread : core.logicalThreads) {
            LogicalProcessorInfo info;
            info.logicalIndex = thread.logicalIndex;
            info.group = thread.group;
            info.coreIndex = thread.coreIndex;
            info.numaNodeIndex = thread.numaNodeIndex;
            info.efficiencyClass = thread.efficiencyClass;
            info.parked = thread.parked;
            info.allocated = thread.allocated;
            topology.logicalProcessors.push_back(info);
            if (core.isHighPerformance) {
                topology.highPerfLogicalIndices.push_back(thread.logicalIndex);
            } else {
                topology.lowPerfLogicalIndices.push_back(thread.logicalIndex);
            }
            if (!hasPreferredLogicalIndex || thread.isPrimaryThread) {
                preferredLogicalIndex = thread.logicalIndex;
                hasPreferredLogicalIndex = true;
            }
        }
        if (hasPreferredLogicalIndex) {
            topology.physicalPreferredLogicalIndices.push_back(preferredLogicalIndex);
        }
    }

    topology.logicalThreadCount = static_cast<uint32_t>(topology.logicalProcessors.size());
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
    topology.highPerfLogicalIndices = UniqueSorted(std::move(topology.highPerfLogicalIndices));
    topology.lowPerfLogicalIndices = UniqueSorted(std::move(topology.lowPerfLogicalIndices));
    topology.physicalPreferredLogicalIndices = UniqueSorted(std::move(topology.physicalPreferredLogicalIndices));
    if (topology.highPerfLogicalIndices.empty()) {
        topology.highPerfLogicalIndices = BuildAllLogicalIndices(topology);
    }
    return topology;
}

CpuTopology BuildFallbackTopology(const std::string &reason)
{
    return BuildLegacyTopologyFromFacts(BuildFallbackFacts(reason));
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

std::optional<ThreadBindingTarget> BuildThreadBindingTarget(const WorkerBindingSlot &slot)
{
    ThreadBindingTarget target;
    target.group = slot.group;
    target.logicalIndex = slot.logicalIndex;
    target.coreIndex = slot.coreIndex;
    target.numaNodeIndex = slot.numaNodeIndex;
    return target;
}

bool ApplyThreadBindingTarget(const ThreadBindingTarget &target,
                              PlacementMode placement,
                              std::string *errorMessage)
{
    if (placement == PlacementMode::None) {
        return true;
    }
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
    using SetThreadGroupAffinityFn = BOOL (WINAPI *)(HANDLE, const GROUP_AFFINITY *, PGROUP_AFFINITY);
    using SetThreadIdealProcessorExFn = BOOL (WINAPI *)(HANDLE, PPROCESSOR_NUMBER, PPROCESSOR_NUMBER);

    auto *kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (kernel32 == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "GetModuleHandleW(kernel32.dll) failed";
        }
        return false;
    }

    HANDLE threadHandle = GetCurrentThread();
    if (placement == PlacementMode::Strict) {
        if (target.logicalIndex >= sizeof(KAFFINITY) * 8) {
            if (errorMessage != nullptr) {
                *errorMessage = "logical processor index exceeds group affinity mask width";
            }
            return false;
        }

        auto rawProc = GetProcAddress(kernel32, "SetThreadGroupAffinity");
        SetThreadGroupAffinityFn setThreadGroupAffinity = nullptr;
        static_assert(sizeof(rawProc) == sizeof(setThreadGroupAffinity));
        std::memcpy(&setThreadGroupAffinity, &rawProc, sizeof(setThreadGroupAffinity));
        if (setThreadGroupAffinity == nullptr) {
            if (errorMessage != nullptr) {
                *errorMessage = "SetThreadGroupAffinity unavailable";
            }
            return false;
        }

        GROUP_AFFINITY affinity{};
        affinity.Group = target.group;
        affinity.Mask = ((KAFFINITY)1) << target.logicalIndex;
        GROUP_AFFINITY previous{};
        if (setThreadGroupAffinity(threadHandle, &affinity, &previous) == FALSE) {
            if (errorMessage != nullptr) {
                *errorMessage = "SetThreadGroupAffinity failed";
            }
            return false;
        }
        return true;
    }

    if (target.logicalIndex > std::numeric_limits<BYTE>::max()) {
        if (errorMessage != nullptr) {
            *errorMessage = "logical processor index exceeds PROCESSOR_NUMBER range";
        }
        return false;
    }

    auto rawProc = GetProcAddress(kernel32, "SetThreadIdealProcessorEx");
    SetThreadIdealProcessorExFn setThreadIdealProcessorEx = nullptr;
    static_assert(sizeof(rawProc) == sizeof(setThreadIdealProcessorEx));
    std::memcpy(&setThreadIdealProcessorEx, &rawProc, sizeof(setThreadIdealProcessorEx));
    if (setThreadIdealProcessorEx == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "SetThreadIdealProcessorEx unavailable";
        }
        return false;
    }

    PROCESSOR_NUMBER idealProcessor{};
    idealProcessor.Group = target.group;
    idealProcessor.Number = static_cast<BYTE>(target.logicalIndex);
    PROCESSOR_NUMBER previous{};
    if (setThreadIdealProcessorEx(threadHandle, &idealProcessor, &previous) == FALSE) {
        if (errorMessage != nullptr) {
            *errorMessage = "SetThreadIdealProcessorEx failed";
        }
        return false;
    }
    return true;
#else
    (void)target;
    if (errorMessage != nullptr) {
        *errorMessage = "thread placement unsupported on current platform";
    }
    return false;
#endif
}

} // namespace

CpuTopologyFacts CpuTopologyDetector::DetectFacts()
{
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
    using GetSystemCpuSetInformationFn = BOOL (WINAPI *)(
        PSYSTEM_CPU_SET_INFORMATION, ULONG, PULONG, HANDLE, ULONG);

    auto *kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (kernel32 == nullptr) {
        return BuildFallbackFacts("GetModuleHandleW(kernel32.dll) failed");
    }

    auto rawProc = GetProcAddress(kernel32, "GetSystemCpuSetInformation");
    GetSystemCpuSetInformationFn getCpuSetInfo = nullptr;
    static_assert(sizeof(rawProc) == sizeof(getCpuSetInfo));
    std::memcpy(&getCpuSetInfo, &rawProc, sizeof(getCpuSetInfo));
    if (getCpuSetInfo == nullptr) {
        return BuildFallbackFacts("GetSystemCpuSetInformation unavailable");
    }

    ULONG bytesNeeded = 0;
    SetLastError(0);
    const BOOL probeOk = getCpuSetInfo(nullptr, 0, &bytesNeeded, nullptr, 0);
    if (probeOk != FALSE || bytesNeeded == 0) {
        return BuildFallbackFacts("failed to probe cpu set buffer size");
    }
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        return BuildFallbackFacts("unexpected GetSystemCpuSetInformation probe status");
    }

    std::vector<unsigned char> buffer(bytesNeeded);
    if (getCpuSetInfo(reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buffer.data()),
                      bytesNeeded,
                      &bytesNeeded,
                      nullptr,
                      0) == FALSE) {
        return BuildFallbackFacts("GetSystemCpuSetInformation call failed");
    }

    std::vector<LogicalProcessorInfo> logicalProcessors;

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
            logicalProcessors.push_back(info);
            groups.insert(info.group);
            numaNodes.insert(info.numaNodeIndex);
            efficiencyClasses.insert(info.efficiencyClass);
        }
        offset += raw->Size;
    }

    if (logicalProcessors.empty()) {
        return BuildFallbackFacts("cpu set list empty");
    }

    std::sort(logicalProcessors.begin(), logicalProcessors.end(),
              [](const LogicalProcessorInfo &a, const LogicalProcessorInfo &b) {
                  if (a.group != b.group) {
                      return a.group < b.group;
                  }
                  if (a.coreIndex != b.coreIndex) {
                      return a.coreIndex < b.coreIndex;
                  }
                  if (a.logicalIndex != b.logicalIndex) {
                      return a.logicalIndex < b.logicalIndex;
                  }
                  return a.numaNodeIndex < b.numaNodeIndex;
              });

    CpuTopologyFacts topology;
    topology.detectionSucceeded = true;
    topology.usedFallback = false;
    topology.isHeterogeneous = efficiencyClasses.size() > 1;
    topology.processorGroups.assign(groups.begin(), groups.end());
    topology.numaNodes.assign(numaNodes.begin(), numaNodes.end());

    const uint8_t minEfficiencyClass = efficiencyClasses.empty() ? 0 : *efficiencyClasses.begin();
    std::map<std::tuple<uint16_t, uint16_t>, size_t> physicalCoreLookup;
    for (const auto &lp : logicalProcessors) {
        const auto key = std::tuple<uint16_t, uint16_t>{lp.group, lp.coreIndex};
        auto lookup = physicalCoreLookup.find(key);
        if (lookup == physicalCoreLookup.end()) {
            PhysicalCoreFacts core;
            core.physicalCoreIndex = static_cast<uint32_t>(topology.physicalCoresBySystemOrder.size());
            core.group = lp.group;
            core.coreIndex = lp.coreIndex;
            core.numaNodeIndex = lp.numaNodeIndex;
            core.efficiencyClass = lp.efficiencyClass;
            core.isHighPerformance = !topology.isHeterogeneous ||
                lp.efficiencyClass == minEfficiencyClass;
            topology.physicalCoresBySystemOrder.push_back(core);
            lookup = physicalCoreLookup.emplace(key, topology.physicalCoresBySystemOrder.size() - 1).first;
        }

        auto &core = topology.physicalCoresBySystemOrder[lookup->second];
        LogicalThreadFacts thread;
        thread.logicalIndex = lp.logicalIndex;
        thread.group = lp.group;
        thread.coreIndex = lp.coreIndex;
        thread.numaNodeIndex = lp.numaNodeIndex;
        thread.efficiencyClass = lp.efficiencyClass;
        thread.parked = lp.parked;
        thread.allocated = lp.allocated;
        thread.isPrimaryThread = core.logicalThreads.empty();
        core.logicalThreads.push_back(thread);
    }

    uint32_t highPerfCoreCount = 0;
    for (const auto &core : topology.physicalCoresBySystemOrder) {
        if (core.isHighPerformance) {
            ++highPerfCoreCount;
        }
    }
    std::ostringstream summary;
    summary << "cpu topology: logical=" << logicalProcessors.size()
            << ", physical=" << topology.physicalCoresBySystemOrder.size()
            << ", groups=" << topology.processorGroups.size()
            << ", numa=" << topology.numaNodes.size()
            << ", hetero=" << (topology.isHeterogeneous ? "yes" : "no")
            << ", high_perf=" << highPerfCoreCount
            << ", low_perf=" << (topology.physicalCoresBySystemOrder.size() - highPerfCoreCount);
    topology.diagnostics = summary.str();

    return topology;
#else
    return BuildFallbackFacts("non-windows build");
#endif
}

CpuTopology CpuTopologyDetector::Detect()
{
    return BuildLegacyTopologyFromFacts(DetectFacts());
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
        if (input.customAllowLowPerf && !lowPhysical.empty() && !pOnly.empty()) {
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

BoundedRecoveryController::BoundedRecoveryController(RecoveryConfig config,
                                                     uint32_t initialWorkers)
    : m_config(std::move(config)),
      m_initialWorkers(std::max<uint32_t>(1, initialWorkers)),
      m_lastObservedWorkers(m_initialWorkers),
      m_lastAdjustment(std::chrono::steady_clock::time_point::min())
{
    if (m_config.stableWindows < 1) {
        m_config.stableWindows = 1;
    }
    m_config.retentionRatio = std::clamp(m_config.retentionRatio, 0.0, 1.0);
}

std::optional<uint32_t> BoundedRecoveryController::Observe(
    double currentSeedsPerSecond,
    uint32_t currentWorkers,
    std::chrono::steady_clock::time_point now)
{
    if (!m_config.enabled || currentWorkers >= m_initialWorkers) {
        return std::nullopt;
    }
    if (!(currentSeedsPerSecond > 0.0)) {
        return std::nullopt;
    }

    if (currentWorkers != m_lastObservedWorkers) {
        m_lastObservedWorkers = currentWorkers;
        m_stageBaselineSeedsPerSecond = currentSeedsPerSecond;
        m_consecutiveStableWindows = 0;
        m_lastAdjustment = now;
        return std::nullopt;
    }

    if (!(m_stageBaselineSeedsPerSecond > 0.0)) {
        m_stageBaselineSeedsPerSecond = currentSeedsPerSecond;
        m_consecutiveStableWindows = 0;
        return std::nullopt;
    }

    const double retentionLine = m_stageBaselineSeedsPerSecond * m_config.retentionRatio;
    if (currentSeedsPerSecond >= retentionLine) {
        ++m_consecutiveStableWindows;
    } else {
        m_consecutiveStableWindows = 0;
    }

    if (m_consecutiveStableWindows < m_config.stableWindows) {
        return std::nullopt;
    }
    if (m_lastAdjustment != std::chrono::steady_clock::time_point::min() &&
        now - m_lastAdjustment < m_config.cooldown) {
        return std::nullopt;
    }

    const uint32_t nextWorkers = std::min<uint32_t>(m_initialWorkers, currentWorkers + 1);
    if (nextWorkers <= currentWorkers) {
        return std::nullopt;
    }

    m_stageBaselineSeedsPerSecond = 0.0;
    m_consecutiveStableWindows = 0;
    m_lastAdjustment = now;
    m_lastObservedWorkers = nextWorkers;
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

std::optional<ThreadBindingTarget> ResolveThreadBindingTarget(const CpuPlacementPlan &plan,
                                                              uint32_t workerIndex)
{
    const auto slot = ResolveWorkerBindingSlot(plan, workerIndex);
    if (!slot.has_value()) {
        return std::nullopt;
    }
    return BuildThreadBindingTarget(*slot);
}

bool ApplyThreadPlacement(const WorkerBindingSlot &slot,
                          PlacementMode placement,
                          std::string *errorMessage)
{
    const auto target = BuildThreadBindingTarget(slot);
    if (!target.has_value()) {
        if (errorMessage != nullptr) {
            *errorMessage = "worker binding slot unavailable";
        }
        return false;
    }
    return ApplyThreadBindingTarget(*target, placement, errorMessage);
}

bool ApplyThreadPlacement(const CpuPlacementPlan &plan,
                          PlacementMode placement,
                          uint32_t workerIndex,
                          std::string *errorMessage)
{
    const auto slot = ResolveWorkerBindingSlot(plan, workerIndex);
    if (!slot.has_value()) {
        if (errorMessage != nullptr) {
            *errorMessage = "worker binding slot unavailable";
        }
        return false;
    }
    return ApplyThreadPlacement(*slot, placement, errorMessage);
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
