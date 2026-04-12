#include "SearchAnalysis/HardValidator.hpp"
#include "SearchAnalysis/SearchConstraintNormalizer.hpp"

#include <iostream>

namespace {

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

    if (failures == 0) {
        std::cout << "[PASS] test_search_analysis" << std::endl;
        return 0;
    }
    return 1;
}
