#include "SearchAnalysis/HardValidator.hpp"

#include <algorithm>

#include "SearchAnalysis/BottleneckSelectivityPredictor.hpp"
#include "SearchAnalysis/SearchConstraintNormalizer.hpp"

namespace SearchAnalysis {

namespace {

constexpr int kMixingMax = 48828124;

int ReadMixingSlotLevel(int mixing, int slot)
{
    if (slot < 0) {
        return 0;
    }
    int value = std::max(0, mixing);
    for (int i = 0; i < slot; ++i) {
        value /= 5;
    }
    return value % 5;
}

void AddIssue(std::vector<ValidationIssue> *issues,
              std::string layer,
              std::string code,
              std::string field,
              std::string message)
{
    if (issues == nullptr) {
        return;
    }
    issues->push_back(ValidationIssue{
        .layer = std::move(layer),
        .code = std::move(code),
        .field = std::move(field),
        .message = std::move(message),
    });
}

void ValidateLayer1(const SearchAnalysisRequest &request,
                    std::vector<ValidationIssue> *errors)
{
    if (request.seedStart < 0) {
        AddIssue(errors,
                 "layer1",
                 "range.seed_start_negative",
                 "seedStart",
                 "seedStart 不能为负数");
    }
    if (request.seedEnd < 0) {
        AddIssue(errors,
                 "layer1",
                 "range.seed_end_negative",
                 "seedEnd",
                 "seedEnd 不能为负数");
    }
    if (request.seedStart > request.seedEnd) {
        AddIssue(errors,
                 "layer1",
                 "range.seed_start_gt_seed_end",
                 "seedStart/seedEnd",
                 "seedStart 必须 <= seedEnd");
    }
    if (request.mixing < 0 || request.mixing > kMixingMax) {
        AddIssue(errors,
                 "layer1",
                 "range.mixing_out_of_bounds",
                 "mixing",
                 "mixing 超出有效范围");
    }
    if (request.threads < 0) {
        AddIssue(errors,
                 "layer1",
                 "range.threads_negative",
                 "threads",
                 "threads 不能为负数");
    }

    for (size_t i = 0; i < request.constraints.distance.size(); ++i) {
        const auto &rule = request.constraints.distance[i];
        if (rule.minDist < 0.0) {
            AddIssue(errors,
                     "layer1",
                     "range.distance_min_negative",
                     "constraints.distance[" + std::to_string(i) + "].minDist",
                     "distance.minDist 不能为负数");
        }
        if (rule.maxDist < 0.0) {
            AddIssue(errors,
                     "layer1",
                     "range.distance_max_negative",
                     "constraints.distance[" + std::to_string(i) + "].maxDist",
                     "distance.maxDist 不能为负数");
        }
        if (rule.minDist > rule.maxDist) {
            AddIssue(errors,
                     "layer1",
                     "range.distance_min_gt_max",
                     "constraints.distance[" + std::to_string(i) + "].maxDist",
                     "distance.minDist 必须 <= distance.maxDist");
        }
    }

    for (size_t i = 0; i < request.constraints.count.size(); ++i) {
        const auto &rule = request.constraints.count[i];
        if (rule.minCount < 0) {
            AddIssue(errors,
                     "layer1",
                     "range.count_min_negative",
                     "constraints.count[" + std::to_string(i) + "].minCount",
                     "count.minCount 不能为负数");
        }
        if (rule.maxCount < 0) {
            AddIssue(errors,
                     "layer1",
                     "range.count_max_negative",
                     "constraints.count[" + std::to_string(i) + "].maxCount",
                     "count.maxCount 不能为负数");
        }
        if (rule.minCount > rule.maxCount) {
            AddIssue(errors,
                     "layer1",
                     "range.count_min_gt_max",
                     "constraints.count[" + std::to_string(i) + "].maxCount",
                     "count.minCount 必须 <= count.maxCount");
        }
    }

    if (request.cpu.hasValue) {
        if (request.cpu.chunkSize < 1 || request.cpu.chunkSize > 2048) {
            AddIssue(errors,
                     "layer1",
                     "range.cpu_chunk_size",
                     "cpu.chunkSize",
                     "cpu.chunkSize 必须在 [1, 2048]");
        }
        if (request.cpu.progressInterval < 1 || request.cpu.progressInterval > 1000000) {
            AddIssue(errors,
                     "layer1",
                     "range.cpu_progress_interval",
                     "cpu.progressInterval",
                     "cpu.progressInterval 必须在 [1, 1000000]");
        }
        if (request.cpu.sampleWindowMs < 200 || request.cpu.sampleWindowMs > 10000) {
            AddIssue(errors,
                     "layer1",
                     "range.cpu_sample_window",
                     "cpu.sampleWindowMs",
                     "cpu.sampleWindowMs 必须在 [200, 10000]");
        }
        if (request.cpu.adaptiveMinWorkers < 1 || request.cpu.adaptiveMinWorkers > 1024) {
            AddIssue(errors,
                     "layer1",
                     "range.cpu_adaptive_min_workers",
                     "cpu.adaptiveMinWorkers",
                     "cpu.adaptiveMinWorkers 必须在 [1, 1024]");
        }
        if (request.cpu.adaptiveDropThreshold < 0.0 || request.cpu.adaptiveDropThreshold > 0.5) {
            AddIssue(errors,
                     "layer1",
                     "range.cpu_adaptive_drop_threshold",
                     "cpu.adaptiveDropThreshold",
                     "cpu.adaptiveDropThreshold 必须在 [0, 0.5]");
        }
        if (request.cpu.adaptiveDropWindows < 1 || request.cpu.adaptiveDropWindows > 10) {
            AddIssue(errors,
                     "layer1",
                     "range.cpu_adaptive_drop_windows",
                     "cpu.adaptiveDropWindows",
                     "cpu.adaptiveDropWindows 必须在 [1, 10]");
        }
        if (request.cpu.adaptiveCooldownMs < 1000 || request.cpu.adaptiveCooldownMs > 60000) {
            AddIssue(errors,
                     "layer1",
                     "range.cpu_adaptive_cooldown",
                     "cpu.adaptiveCooldownMs",
                     "cpu.adaptiveCooldownMs 必须在 [1000, 60000]");
        }
    }
}

void ValidateLayer2(const SearchAnalysisRequest &rawRequest,
                    const NormalizedSearchRequest &request,
                    const SearchCatalog &catalog,
                    const WorldEnvelopeProfile *worldProfile,
                    std::vector<ValidationIssue> *errors)
{
    if (request.worldType < 0 ||
        request.worldType >= static_cast<int>(catalog.worlds.size())) {
        AddIssue(errors,
                 "layer2",
                 "world.world_type_out_of_range",
                 "worldType",
                 "worldType 不在 catalog 范围内");
    }

    for (const auto &group : request.groups) {
        if (group.geyserIndex < 0) {
            AddIssue(errors,
                     "layer2",
                     "world.unknown_geyser",
                     "constraints",
                     "存在未知 geyserId: " + group.geyserId);
        }
    }

    if (worldProfile == nullptr || !worldProfile->valid) {
        return;
    }

    std::vector<int> enabledDisabledSlots;
    enabledDisabledSlots.reserve(worldProfile->disabledMixingSlots.size());
    for (const int slot : worldProfile->disabledMixingSlots) {
        if (ReadMixingSlotLevel(rawRequest.mixing, slot) > 0) {
            enabledDisabledSlots.push_back(slot);
        }
    }
    if (!enabledDisabledSlots.empty()) {
        std::string message = "当前 worldType 禁用了 mixing slot: ";
        for (size_t i = 0; i < enabledDisabledSlots.size(); ++i) {
            if (i != 0) {
                message += ", ";
            }
            message += std::to_string(enabledDisabledSlots[i]);
        }
        AddIssue(errors,
                 "layer2",
                 "world.disabled_mixing_slot_enabled",
                 "mixing",
                 std::move(message));
    }

    for (size_t i = 0; i < rawRequest.constraints.distance.size(); ++i) {
        const auto &rule = rawRequest.constraints.distance[i];
        if (rule.maxDist > worldProfile->diagonal) {
            AddIssue(errors,
                     "layer2",
                     "world.distance_max_gt_world_diagonal",
                     "constraints.distance[" + std::to_string(i) + "].maxDist",
                     "distance.maxDist 超过当前世界对角线");
        }
        if (rule.minDist > worldProfile->diagonal) {
            AddIssue(errors,
                     "layer2",
                     "world.distance_min_gt_world_diagonal",
                     "constraints.distance[" + std::to_string(i) + "].minDist",
                     "distance.minDist 超过当前世界对角线");
        }
    }

    for (const auto &group : request.groups) {
        if (group.geyserIndex < 0) {
            continue;
        }
        auto itr = worldProfile->possibleMaxCountByType.find(group.geyserId);
        const int possibleMax =
            (itr == worldProfile->possibleMaxCountByType.end()) ? 0 : itr->second;
        if (possibleMax <= 0) {
            AddIssue(errors,
                     "layer2",
                     "world.geyser_impossible",
                     "constraints",
                     "当前 worldType + mixing 下 geyser 不可能出现: " + group.geyserId);
            continue;
        }
        // 仅当请求显式给出上界（count / forbidden）时，才比较 count.max 上界。
        const bool hasExplicitUpperBound = group.hasExplicitCount || group.hasForbidden;
        if (hasExplicitUpperBound && group.maxCount > possibleMax) {
            AddIssue(errors,
                     "layer2",
                     "world.count_max_gt_possible_max",
                     "constraints.count",
                     "count.maxCount 超过当前世界上界: " + group.geyserId);
        }
        if (group.minCount > possibleMax) {
            AddIssue(errors,
                     "layer2",
                     "world.count_min_gt_possible_max",
                     "constraints.count",
                     "count.minCount 超过当前世界上界: " + group.geyserId);
        }
    }
}

void ValidateLayer3(const NormalizedSearchRequest &request,
                    std::vector<ValidationIssue> *errors)
{
    for (const auto &group : request.groups) {
        const bool emptiedByUpperBound =
            (group.hasForbidden || group.hasExplicitCount) && group.maxCount <= 0;
        if (group.hasRequired && group.hasForbidden) {
            AddIssue(errors,
                     "layer3",
                     "conflict.required_forbidden",
                     "constraints.required/constraints.forbidden",
                     "同一 geyser 不能同时 required 和 forbidden: " + group.geyserId);
        }
        if (group.minCount > group.maxCount) {
            AddIssue(errors,
                     "layer3",
                     "conflict.count_min_gt_max",
                     "constraints.count",
                     "归并后 minCount > maxCount: " + group.geyserId);
        }
        if (emptiedByUpperBound && !group.distanceRules.empty()) {
            AddIssue(errors,
                     "layer3",
                     "conflict.forbidden_with_distance",
                     "constraints.distance",
                     "已被排空的 geyser 不能同时设置 distance: " + group.geyserId);
        }
    }
}

} // namespace

SearchAnalysisResult RunSearchAnalysis(const SearchAnalysisRequest &request,
                                       const SearchCatalog &catalog,
                                       const WorldEnvelopeProfile *worldProfile)
{
    SearchAnalysisResult result;
    if (worldProfile != nullptr) {
        result.worldProfile = *worldProfile;
    }
    result.normalizedRequest = NormalizeSearchRequest(request);
    ValidateLayer1(request, &result.errors);
    ValidateLayer2(request, result.normalizedRequest, catalog, worldProfile, &result.errors);
    ValidateLayer3(result.normalizedRequest, &result.errors);
    if (result.errors.empty()) {
        RunBottleneckSelectivityPredictor(result.normalizedRequest,
                                          worldProfile,
                                          &result.warnings,
                                          &result.bottlenecks,
                                          &result.predictedBottleneckProbability);
    }
    return result;
}

} // namespace SearchAnalysis
