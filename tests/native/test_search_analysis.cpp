#include "SearchAnalysis/HardValidator.hpp"
#include "SearchAnalysis/SearchConstraintNormalizer.hpp"

#include <iostream>

namespace {

template<typename T>
concept HasWorkersField = requires(T value) {
    value.workers;
};

template<typename T>
concept HasEnableWarmupField = requires(T value) {
    value.enableWarmup;
};

template<typename T>
concept HasEnableAdaptiveDownField = requires(T value) {
    value.enableAdaptiveDown;
};

template<typename T>
concept HasChunkSizeField = requires(T value) {
    value.chunkSize;
};

template<typename T>
concept HasProgressIntervalField = requires(T value) {
    value.progressInterval;
};

template<typename T>
concept HasSampleWindowMsField = requires(T value) {
    value.sampleWindowMs;
};

template<typename T>
concept HasAdaptiveMinWorkersField = requires(T value) {
    value.adaptiveMinWorkers;
};

template<typename T>
concept HasAdaptiveDropThresholdField = requires(T value) {
    value.adaptiveDropThreshold;
};

template<typename T>
concept HasAdaptiveDropWindowsField = requires(T value) {
    value.adaptiveDropWindows;
};

template<typename T>
concept HasAdaptiveCooldownMsField = requires(T value) {
    value.adaptiveCooldownMs;
};

static_assert(!HasWorkersField<SearchAnalysis::SearchCpuConfig>,
              "SearchAnalysis::SearchCpuConfig should not expose legacy workers");
static_assert(!HasEnableWarmupField<SearchAnalysis::SearchCpuConfig>,
              "SearchAnalysis::SearchCpuConfig should not expose legacy enableWarmup");
static_assert(!HasEnableAdaptiveDownField<SearchAnalysis::SearchCpuConfig>,
              "SearchAnalysis::SearchCpuConfig should not expose legacy enableAdaptiveDown");
static_assert(!HasChunkSizeField<SearchAnalysis::SearchCpuConfig>,
              "SearchAnalysis::SearchCpuConfig should not expose legacy chunkSize");
static_assert(!HasProgressIntervalField<SearchAnalysis::SearchCpuConfig>,
              "SearchAnalysis::SearchCpuConfig should not expose legacy progressInterval");
static_assert(!HasSampleWindowMsField<SearchAnalysis::SearchCpuConfig>,
              "SearchAnalysis::SearchCpuConfig should not expose legacy sampleWindowMs");
static_assert(!HasAdaptiveMinWorkersField<SearchAnalysis::SearchCpuConfig>,
              "SearchAnalysis::SearchCpuConfig should not expose legacy adaptiveMinWorkers");
static_assert(!HasAdaptiveDropThresholdField<SearchAnalysis::SearchCpuConfig>,
              "SearchAnalysis::SearchCpuConfig should not expose legacy adaptiveDropThreshold");
static_assert(!HasAdaptiveDropWindowsField<SearchAnalysis::SearchCpuConfig>,
              "SearchAnalysis::SearchCpuConfig should not expose legacy adaptiveDropWindows");
static_assert(!HasAdaptiveCooldownMsField<SearchAnalysis::SearchCpuConfig>,
              "SearchAnalysis::SearchCpuConfig should not expose legacy adaptiveCooldownMs");

bool Expect(bool condition, const char *message, int &failures)
{
    if (condition) {
        return true;
    }
    std::cerr << "[FAIL] " << message << std::endl;
    ++failures;
    return false;
}

SearchAnalysis::SearchCatalog BuildMockCatalog()
{
    SearchAnalysis::SearchCatalog catalog;
    catalog.worlds.push_back({.id = 0, .code = "SNDST-A-"});
    catalog.worlds.push_back({.id = 1, .code = "OCAN-A-"});
    catalog.geysers.push_back({.id = 0, .key = "steam"});
    catalog.geysers.push_back({.id = 2, .key = "hot_water"});
    catalog.geysers.push_back({.id = 15, .key = "methane"});
    return catalog;
}

} // namespace

int RunAllTests()
{
    int failures = 0;

    auto HasIssue = [](const std::vector<SearchAnalysis::ValidationIssue> &issues,
                       const char *code) {
        for (const auto &issue : issues) {
            if (issue.code == code) {
                return true;
            }
        }
        return false;
    };

    {
        SearchAnalysis::SearchAnalysisRequest request;
        request.worldType = 1;
        request.seedStart = 10;
        request.seedEnd = 20;
        request.constraints.required = {"hot_water"};
        request.constraints.count = {
            {.geyserId = "hot_water", .minCount = 2, .maxCount = 3},
        };
        request.constraints.distance = {
            {.geyserId = "hot_water", .minDist = 0.0, .maxDist = 100.0},
        };

        const auto normalized = SearchAnalysis::NormalizeSearchRequest(request);
        Expect(normalized.groups.size() == 1, "normalize should merge same geyser group", failures);
        if (!normalized.groups.empty()) {
            const auto &group = normalized.groups.front();
            Expect(group.minCount == 2, "normalized minCount should apply max rule", failures);
            Expect(group.maxCount == 3, "normalized maxCount should apply min rule", failures);
            Expect(group.hasRequired, "normalized group should mark required", failures);
            Expect(group.hasExplicitCount, "normalized group should mark explicit count", failures);
            Expect(group.distanceRules.size() == 1, "normalized group should keep distance rule", failures);
        }
    }

    {
        SearchAnalysis::SearchAnalysisRequest request;
        request.worldType = 1;
        request.seedStart = 10;
        request.seedEnd = 20;
        request.constraints.distance = {
            {.geyserId = "hot_water", .minDist = 0.0, .maxDist = 80.0},
        };

        const auto normalized = SearchAnalysis::NormalizeSearchRequest(request);
        Expect(normalized.groups.size() == 1,
               "distance-only normalize should keep one geyser group",
               failures);
        if (!normalized.groups.empty()) {
            const auto &group = normalized.groups.front();
            Expect(group.minCount == 1,
                   "distance-only normalize should require at least one geyser",
                   failures);
            Expect(group.maxCount == std::numeric_limits<int>::max(),
                   "distance-only normalize should preserve open upper bound",
                   failures);
            Expect(!group.hasRequired,
                   "distance-only normalize should not mark required",
                   failures);
            Expect(group.distanceRules.size() == 1,
                   "distance-only normalize should preserve distance rule",
                   failures);
        }
    }

    {
        SearchAnalysis::SearchAnalysisRequest request;
        request.worldType = 99;
        request.seedStart = 100;
        request.seedEnd = 50;
        request.mixing = 50000000;
        request.constraints.required = {"unknown_geyser"};
        request.constraints.forbidden = {"unknown_geyser"};
        request.constraints.distance = {
            {.geyserId = "unknown_geyser", .minDist = 10.0, .maxDist = 1.0},
        };
        request.constraints.count = {
            {.geyserId = "unknown_geyser", .minCount = 1, .maxCount = 0},
        };

        const auto result = SearchAnalysis::RunSearchAnalysis(request, BuildMockCatalog());
        Expect(!result.errors.empty(), "hard validator should return errors", failures);

        bool hasLayer1 = false;
        bool hasLayer2 = false;
        bool hasLayer3 = false;
        for (const auto &issue : result.errors) {
            if (issue.layer == "layer1") {
                hasLayer1 = true;
            } else if (issue.layer == "layer2") {
                hasLayer2 = true;
            } else if (issue.layer == "layer3") {
                hasLayer3 = true;
            }
        }
        Expect(hasLayer1, "hard validator should include layer1 errors", failures);
        Expect(hasLayer2, "hard validator should include layer2 errors", failures);
        Expect(hasLayer3, "hard validator should include layer3 errors", failures);
    }

    {
        SearchAnalysis::SearchAnalysisRequest request;
        request.worldType = 1;
        request.seedStart = 100;
        request.seedEnd = 200;
        request.constraints.count = {
            {.geyserId = "hot_water", .minCount = 1, .maxCount = 1},
        };

        SearchAnalysis::WorldEnvelopeProfile profile;
        profile.valid = true;
        profile.worldType = 1;
        profile.diagonal = 400.0;
        profile.possibleMaxCountByType["hot_water"] = 3;
        profile.genericTypeUpperById["hot_water"] = 0.01;
        profile.genericSlotUpper = 1;
        profile.genericSourceSummary.push_back(SearchAnalysis::SourceSummary{
            .ruleId = "rule-generic",
            .templateName = "geysers/generic",
            .geyserId = "geysers/generic",
            .upperBound = 1,
            .sourceKind = "generic",
            .poolId = "generic",
        });
        profile.sourcePools.push_back(SearchAnalysis::SourcePool{
            .poolId = "generic",
            .sourceKind = "generic",
            .capacityUpper = 1,
        });

        const auto result = SearchAnalysis::RunSearchAnalysis(request,
                                                              BuildMockCatalog(),
                                                              &profile);
        Expect(result.errors.empty(), "layer4 warning case should not have errors", failures);
        Expect(!result.warnings.empty(),
               "layer4 should emit warning for low predicted probability",
               failures);
        Expect(result.predictedBottleneckProbability < 0.05,
               "layer4 predicted probability should be low",
               failures);
        Expect(!result.bottlenecks.empty(),
               "layer4 should include bottleneck geyser list",
               failures);
        Expect(result.warnings.front().message.find("乐观估计可匹配概率=") != std::string::npos,
               "layer4 low probability warning should use optimistic estimate wording",
               failures);
    }

    {
        SearchAnalysis::SearchAnalysisRequest request;
        request.worldType = 1;
        request.seedStart = 100;
        request.seedEnd = 200;
        request.constraints.count = {
            {.geyserId = "hot_water", .minCount = 3, .maxCount = 3},
            {.geyserId = "methane", .minCount = 2, .maxCount = 2},
        };

        SearchAnalysis::WorldEnvelopeProfile profile;
        profile.valid = true;
        profile.worldType = 1;
        profile.diagonal = 400.0;
        profile.possibleMaxCountByType["hot_water"] = 5;
        profile.possibleMaxCountByType["methane"] = 5;
        profile.genericTypeUpperById["hot_water"] = 0.1;
        profile.genericTypeUpperById["methane"] = 0.1;
        profile.genericSlotUpper = 4;
        profile.genericSourceSummary.push_back(SearchAnalysis::SourceSummary{
            .ruleId = "rule-generic",
            .templateName = "geysers/generic",
            .geyserId = "geysers/generic",
            .upperBound = 4,
            .sourceKind = "generic",
            .poolId = "generic",
        });
        profile.sourcePools.push_back(SearchAnalysis::SourcePool{
            .poolId = "generic",
            .sourceKind = "generic",
            .capacityUpper = 4,
        });

        const auto result = SearchAnalysis::RunSearchAnalysis(request,
                                                              BuildMockCatalog(),
                                                              &profile);
        Expect(result.errors.empty(),
               "generic capacity pruning case should not have hard errors",
               failures);
        Expect(result.predictedBottleneckProbability <= 1e-9,
               "generic capacity pruning should drive predicted probability to zero",
               failures);
        bool hasCapacityWarning = false;
        for (const auto &issue : result.warnings) {
            if (issue.code == "predict.generic_capacity_pruned") {
                hasCapacityWarning = true;
                break;
            }
        }
        Expect(hasCapacityWarning,
               "generic capacity pruning should emit dedicated warning code",
               failures);
    }

    {
        SearchAnalysis::SearchAnalysisRequest request;
        request.worldType = 1;
        request.seedStart = 100;
        request.seedEnd = 200;
        request.constraints.required = {"hot_water"};
        request.constraints.distance = {
            {.geyserId = "hot_water", .minDist = 0.0, .maxDist = 500.0},
        };
        request.constraints.count = {
            {.geyserId = "hot_water", .minCount = 1, .maxCount = 4},
        };

        SearchAnalysis::WorldEnvelopeProfile profile;
        profile.valid = true;
        profile.worldType = 1;
        profile.diagonal = 100.0;
        profile.possibleMaxCountByType["hot_water"] = 2;

        const auto result = SearchAnalysis::RunSearchAnalysis(request,
                                                              BuildMockCatalog(),
                                                              &profile);
        bool hasDistanceLimitError = false;
        bool hasCountLimitError = false;
        for (const auto &issue : result.errors) {
            if (issue.code == "world.distance_max_gt_world_diagonal") {
                hasDistanceLimitError = true;
            }
            if (issue.code == "world.count_max_gt_possible_max") {
                hasCountLimitError = true;
            }
        }
        Expect(hasDistanceLimitError,
               "layer2 should reject distance max out of world diagonal",
               failures);
        Expect(hasCountLimitError,
               "layer2 should reject count max above possible world upper bound",
               failures);
    }

    {
        SearchAnalysis::SearchAnalysisRequest request;
        request.worldType = 1;
        request.seedStart = 100;
        request.seedEnd = 200;
        request.constraints.distance = {
            {.geyserId = "hot_water", .minDist = 0.0, .maxDist = 50.0},
        };

        SearchAnalysis::WorldEnvelopeProfile profile;
        profile.valid = true;
        profile.worldType = 1;
        profile.diagonal = 100.0;
        profile.possibleMaxCountByType["hot_water"] = 2;

        const auto result = SearchAnalysis::RunSearchAnalysis(request,
                                                              BuildMockCatalog(),
                                                              &profile);
        bool hasUnexpectedCountMaxError = false;
        for (const auto &issue : result.errors) {
            if (issue.code == "world.count_max_gt_possible_max") {
                hasUnexpectedCountMaxError = true;
                break;
            }
        }
        Expect(!hasUnexpectedCountMaxError,
               "distance-only group should not trigger count max upper-bound error",
               failures);
    }

    {
        SearchAnalysis::SearchAnalysisRequest request;
        request.worldType = 1;
        request.seedStart = 100;
        request.seedEnd = 200;
        request.constraints.required = {"hot_water"};

        SearchAnalysis::WorldEnvelopeProfile profile;
        profile.valid = true;
        profile.worldType = 1;
        profile.diagonal = 100.0;
        profile.possibleMaxCountByType["hot_water"] = 2;

        const auto result = SearchAnalysis::RunSearchAnalysis(request,
                                                              BuildMockCatalog(),
                                                              &profile);
        bool hasUnexpectedCountMaxError = false;
        for (const auto &issue : result.errors) {
            if (issue.code == "world.count_max_gt_possible_max") {
                hasUnexpectedCountMaxError = true;
                break;
            }
        }
        Expect(!hasUnexpectedCountMaxError,
               "required-only group should not trigger count max upper-bound error",
               failures);
    }

    {
        SearchAnalysis::SearchAnalysisRequest request;
        request.worldType = 1;
        request.seedStart = 100;
        request.seedEnd = 200;
        request.constraints.distance = {
            {.geyserId = "hot_water", .minDist = 0.0, .maxDist = 50.0},
        };
        request.constraints.count = {
            {.geyserId = "hot_water", .minCount = 0, .maxCount = 0},
        };

        SearchAnalysis::WorldEnvelopeProfile profile;
        profile.valid = true;
        profile.worldType = 1;
        profile.diagonal = 100.0;
        profile.possibleMaxCountByType["hot_water"] = 2;

        const auto result = SearchAnalysis::RunSearchAnalysis(request,
                                                              BuildMockCatalog(),
                                                              &profile);
        Expect(HasIssue(result.errors, "conflict.forbidden_with_distance"),
               "count.max=0 with distance should be treated as forbidden conflict",
               failures);
    }

    {
        SearchAnalysis::SearchAnalysisRequest request;
        request.worldType = 1;
        request.seedStart = 100;
        request.seedEnd = 200;
        request.mixing = 1;

        SearchAnalysis::WorldEnvelopeProfile profile;
        profile.valid = true;
        profile.worldType = 1;
        profile.diagonal = 100.0;
        profile.disabledMixingSlots = {0};

        const auto result = SearchAnalysis::RunSearchAnalysis(request,
                                                              BuildMockCatalog(),
                                                              &profile);
        Expect(HasIssue(result.errors, "world.disabled_mixing_slot_enabled"),
               "enabled disabled mixing slot should be rejected in layer2",
               failures);
    }

    {
        SearchAnalysis::SearchAnalysisRequest request;
        request.worldType = 1;
        request.seedStart = 100;
        request.seedEnd = 200;
        request.constraints.distance = {
            {.geyserId = "hot_water", .minDist = 0.0, .maxDist = 50.0},
        };
        request.constraints.count = {
            {.geyserId = "hot_water", .minCount = 1, .maxCount = 1},
        };

        SearchAnalysis::WorldEnvelopeProfile profile;
        profile.valid = true;
        profile.worldType = 1;
        profile.width = 200;
        profile.height = 200;
        profile.diagonal = 282.8;
        profile.possibleMaxCountByType["hot_water"] = 1;
        profile.exactSourceSummary.push_back(SearchAnalysis::SourceSummary{
            .ruleId = "rule-hot-water",
            .templateName = "poi/hot_water",
            .geyserId = "hot_water",
            .upperBound = 1,
            .sourceKind = "exact",
            .poolId = "rule-hot-water",
            .envelopeId = "env-hot-water",
        });
        profile.sourcePools.push_back(SearchAnalysis::SourcePool{
            .poolId = "rule-hot-water",
            .sourceKind = "exact",
            .capacityUpper = 1,
        });
        profile.spatialEnvelopes.push_back(SearchAnalysis::SpatialEnvelope{
            .envelopeId = "env-hot-water",
            .confidence = "medium",
            .method = "candidate-sites",
        });
        profile.envelopeStatsById["env-hot-water"] = SearchAnalysis::EnvelopeStats{
            .candidateCount = 4,
            .candidateDistances = {10.0, 20.0, 90.0, 130.0},
            .confidence = "medium",
            .method = "candidate-sites",
        };

        const auto result = SearchAnalysis::RunSearchAnalysis(request,
                                                              BuildMockCatalog(),
                                                              &profile);
        Expect(result.errors.empty(), "envelope-backed distance test should not hard fail", failures);
        Expect(result.predictedBottleneckProbability > 0.49 &&
                   result.predictedBottleneckProbability < 0.51,
               "distance probability should use source-conditioned envelope mass",
               failures);
        bool hasLowProbabilityWarning = false;
        for (const auto &issue : result.warnings) {
            if (issue.code.rfind("predict.low_probability", 0) == 0) {
                hasLowProbabilityWarning = true;
                break;
            }
        }
        Expect(!hasLowProbabilityWarning,
               "source-conditioned envelope should avoid false low-probability warning",
               failures);
    }

    {
        SearchAnalysis::SearchAnalysisRequest request;
        request.worldType = 1;
        request.seedStart = 100;
        request.seedEnd = 200;
        request.constraints.distance = {
            {.geyserId = "hot_water", .minDist = 0.0, .maxDist = 80.0},
        };

        SearchAnalysis::WorldEnvelopeProfile profile;
        profile.valid = true;
        profile.worldType = 1;
        profile.diagonal = 400.0;
        profile.possibleMaxCountByType["hot_water"] = 3;
        profile.genericTypeUpperById["hot_water"] = 0.01;
        profile.genericSlotUpper = 1;
        profile.genericSourceSummary.push_back(SearchAnalysis::SourceSummary{
            .ruleId = "rule-generic",
            .templateName = "geysers/generic",
            .geyserId = "geysers/generic",
            .upperBound = 1,
            .sourceKind = "generic",
            .poolId = "generic",
        });
        profile.sourcePools.push_back(SearchAnalysis::SourcePool{
            .poolId = "generic",
            .sourceKind = "generic",
            .capacityUpper = 1,
        });

        const auto result = SearchAnalysis::RunSearchAnalysis(request,
                                                              BuildMockCatalog(),
                                                              &profile);
        Expect(result.errors.empty(),
               "distance-only low probability case should not have hard errors",
               failures);
        Expect(result.predictedBottleneckProbability < 0.05,
               "distance-only analysis should not overestimate to certainty",
               failures);
        bool hasLowProbabilityWarning = false;
        for (const auto &issue : result.warnings) {
            if (issue.code.rfind("predict.low_probability", 0) == 0) {
                hasLowProbabilityWarning = true;
                break;
            }
        }
        Expect(hasLowProbabilityWarning,
               "distance-only low probability case should emit low-probability warning",
               failures);
    }

    if (failures == 0) {
        std::cout << "[PASS] test_search_analysis" << std::endl;
        return 0;
    }
    return 1;
}
