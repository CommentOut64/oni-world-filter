#include "SearchAnalysis/SearchConstraintNormalizer.hpp"

#include <algorithm>
#include <unordered_map>

#include "Batch/FilterConfig.hpp"

namespace SearchAnalysis {

namespace {

ConstraintGroup &GetOrCreateGroup(std::vector<ConstraintGroup> *groups,
                                  std::unordered_map<std::string, size_t> *indexById,
                                  const std::string &geyserId)
{
    const auto itr = indexById->find(geyserId);
    if (itr != indexById->end()) {
        return (*groups)[itr->second];
    }

    const size_t index = groups->size();
    indexById->emplace(geyserId, index);
    groups->push_back(ConstraintGroup{});
    auto &group = groups->back();
    group.geyserId = geyserId;
    group.geyserIndex = Batch::GeyserIdToIndex(geyserId);
    return group;
}

} // namespace

NormalizedSearchRequest NormalizeSearchRequest(const SearchAnalysisRequest &request)
{
    NormalizedSearchRequest normalized;
    normalized.worldType = request.worldType;
    normalized.seedStart = request.seedStart;
    normalized.seedEnd = request.seedEnd;
    normalized.mixing = request.mixing;
    normalized.cpu = request.cpu;

    std::unordered_map<std::string, size_t> indexById;
    indexById.reserve(request.constraints.required.size() +
                      request.constraints.forbidden.size() +
                      request.constraints.distance.size() +
                      request.constraints.count.size());

    for (const auto &geyserId : request.constraints.required) {
        auto &group = GetOrCreateGroup(&normalized.groups, &indexById, geyserId);
        group.hasRequired = true;
        group.minCount = std::max(group.minCount, 1);
    }

    for (const auto &geyserId : request.constraints.forbidden) {
        auto &group = GetOrCreateGroup(&normalized.groups, &indexById, geyserId);
        group.hasForbidden = true;
        group.maxCount = std::min(group.maxCount, 0);
    }

    for (const auto &rule : request.constraints.count) {
        auto &group = GetOrCreateGroup(&normalized.groups, &indexById, rule.geyserId);
        group.hasExplicitCount = true;
        group.minCount = std::max(group.minCount, rule.minCount);
        group.maxCount = std::min(group.maxCount, rule.maxCount);
    }

    for (const auto &rule : request.constraints.distance) {
        auto &group = GetOrCreateGroup(&normalized.groups, &indexById, rule.geyserId);
        group.distanceRules.push_back(rule);
    }

    for (auto &group : normalized.groups) {
        // distance 语义与 BatchMatcher 保持一致: 只要设置距离规则，就至少要存在一个对应喷口。
        if (!group.distanceRules.empty() && !group.hasForbidden && group.maxCount > 0) {
            group.minCount = std::max(group.minCount, 1);
        }
    }

    return normalized;
}

} // namespace SearchAnalysis
