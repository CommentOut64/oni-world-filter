#include "BatchCpu/CpuOptimization.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <thread>
#include <tuple>

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

struct LogicalProcessorInfo {
    uint32_t logicalIndex = 0;
    uint16_t group = 0;
    uint16_t coreIndex = 0;
    uint16_t numaNodeIndex = 0;
    std::optional<uint32_t> cpuSetId;
    uint8_t efficiencyClass = 0;
    bool parked = false;
    bool allocated = false;
};

std::string ToLower(const std::string &value)
{
    std::string out = value;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

uint32_t SafeHardwareConcurrency()
{
    const auto hw = std::thread::hardware_concurrency();
    return hw > 0 ? hw : 4U;
}

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
        primary.cpuSetId = primary.logicalIndex;
        primary.efficiencyClass = 0;
        primary.isPrimaryThread = true;
        core.logicalThreads.push_back(primary);

        if (nextLogicalIndex < logicalThreadCount) {
            LogicalThreadFacts sibling;
            sibling.logicalIndex = nextLogicalIndex++;
            sibling.group = 0;
            sibling.coreIndex = core.coreIndex;
            sibling.numaNodeIndex = 0;
            sibling.cpuSetId = sibling.logicalIndex;
            sibling.efficiencyClass = 0;
            sibling.isPrimaryThread = false;
            core.logicalThreads.push_back(sibling);
        }

        fallback.physicalCoresBySystemOrder.push_back(std::move(core));
    }

    fallback.diagnostics = "fallback topology: " + reason;
    return fallback;
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
    using SetThreadGroupAffinityFn = BOOL(WINAPI *)(HANDLE, const GROUP_AFFINITY *, PGROUP_AFFINITY);
    using SetThreadIdealProcessorExFn = BOOL(WINAPI *)(HANDLE, PPROCESSOR_NUMBER, PPROCESSOR_NUMBER);

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
        affinity.Mask = (static_cast<KAFFINITY>(1) << target.logicalIndex);
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

bool ApplyThreadSelectedCpuSets(const std::vector<uint32_t> &cpuSetIds,
                                std::string *errorMessage)
{
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
    using SetThreadSelectedCpuSetsFn = BOOL(WINAPI *)(HANDLE, const ULONG *, ULONG);

    if (cpuSetIds.empty()) {
        return true;
    }

    auto *kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (kernel32 == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "GetModuleHandleW(kernel32.dll) failed";
        }
        return false;
    }

    auto rawProc = GetProcAddress(kernel32, "SetThreadSelectedCpuSets");
    SetThreadSelectedCpuSetsFn setThreadSelectedCpuSets = nullptr;
    static_assert(sizeof(rawProc) == sizeof(setThreadSelectedCpuSets));
    std::memcpy(&setThreadSelectedCpuSets, &rawProc, sizeof(setThreadSelectedCpuSets));
    if (setThreadSelectedCpuSets == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "SetThreadSelectedCpuSets unavailable";
        }
        return false;
    }

    std::vector<ULONG> rawCpuSetIds;
    rawCpuSetIds.reserve(cpuSetIds.size());
    for (const auto cpuSetId : cpuSetIds) {
        rawCpuSetIds.push_back(static_cast<ULONG>(cpuSetId));
    }

    if (setThreadSelectedCpuSets(GetCurrentThread(),
                                 rawCpuSetIds.data(),
                                 static_cast<ULONG>(rawCpuSetIds.size())) == FALSE) {
        if (errorMessage != nullptr) {
            *errorMessage = "SetThreadSelectedCpuSets failed";
        }
        return false;
    }
    return true;
#else
    (void)cpuSetIds;
    if (errorMessage != nullptr) {
        *errorMessage = "thread cpu set filtering unsupported on current platform";
    }
    return false;
#endif
}

} // namespace

std::optional<std::vector<uint32_t>> GetProcessDefaultCpuSetIds(std::string *errorMessage)
{
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
    using GetProcessDefaultCpuSetsFn = BOOL(WINAPI *)(HANDLE, PULONG, ULONG, PULONG);

    auto *kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (kernel32 == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "GetModuleHandleW(kernel32.dll) failed";
        }
        return std::nullopt;
    }

    auto rawProc = GetProcAddress(kernel32, "GetProcessDefaultCpuSets");
    GetProcessDefaultCpuSetsFn getProcessDefaultCpuSets = nullptr;
    static_assert(sizeof(rawProc) == sizeof(getProcessDefaultCpuSets));
    std::memcpy(&getProcessDefaultCpuSets, &rawProc, sizeof(getProcessDefaultCpuSets));
    if (getProcessDefaultCpuSets == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "GetProcessDefaultCpuSets unavailable";
        }
        return std::nullopt;
    }

    ULONG requiredCpuSetCount = 0;
    SetLastError(0);
    const BOOL probeOk =
        getProcessDefaultCpuSets(GetCurrentProcess(), nullptr, 0, &requiredCpuSetCount);
    if (probeOk == FALSE && GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        if (errorMessage != nullptr) {
            *errorMessage = "GetProcessDefaultCpuSets probe failed";
        }
        return std::nullopt;
    }

    if (requiredCpuSetCount == 0) {
        return std::vector<uint32_t>{};
    }

    std::vector<ULONG> rawCpuSetIds(requiredCpuSetCount);
    if (getProcessDefaultCpuSets(GetCurrentProcess(),
                                 rawCpuSetIds.data(),
                                 static_cast<ULONG>(rawCpuSetIds.size()),
                                 &requiredCpuSetCount) == FALSE) {
        if (errorMessage != nullptr) {
            *errorMessage = "GetProcessDefaultCpuSets failed";
        }
        return std::nullopt;
    }

    std::vector<uint32_t> cpuSetIds;
    cpuSetIds.reserve(requiredCpuSetCount);
    for (ULONG index = 0; index < requiredCpuSetCount; ++index) {
        cpuSetIds.push_back(static_cast<uint32_t>(rawCpuSetIds[index]));
    }
    return cpuSetIds;
#else
    if (errorMessage != nullptr) {
        *errorMessage = "process default cpu set query unsupported on current platform";
    }
    return std::nullopt;
#endif
}

bool SetProcessDefaultCpuSets(const std::vector<uint32_t> &cpuSetIds,
                              std::string *errorMessage)
{
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
    using SetProcessDefaultCpuSetsFn = BOOL(WINAPI *)(HANDLE, const ULONG *, ULONG);

    auto *kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (kernel32 == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "GetModuleHandleW(kernel32.dll) failed";
        }
        return false;
    }

    auto rawProc = GetProcAddress(kernel32, "SetProcessDefaultCpuSets");
    SetProcessDefaultCpuSetsFn setProcessDefaultCpuSets = nullptr;
    static_assert(sizeof(rawProc) == sizeof(setProcessDefaultCpuSets));
    std::memcpy(&setProcessDefaultCpuSets, &rawProc, sizeof(setProcessDefaultCpuSets));
    if (setProcessDefaultCpuSets == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "SetProcessDefaultCpuSets unavailable";
        }
        return false;
    }

    std::vector<ULONG> rawCpuSetIds;
    rawCpuSetIds.reserve(cpuSetIds.size());
    for (const auto cpuSetId : cpuSetIds) {
        rawCpuSetIds.push_back(static_cast<ULONG>(cpuSetId));
    }

    const ULONG *rawCpuSetPtr = rawCpuSetIds.empty() ? nullptr : rawCpuSetIds.data();
    if (setProcessDefaultCpuSets(GetCurrentProcess(),
                                 rawCpuSetPtr,
                                 static_cast<ULONG>(rawCpuSetIds.size())) == FALSE) {
        if (errorMessage != nullptr) {
            *errorMessage = "SetProcessDefaultCpuSets failed";
        }
        return false;
    }
    return true;
#else
    (void)cpuSetIds;
    if (errorMessage != nullptr) {
        *errorMessage = "process default cpu set filtering unsupported on current platform";
    }
    return false;
#endif
}

namespace {

bool ClassifyHighPerformanceEfficiencyClass(bool isHeterogeneous,
                                            uint8_t efficiencyClass,
                                            uint8_t highestEfficiencyClass)
{
    return !isHeterogeneous || efficiencyClass == highestEfficiencyClass;
}

} // namespace

bool IsHighPerformanceEfficiencyClass(bool isHeterogeneous,
                                      uint8_t efficiencyClass,
                                      uint8_t highestEfficiencyClass)
{
    return ClassifyHighPerformanceEfficiencyClass(
        isHeterogeneous,
        efficiencyClass,
        highestEfficiencyClass);
}

CpuTopologyFacts CpuTopologyDetector::DetectFacts()
{
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
    using GetSystemCpuSetInformationFn =
        BOOL(WINAPI *)(PSYSTEM_CPU_SET_INFORMATION, ULONG, PULONG, HANDLE, ULONG);

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
            info.cpuSetId = cpu.Id;
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

    const uint8_t highestEfficiencyClass =
        efficiencyClasses.empty() ? 0 : *efficiencyClasses.rbegin();
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
            core.isHighPerformance = ClassifyHighPerformanceEfficiencyClass(
                topology.isHeterogeneous,
                lp.efficiencyClass,
                highestEfficiencyClass);
            topology.physicalCoresBySystemOrder.push_back(core);
            lookup = physicalCoreLookup.emplace(key, topology.physicalCoresBySystemOrder.size() - 1).first;
        }

        auto &core = topology.physicalCoresBySystemOrder[lookup->second];
        LogicalThreadFacts thread;
        thread.logicalIndex = lp.logicalIndex;
        thread.group = lp.group;
        thread.coreIndex = lp.coreIndex;
        thread.numaNodeIndex = lp.numaNodeIndex;
        thread.cpuSetId = lp.cpuSetId;
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

CpuMode ParseCpuMode(const std::string &value)
{
    const auto lower = ToLower(value);
    if (lower == "turbo" || lower == "狂暴" || lower == "custom" || lower == "自定义") {
        return CpuMode::Turbo;
    }
    if (lower == "conservative" || lower == "保守") {
        return CpuMode::Balanced;
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
    return PlacementMode::Strict;
}

const char *ToString(CpuMode mode)
{
    switch (mode) {
    case CpuMode::Balanced:
        return "balanced";
    case CpuMode::Turbo:
        return "turbo";
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
        return "strict";
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

std::optional<uint32_t> ResolveWorkerCpuSetId(const CpuPlacementPlan &plan,
                                              uint32_t workerIndex)
{
    const auto slot = ResolveWorkerBindingSlot(plan, workerIndex);
    if (!slot.has_value()) {
        return std::nullopt;
    }
    return slot->cpuSetId;
}

std::vector<uint32_t> ResolveAllowedCpuSetIds(const CompiledSearchCpuPlan &plan)
{
    if (!plan.isHeterogeneous || plan.policy.allowLowPerf) {
        return {};
    }

    std::vector<uint32_t> cpuSetIds;
    std::set<uint32_t> seen;
    cpuSetIds.reserve(plan.placement.workerSlotsByPriority.size());
    for (const auto &slot : plan.placement.workerSlotsByPriority) {
        if (!slot.cpuSetId.has_value()) {
            continue;
        }
        if (seen.insert(slot.cpuSetId.value()).second) {
            cpuSetIds.push_back(slot.cpuSetId.value());
        }
    }
    return cpuSetIds;
}

std::optional<ThreadPlacementDirective> ResolveThreadPlacementDirective(
    const CompiledSearchCpuPlan &plan,
    uint32_t workerIndex)
{
    const auto slot = ResolveWorkerBindingSlot(plan.placement, workerIndex);
    if (!slot.has_value()) {
        return std::nullopt;
    }

    ThreadPlacementDirective directive;
    directive.bindingTarget = BuildThreadBindingTarget(*slot);

    const auto allowedCpuSetIds = ResolveAllowedCpuSetIds(plan);
    if (!allowedCpuSetIds.empty()) {
        if (plan.policy.binding == PlacementMode::Strict && slot->cpuSetId.has_value()) {
            directive.selectedCpuSetIds.push_back(slot->cpuSetId.value());
        } else {
            directive.selectedCpuSetIds = allowedCpuSetIds;
        }
    }

    return directive;
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

bool ApplyThreadPlacement(const CompiledSearchCpuPlan &plan,
                          uint32_t workerIndex,
                          std::string *errorMessage)
{
    const auto directive = ResolveThreadPlacementDirective(plan, workerIndex);
    if (!directive.has_value()) {
        if (errorMessage != nullptr) {
            *errorMessage = "worker binding slot unavailable";
        }
        return false;
    }

    if (!directive->selectedCpuSetIds.empty()) {
        if (!ApplyThreadSelectedCpuSets(directive->selectedCpuSetIds, errorMessage)) {
            return false;
        }
    }

    if (!directive->bindingTarget.has_value()) {
        if (errorMessage != nullptr) {
            *errorMessage = "worker binding target unavailable";
        }
        return false;
    }

    return ApplyThreadBindingTarget(*directive->bindingTarget, plan.policy.binding, errorMessage);
}

} // namespace BatchCpu
