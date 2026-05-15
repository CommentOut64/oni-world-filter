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

int EncodeMixingFromLevels(const std::vector<int> &levels)
{
    int value = 0;
    for (const int level : levels) {
        value = value * 5 + level;
    }
    return value;
}

SearchAnalysis::SearchCatalog BuildMockCatalog()
{
    SearchAnalysis::SearchCatalog catalog;
    catalog.worlds.push_back({.id = 0, .code = "SNDST-A-"});
    catalog.worlds.push_back({.id = 1, .code = "OCAN-A-"});
    catalog.geysers.push_back({.id = 0, .key = "steam"});
    catalog.geysers.push_back({.id = 2, .key = "hot_water"});
    catalog.geysers.push_back({.id = 15, .key = "methane"});
    catalog.traits.push_back({
        .id = "traits/GeoDormant",
        .name = "Geo Dormant",
        .description = "",
        .traitTags = {"geo"},
        .searchable = true,
    });
    catalog.traits.push_back({
        .id = "traits/MagmaVents",
        .name = "Magma Vents",
        .description = "",
        .exclusiveWith = {"traits/FrozenCore"},
        .exclusiveWithTags = {"thermal-core"},
        .searchable = true,
    });
    catalog.traits.push_back({
        .id = "traits/FrozenCore",
        .name = "Frozen Core",
        .description = "",
        .exclusiveWith = {"traits/MagmaVents"},
        .exclusiveWithTags = {"thermal-core"},
        .searchable = true,
    });
    catalog.traits.push_back({
        .id = "traits/BuriedOcean",
        .name = "Buried Ocean",
        .description = "",
        .exclusiveWithTags = {"hydrology"},
        .searchable = true,
    });
    catalog.traits.push_back({
        .id = "traits/DryCore",
        .name = "Dry Core",
        .description = "",
        .exclusiveWithTags = {"hydrology"},
        .searchable = true,
    });
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
    auto HasWarning = [](const std::vector<SearchAnalysis::ValidationIssue> &issues,
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
        request.worldType = 1;
        request.seedStart = 10;
        request.seedEnd = 20;
        request.constraints.count = {
            {.geyserId = "hot_water", .minCount = 0, .maxCount = 0},
        };

        const auto result = SearchAnalysis::RunSearchAnalysis(request, BuildMockCatalog());
        Expect(HasIssue(result.errors, "range.count_min_less_than_one"),
               "count.min=0 should be rejected in layer1",
               failures);
        Expect(HasIssue(result.errors, "range.count_max_less_than_one"),
               "count.max=0 should be rejected in layer1",
               failures);
    }

    {
        SearchAnalysis::SearchAnalysisRequest request;
        request.worldType = 1;
        request.seedStart = 10;
        request.seedEnd = 20;
        request.constraints.required = {"hot_water"};
        request.constraints.count = {
            {.geyserId = "hot_water", .minCount = 1, .maxCount = 2},
        };

        const auto result = SearchAnalysis::RunSearchAnalysis(request, BuildMockCatalog());
        Expect(HasIssue(result.errors, "conflict.required_with_count"),
               "required + count should be rejected in layer3",
               failures);
    }

    {
        SearchAnalysis::SearchAnalysisRequest request;
        request.worldType = 1;
        request.seedStart = 10;
        request.seedEnd = 20;
        request.constraints.required = {"hot_water"};
        request.constraints.distance = {
            {.geyserId = "hot_water", .minDist = 0.0, .maxDist = 80.0},
        };

        const auto result = SearchAnalysis::RunSearchAnalysis(request, BuildMockCatalog());
        Expect(HasIssue(result.errors, "conflict.required_with_distance"),
               "required + distance should be rejected in layer3",
               failures);
    }

    {
        SearchAnalysis::SearchAnalysisRequest request;
        request.worldType = 1;
        request.seedStart = 10;
        request.seedEnd = 20;
        request.constraints.forbidden = {"hot_water"};
        request.constraints.count = {
            {.geyserId = "hot_water", .minCount = 1, .maxCount = 2},
        };

        const auto result = SearchAnalysis::RunSearchAnalysis(request, BuildMockCatalog());
        Expect(HasIssue(result.errors, "conflict.forbidden_with_count"),
               "forbidden + count should be rejected in layer3",
               failures);
    }

    {
        SearchAnalysis::SearchAnalysisRequest request;
        request.worldType = 1;
        request.seedStart = 10;
        request.seedEnd = 20;
        request.constraints.count = {
            {.geyserId = "hot_water", .minCount = 1, .maxCount = 2},
        };
        request.constraints.distance = {
            {.geyserId = "hot_water", .minDist = 0.0, .maxDist = 80.0},
        };

        SearchAnalysis::WorldEnvelopeProfile profile;
        profile.valid = true;
        profile.worldType = 1;
        profile.diagonal = 400.0;
        profile.possibleMaxCountByType["hot_water"] = 3;

        const auto result = SearchAnalysis::RunSearchAnalysis(request,
                                                              BuildMockCatalog(),
                                                              &profile);
        Expect(!HasIssue(result.errors, "conflict.required_with_count"),
               "count + distance should remain legal",
               failures);
        Expect(!HasIssue(result.errors, "conflict.required_with_distance"),
               "count + distance should not be mistaken for required conflict",
               failures);
        Expect(!HasIssue(result.errors, "conflict.forbidden_with_distance"),
               "count + distance should not be mistaken for forbidden conflict",
               failures);
    }

    {
        SearchAnalysis::SearchAnalysisRequest request;
        request.worldType = 99;
        request.seedStart = 100;
        request.seedEnd = 50;
        request.mixing = 50000000;
        request.constraints.required = {"unknown_geyser"};
        request.constraints.forbidden = {"unknown_geyser"};
        request.constraints.requiredTraits = {"traits/UnknownTrait"};
        request.constraints.forbiddenTraits = {"traits/UnknownTrait"};
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
        request.seedStart = 10;
        request.seedEnd = 20;
        request.constraints.requiredTraits = {"traits/MagmaVents", "traits/MagmaVents"};

        const auto result = SearchAnalysis::RunSearchAnalysis(request, BuildMockCatalog());
        Expect(HasIssue(result.errors, "conflict.required_trait_duplicate"),
               "duplicate required trait should be rejected in layer3",
               failures);
    }

    {
        SearchAnalysis::SearchAnalysisRequest request;
        request.worldType = 1;
        request.seedStart = 10;
        request.seedEnd = 20;
        request.constraints.requiredTraits = {"traits/MagmaVents"};
        request.constraints.forbiddenTraits = {"traits/MagmaVents"};

        const auto result = SearchAnalysis::RunSearchAnalysis(request, BuildMockCatalog());
        Expect(HasIssue(result.errors, "conflict.required_forbidden_trait"),
               "same trait in required and forbidden should be rejected in layer3",
               failures);
    }

    {
        SearchAnalysis::SearchAnalysisRequest request;
        request.worldType = 1;
        request.seedStart = 10;
        request.seedEnd = 20;
        request.constraints.requiredTraits = {"traits/MagmaVents"};

        SearchAnalysis::WorldEnvelopeProfile profile;
        profile.valid = true;
        profile.worldType = 1;
        profile.possibleTraitIds = {"traits/GeoDormant"};
        profile.impossibleTraitIds = {"traits/MagmaVents"};

        const auto result = SearchAnalysis::RunSearchAnalysis(request,
                                                              BuildMockCatalog(),
                                                              &profile);
        Expect(HasIssue(result.errors, "world.required_trait_impossible"),
               "required impossible trait should be rejected in layer2",
               failures);
    }

    {
        SearchAnalysis::SearchAnalysisRequest request;
        request.worldType = 1;
        request.seedStart = 10;
        request.seedEnd = 20;
        request.constraints.forbiddenTraits = {"traits/MagmaVents"};

        SearchAnalysis::WorldEnvelopeProfile profile;
        profile.valid = true;
        profile.worldType = 1;
        profile.possibleTraitIds = {"traits/GeoDormant"};
        profile.impossibleTraitIds = {"traits/MagmaVents"};

        const auto result = SearchAnalysis::RunSearchAnalysis(request,
                                                              BuildMockCatalog(),
                                                              &profile);
        Expect(result.errors.empty(),
               "forbidden impossible trait should not become a hard error",
               failures);
        Expect(HasWarning(result.warnings, "world.forbidden_trait_already_impossible"),
               "forbidden impossible trait should become a warning",
               failures);
    }

    {
        SearchAnalysis::SearchAnalysisRequest request;
        request.worldType = 1;
        request.seedStart = 10;
        request.seedEnd = 20;
        request.constraints.requiredTraits = {"traits/GeoDormant", "traits/MagmaVents"};

        SearchAnalysis::WorldEnvelopeProfile profile;
        profile.valid = true;
        profile.worldType = 1;
        profile.possibleTraitIds = {"traits/GeoDormant", "traits/MagmaVents"};
        profile.impossibleTraitIds = {"traits/FrozenCore"};
        profile.possibleTraitCountUpper = 1;

        const auto result = SearchAnalysis::RunSearchAnalysis(request,
                                                              BuildMockCatalog(),
                                                              &profile);
        Expect(HasIssue(result.errors, "world.required_trait_count_gt_possible_max"),
               "required trait count beyond world upper bound should be rejected in layer2",
               failures);
    }

    {
        SearchAnalysis::SearchAnalysisRequest request;
        request.worldType = 1;
        request.seedStart = 10;
        request.seedEnd = 20;
        request.constraints.requiredTraits = {"traits/MagmaVents", "traits/FrozenCore"};

        const auto result = SearchAnalysis::RunSearchAnalysis(request, BuildMockCatalog());
        Expect(HasIssue(result.errors, "conflict.required_traits_mutually_exclusive"),
               "exclusiveWith required trait pair should be rejected in layer3",
               failures);
    }

    {
        SearchAnalysis::SearchAnalysisRequest request;
        request.worldType = 1;
        request.seedStart = 10;
        request.seedEnd = 20;
        request.constraints.requiredTraits = {"traits/BuriedOcean", "traits/DryCore"};

        const auto result = SearchAnalysis::RunSearchAnalysis(request, BuildMockCatalog());
        Expect(HasIssue(result.errors, "conflict.required_traits_mutually_exclusive"),
               "exclusiveWithTags required trait pair should be rejected in layer3",
               failures);
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
        request.constraints.forbidden = {"methane"};

        SearchAnalysis::WorldEnvelopeProfile profile;
        profile.valid = true;
        profile.worldType = 1;
        profile.diagonal = 100.0;
        profile.possibleMaxCountByType["hot_water"] = 2;

        const auto result = SearchAnalysis::RunSearchAnalysis(request,
                                                              BuildMockCatalog(),
                                                              &profile);
        Expect(result.errors.empty(),
               "forbidden-only impossible geyser should not become a hard error",
               failures);
        Expect(HasWarning(result.warnings, "world.forbidden_geyser_already_impossible"),
               "forbidden-only impossible geyser should become a warning",
               failures);
    }

    {
        SearchAnalysis::SearchAnalysisRequest request;
        request.worldType = 1;
        request.seedStart = 100;
        request.seedEnd = 200;
        request.mixing = EncodeMixingFromLevels({1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});

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
        request.mixing = EncodeMixingFromLevels({1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0});

        SearchAnalysis::WorldEnvelopeProfile profile;
        profile.valid = true;
        profile.worldType = 1;
        profile.diagonal = 100.0;
        profile.disabledMixingSlots = {6, 7, 8, 9, 10};

        const auto result = SearchAnalysis::RunSearchAnalysis(request,
                                                              BuildMockCatalog(),
                                                              &profile);
        Expect(!HasIssue(result.errors, "world.disabled_mixing_slot_enabled"),
               "PRE-style disabled slots should not reject frost-side enabled slots",
               failures);
    }

    {
        SearchAnalysis::SearchAnalysisRequest request;
        request.worldType = 1;
        request.seedStart = 100;
        request.seedEnd = 200;
        request.mixing = EncodeMixingFromLevels({0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1});

        SearchAnalysis::WorldEnvelopeProfile profile;
        profile.valid = true;
        profile.worldType = 1;
        profile.diagonal = 100.0;
        profile.disabledMixingSlots = {6, 7, 8, 9, 10};

        const auto result = SearchAnalysis::RunSearchAnalysis(request,
                                                              BuildMockCatalog(),
                                                              &profile);
        Expect(HasIssue(result.errors, "world.disabled_mixing_slot_enabled"),
               "PRE-style disabled slots should reject prehistoric-side enabled slots",
               failures);
    }

    {
        SearchAnalysis::SearchAnalysisRequest request;
        request.worldType = 1;
        request.seedStart = 100;
        request.seedEnd = 200;
        request.mixing = EncodeMixingFromLevels({0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1});

        SearchAnalysis::WorldEnvelopeProfile profile;
        profile.valid = true;
        profile.worldType = 1;
        profile.diagonal = 100.0;
        profile.disabledMixingSlots = {0, 1, 2, 3, 4};

        const auto result = SearchAnalysis::RunSearchAnalysis(request,
                                                              BuildMockCatalog(),
                                                              &profile);
        Expect(!HasIssue(result.errors, "world.disabled_mixing_slot_enabled"),
               "CER-style disabled slots should not reject prehistoric-side enabled slots",
               failures);
    }

    {
        SearchAnalysis::SearchAnalysisRequest request;
        request.worldType = 1;
        request.seedStart = 100;
        request.seedEnd = 200;
        request.mixing = EncodeMixingFromLevels({1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0});

        SearchAnalysis::WorldEnvelopeProfile profile;
        profile.valid = true;
        profile.worldType = 1;
        profile.diagonal = 100.0;
        profile.disabledMixingSlots = {0, 1, 2, 3, 4};

        const auto result = SearchAnalysis::RunSearchAnalysis(request,
                                                              BuildMockCatalog(),
                                                              &profile);
        Expect(HasIssue(result.errors, "world.disabled_mixing_slot_enabled"),
               "CER-style disabled slots should reject frost-side enabled slots",
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
