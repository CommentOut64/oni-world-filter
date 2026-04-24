#include "SearchAnalysis/ExplainabilityBuilder.hpp"

#include <iomanip>
#include <sstream>
#include <utility>

namespace SearchAnalysis {

namespace {

std::string JoinGroups(const std::vector<std::string> &groups)
{
    if (groups.empty()) {
        return "无";
    }
    std::ostringstream stream;
    for (size_t i = 0; i < groups.size(); ++i) {
        if (i != 0) {
            stream << ", ";
        }
        stream << groups[i];
    }
    return stream.str();
}

std::string FormatProbability(double probability)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(6) << probability;
    return stream.str();
}

} // namespace

ValidationIssue BuildLowProbabilityWarning(double probabilityUpper,
                                           const std::vector<std::string> &bottlenecks,
                                           bool strongWarning,
                                           bool hasLowConfidenceDistance)
{
    const std::string severity = strongWarning ? "strong-warning" : "warning";
    std::string message = "乐观估计可匹配概率=" + FormatProbability(probabilityUpper) +
                          "，主要瓶颈: " + JoinGroups(bottlenecks) +
                          "，建议先放宽这些条件。";
    if (hasLowConfidenceDistance && strongWarning) {
        message += " 距离项为低置信估计，已降级为 warning。";
    }

    return ValidationIssue{
        .layer = "layer4",
        .code = "predict.low_probability." + severity,
        .field = "constraints",
        .message = std::move(message),
    };
}

ValidationIssue BuildGenericCapacityPrunedWarning(const std::vector<std::string> &groups,
                                                  int demandedSlots,
                                                  int capacityUpper)
{
    return ValidationIssue{
        .layer = "layer4",
        .code = "predict.generic_capacity_pruned",
        .field = "constraints.count",
        .message = "共享 generic 槽位容量不足，需求 " + std::to_string(demandedSlots) +
                   " > 上界 " + std::to_string(capacityUpper) +
                   "，相关组: " + JoinGroups(groups),
    };
}

ValidationIssue BuildDependencyFallbackWarning(const std::vector<std::string> &groups)
{
    return ValidationIssue{
        .layer = "layer4",
        .code = "predict.dependency_fallback_min",
        .field = "constraints",
        .message = "瓶颈组存在共享依赖但联合分布尚未完全解析，已回退 min 上界。相关组: " +
                   JoinGroups(groups),
    };
}

} // namespace SearchAnalysis
