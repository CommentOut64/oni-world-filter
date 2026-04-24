#include "SearchAnalysis/BottleneckSelectivityPredictor.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

#include "SearchAnalysis/ExplainabilityBuilder.hpp"
#include "SearchAnalysis/PoissonBinomial.hpp"

namespace SearchAnalysis {

namespace {

constexpr double kProbabilityEpsilon = 1e-12;
constexpr int kTopBottlenecks = 3;

struct GroupPrediction {
    std::string geyserId;
    int minCount = 0;
    int maxCount = 0;
    int exactUpper = 0;
    int genericUpper = 0;
    double genericDistanceUpper = 0.0;
    double genericTypeUpper = 0.0;
    double probabilityUpper = 1.0;
    double score = 0.0;
    bool lowConfidenceDistance = false;
    std::set<std::string> poolIds;
    std::set<std::string> envelopeIds;
    std::vector<double> qValues;
};

struct ComponentEvaluation {
    double probabilityUpper = 1.0;
    bool usedFallback = false;
    bool genericCapacityPruned = false;
    int demandedSlots = 0;
    int capacityUpper = 0;
    std::vector<std::string> groups;
};

double ClampProbability(double value)
{
    if (value < 0.0) {
        return 0.0;
    }
    if (value > 1.0) {
        return 1.0;
    }
    return value;
}

double ComputeDistanceUpper(const ConstraintGroup &group,
                            const std::string &envelopeId,
                            const WorldEnvelopeProfile &profile,
                            bool *lowConfidenceDistance)
{
    if (lowConfidenceDistance != nullptr) {
        *lowConfidenceDistance = false;
    }
    if (group.distanceRules.empty()) {
        return 1.0;
    }

    const auto envelopeItr = profile.envelopeStatsById.find(envelopeId);
    if (envelopeId.empty() || envelopeItr == profile.envelopeStatsById.end()) {
        if (lowConfidenceDistance != nullptr) {
            *lowConfidenceDistance = true;
        }
        return 1.0;
    }

    const auto &stats = envelopeItr->second;
    if (stats.candidateCount <= 0 || stats.candidateDistances.empty()) {
        if (lowConfidenceDistance != nullptr) {
            *lowConfidenceDistance = true;
        }
        return 1.0;
    }

    double upper = 1.0;
    for (const auto &rule : group.distanceRules) {
        double minDist = std::max(0.0, rule.minDist);
        double maxDist = std::max(minDist, rule.maxDist);
        int matchedCount = 0;
        for (const double candidateDistance : stats.candidateDistances) {
            if (candidateDistance + kProbabilityEpsilon < minDist) {
                continue;
            }
            if (candidateDistance - kProbabilityEpsilon > maxDist) {
                continue;
            }
            ++matchedCount;
        }
        const double ratio =
            ClampProbability(static_cast<double>(matchedCount) /
                             static_cast<double>(stats.candidateCount));
        upper = std::min(upper, ratio);
    }
    return ClampProbability(upper);
}

int ResolveGroupMaxCount(const ConstraintGroup &group, int opportunityCount)
{
    if (group.maxCount >= std::numeric_limits<int>::max()) {
        return opportunityCount;
    }
    return std::max(0, std::min(group.maxCount, opportunityCount));
}

int ResolveGenericCapacity(const WorldEnvelopeProfile &profile)
{
    for (const auto &pool : profile.sourcePools) {
        if (pool.poolId == "generic") {
            return std::max(0, pool.capacityUpper);
        }
    }
    return std::max(0, profile.genericSlotUpper);
}

GroupPrediction PredictGroup(const ConstraintGroup &group, const WorldEnvelopeProfile &profile)
{
    GroupPrediction prediction;
    prediction.geyserId = group.geyserId;
    prediction.minCount = std::max(0, group.minCount);

    const auto genericUpperItr = profile.genericTypeUpperById.find(group.geyserId);
    if (genericUpperItr != profile.genericTypeUpperById.end()) {
        prediction.genericTypeUpper = ClampProbability(genericUpperItr->second);
    }

    for (const auto &source : profile.exactSourceSummary) {
        if (source.geyserId != group.geyserId) {
            continue;
        }
        const int upper = std::max(0, source.upperBound);
        prediction.exactUpper += upper;
        prediction.poolIds.insert(source.poolId);
        if (!source.envelopeId.empty()) {
            prediction.envelopeIds.insert(source.envelopeId);
        }
        bool lowConfidenceDistance = false;
        const double sourceDistanceUpper =
            ComputeDistanceUpper(group, source.envelopeId, profile, &lowConfidenceDistance);
        prediction.lowConfidenceDistance =
            prediction.lowConfidenceDistance || lowConfidenceDistance;
        const double q = ClampProbability(sourceDistanceUpper);
        for (int i = 0; i < upper; ++i) {
            prediction.qValues.push_back(q);
        }
    }

    for (const auto &source : profile.genericSourceSummary) {
        const int upper = std::max(0, source.upperBound);
        if (upper <= 0) {
            continue;
        }
        prediction.genericUpper += upper;
        prediction.poolIds.insert(source.poolId);
        if (!source.envelopeId.empty()) {
            prediction.envelopeIds.insert(source.envelopeId);
        }
        bool lowConfidenceDistance = false;
        const double sourceDistanceUpper =
            ComputeDistanceUpper(group, source.envelopeId, profile, &lowConfidenceDistance);
        prediction.lowConfidenceDistance =
            prediction.lowConfidenceDistance || lowConfidenceDistance;
        prediction.genericDistanceUpper =
            std::max(prediction.genericDistanceUpper, sourceDistanceUpper);
        const double q =
            ClampProbability(prediction.genericTypeUpper * sourceDistanceUpper);
        for (int i = 0; i < upper; ++i) {
            prediction.qValues.push_back(q);
        }
    }

    prediction.maxCount = ResolveGroupMaxCount(group, static_cast<int>(prediction.qValues.size()));
    if (prediction.qValues.empty()) {
        prediction.probabilityUpper =
            (prediction.minCount <= 0 && prediction.maxCount >= 0) ? 1.0 : 0.0;
    } else {
        prediction.probabilityUpper = ComputePoissonBinomialRangeProbability(prediction.qValues,
                                                                             prediction.minCount,
                                                                             prediction.maxCount);
    }

    prediction.probabilityUpper = ClampProbability(prediction.probabilityUpper);
    prediction.score = -std::log10(std::max(prediction.probabilityUpper, kProbabilityEpsilon));
    return prediction;
}

bool SharePool(const GroupPrediction &left, const GroupPrediction &right)
{
    for (const auto &pool : left.poolIds) {
        if (right.poolIds.find(pool) != right.poolIds.end()) {
            return true;
        }
    }
    return false;
}

bool ShareEnvelope(const GroupPrediction &left, const GroupPrediction &right)
{
    for (const auto &envelopeId : left.envelopeIds) {
        if (right.envelopeIds.find(envelopeId) != right.envelopeIds.end()) {
            return true;
        }
    }
    return false;
}

std::vector<std::vector<int>> BuildConnectedComponents(const std::vector<int> &selected,
                                                       const std::vector<GroupPrediction> &groups)
{
    if (selected.empty()) {
        return {};
    }

    std::vector<int> parent(selected.size());
    for (size_t i = 0; i < selected.size(); ++i) {
        parent[i] = static_cast<int>(i);
    }

    auto findRoot = [&](int node) {
        int root = node;
        while (parent[root] != root) {
            root = parent[root];
        }
        while (parent[node] != node) {
            const int next = parent[node];
            parent[node] = root;
            node = next;
        }
        return root;
    };

    auto merge = [&](int a, int b) {
        const int ra = findRoot(a);
        const int rb = findRoot(b);
        if (ra != rb) {
            parent[rb] = ra;
        }
    };

    for (size_t i = 0; i < selected.size(); ++i) {
        for (size_t j = i + 1; j < selected.size(); ++j) {
            const auto &left = groups[static_cast<size_t>(selected[i])];
            const auto &right = groups[static_cast<size_t>(selected[j])];
            if (SharePool(left, right) || ShareEnvelope(left, right)) {
                merge(static_cast<int>(i), static_cast<int>(j));
            }
        }
    }

    std::unordered_map<int, std::vector<int>> buckets;
    for (size_t i = 0; i < selected.size(); ++i) {
        buckets[findRoot(static_cast<int>(i))].push_back(selected[i]);
    }

    std::vector<std::vector<int>> components;
    components.reserve(buckets.size());
    for (auto &[_, items] : buckets) {
        components.push_back(std::move(items));
    }
    return components;
}

bool IsSingleGenericComponent(const std::vector<int> &component,
                              const std::vector<GroupPrediction> &groups)
{
    std::map<std::string, int> sharedCounts;
    for (int index : component) {
        for (const auto &pool : groups[static_cast<size_t>(index)].poolIds) {
            sharedCounts[pool] += 1;
        }
    }

    std::set<std::string> sharedPools;
    for (const auto &[pool, count] : sharedCounts) {
        if (count > 1) {
            sharedPools.insert(pool);
        }
    }
    return sharedPools.size() == 1 && *sharedPools.begin() == "generic";
}

double ComputeSingleGenericJointProbability(const std::vector<int> &component,
                                            const std::vector<GroupPrediction> &groups,
                                            int capacity)
{
    const int m = static_cast<int>(component.size());
    std::vector<int> demands(static_cast<size_t>(m), 0);
    std::vector<double> categoryProbabilities(static_cast<size_t>(m), 0.0);

    for (int i = 0; i < m; ++i) {
        const auto &group = groups[static_cast<size_t>(component[static_cast<size_t>(i)])];
        demands[static_cast<size_t>(i)] = std::max(0, group.minCount - group.exactUpper);
        categoryProbabilities[static_cast<size_t>(i)] =
            ClampProbability(group.genericTypeUpper * group.genericDistanceUpper);
    }

    int totalDemand = 0;
    for (const int demand : demands) {
        totalDemand += demand;
    }
    if (totalDemand > capacity) {
        return 0.0;
    }
    if (capacity <= 0) {
        return totalDemand == 0 ? 1.0 : 0.0;
    }

    double probabilitySum = 0.0;
    for (const double probability : categoryProbabilities) {
        probabilitySum += probability;
    }
    if (probabilitySum > 1.0 && probabilitySum > 0.0) {
        for (double &probability : categoryProbabilities) {
            probability /= probabilitySum;
        }
        probabilitySum = 1.0;
    }
    const double otherProbability = ClampProbability(1.0 - probabilitySum);

    std::vector<int> zeroState(static_cast<size_t>(m), 0);
    std::map<std::vector<int>, double> states;
    states[zeroState] = 1.0;

    for (int slot = 0; slot < capacity; ++slot) {
        std::map<std::vector<int>, double> nextStates;
        for (const auto &[state, probability] : states) {
            if (probability <= 0.0) {
                continue;
            }

            if (otherProbability > 0.0) {
                nextStates[state] += probability * otherProbability;
            }

            for (int i = 0; i < m; ++i) {
                const double p = categoryProbabilities[static_cast<size_t>(i)];
                if (p <= 0.0) {
                    continue;
                }
                auto advanced = state;
                advanced[static_cast<size_t>(i)] = std::min(
                    demands[static_cast<size_t>(i)],
                    advanced[static_cast<size_t>(i)] + 1);
                nextStates[advanced] += probability * p;
            }
        }
        states = std::move(nextStates);
    }

    const auto itr = states.find(demands);
    if (itr == states.end()) {
        return 0.0;
    }
    return ClampProbability(itr->second);
}

ComponentEvaluation EvaluateComponent(const std::vector<int> &component,
                                      const std::vector<GroupPrediction> &groups,
                                      const WorldEnvelopeProfile &profile)
{
    ComponentEvaluation evaluation;
    for (int index : component) {
        evaluation.groups.push_back(groups[static_cast<size_t>(index)].geyserId);
    }

    if (component.empty()) {
        return evaluation;
    }

    double minGroupUpper = 1.0;
    for (int index : component) {
        minGroupUpper = std::min(minGroupUpper,
                                 groups[static_cast<size_t>(index)].probabilityUpper);
    }

    if (component.size() == 1) {
        evaluation.probabilityUpper = minGroupUpper;
        return evaluation;
    }

    if (IsSingleGenericComponent(component, groups)) {
        const int capacity = ResolveGenericCapacity(profile);
        int totalDemand = 0;
        for (int index : component) {
            const auto &group = groups[static_cast<size_t>(index)];
            totalDemand += std::max(0, group.minCount - group.exactUpper);
        }

        evaluation.capacityUpper = capacity;
        evaluation.demandedSlots = totalDemand;
        if (totalDemand > capacity) {
            evaluation.probabilityUpper = 0.0;
            evaluation.genericCapacityPruned = true;
            return evaluation;
        }

        const double joint = ComputeSingleGenericJointProbability(component, groups, capacity);
        evaluation.probabilityUpper = std::min(joint, minGroupUpper);
        return evaluation;
    }

    evaluation.usedFallback = true;
    evaluation.probabilityUpper = minGroupUpper;
    return evaluation;
}

} // namespace

void RunBottleneckSelectivityPredictor(const NormalizedSearchRequest &request,
                                       const WorldEnvelopeProfile *worldProfile,
                                       std::vector<ValidationIssue> *warnings,
                                       std::vector<std::string> *bottlenecks,
                                       double *predictedBottleneckProbability)
{
    if (warnings == nullptr || bottlenecks == nullptr || predictedBottleneckProbability == nullptr) {
        return;
    }

    warnings->clear();
    bottlenecks->clear();
    *predictedBottleneckProbability = 1.0;

    if (worldProfile == nullptr || !worldProfile->valid || request.groups.empty()) {
        return;
    }

    std::vector<GroupPrediction> predictions;
    predictions.reserve(request.groups.size());
    for (const auto &group : request.groups) {
        predictions.push_back(PredictGroup(group, *worldProfile));
    }

    std::vector<int> sorted;
    sorted.reserve(predictions.size());
    for (size_t i = 0; i < predictions.size(); ++i) {
        sorted.push_back(static_cast<int>(i));
    }
    std::sort(sorted.begin(),
              sorted.end(),
              [&](int left, int right) {
                  const auto &lhs = predictions[static_cast<size_t>(left)];
                  const auto &rhs = predictions[static_cast<size_t>(right)];
                  if (lhs.score != rhs.score) {
                      return lhs.score > rhs.score;
                  }
                  if (lhs.probabilityUpper != rhs.probabilityUpper) {
                      return lhs.probabilityUpper < rhs.probabilityUpper;
                  }
                  return lhs.geyserId < rhs.geyserId;
              });

    const int topCount = std::min(static_cast<int>(sorted.size()), kTopBottlenecks);
    std::vector<int> selected;
    selected.reserve(static_cast<size_t>(topCount));
    bool hasLowConfidenceDistance = false;
    for (int i = 0; i < topCount; ++i) {
        const int index = sorted[static_cast<size_t>(i)];
        selected.push_back(index);
        bottlenecks->push_back(predictions[static_cast<size_t>(index)].geyserId);
        if (predictions[static_cast<size_t>(index)].lowConfidenceDistance) {
            hasLowConfidenceDistance = true;
        }
    }

    const auto components = BuildConnectedComponents(selected, predictions);
    double bottleneckProbability = 1.0;
    for (const auto &component : components) {
        const auto evaluation = EvaluateComponent(component, predictions, *worldProfile);
        bottleneckProbability *= ClampProbability(evaluation.probabilityUpper);
        if (evaluation.genericCapacityPruned) {
            warnings->push_back(BuildGenericCapacityPrunedWarning(evaluation.groups,
                                                                  evaluation.demandedSlots,
                                                                  evaluation.capacityUpper));
        } else if (evaluation.usedFallback) {
            warnings->push_back(BuildDependencyFallbackWarning(evaluation.groups));
        }
    }
    bottleneckProbability = ClampProbability(bottleneckProbability);
    *predictedBottleneckProbability = bottleneckProbability;

    if (bottleneckProbability < 0.05) {
        const bool strongWarning = bottleneckProbability < 0.01 && !hasLowConfidenceDistance;
        warnings->push_back(BuildLowProbabilityWarning(bottleneckProbability,
                                                       *bottlenecks,
                                                       strongWarning,
                                                       hasLowConfidenceDistance));
    }
}

} // namespace SearchAnalysis
