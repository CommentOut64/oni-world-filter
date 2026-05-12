#include "Geyser/GeyserParameterCalculator.hpp"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

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

bool ExpectNear(float actual, float expected, float tolerance, const char *message, int &failures)
{
    if (std::fabs(actual - expected) <= tolerance) {
        return true;
    }
    std::cerr << "[FAIL] " << message << " actual=" << actual << " expected=" << expected
              << " tolerance=" << tolerance << std::endl;
    ++failures;
    return false;
}

} // namespace

int RunAllTests()
{
    int failures = 0;

    const std::string sampleCoord = "V-SNDST-C-1927980015-0-3A-0";
    const int worldSeed = 1927980015;
    // 该样例世界经游戏本体反编译链路核对，喷口使用的全局 seed 为 baseSeed + 7。
    const int geyserSeed = worldSeed + 7;
    const int worldHeight = 380;

    {
        const std::vector<GeyserSummary> geysers = {
            {6, 205, 250},
            {17, 162, 216},
        };
        const auto details = GeyserCalc::BuildGeyserDetails(geyserSeed, worldHeight, geysers);

        Expect(details.size() == 2, "sample world should produce two requested geyser details", failures);
        if (details.size() == 2) {
            const auto &saltWater = details[0];
            Expect(saltWater.index == 0, "salt water index mismatch", failures);
            Expect(saltWater.hasParameters, "salt water should have parameters", failures);
            Expect(saltWater.parameterKind == "geyser", "salt water parameter kind mismatch", failures);
            ExpectNear(saltWater.derived.temperatureCelsius, 95.0f, 0.05f,
                       "salt water temperature mismatch", failures);
            ExpectNear(saltWater.derived.eruptionRateKgPerSecond, 12.4f, 0.15f,
                       "salt water eruption rate mismatch", failures);
            ExpectNear(saltWater.native.eruptionPeriodSeconds, 805.0f, 0.6f,
                       "salt water eruption period mismatch", failures);
            ExpectNear(saltWater.derived.eruptionSeconds, 324.0f, 0.6f,
                       "salt water eruption seconds mismatch", failures);
            ExpectNear(saltWater.derived.totalCycles, 81.8f, 0.15f,
                       "salt water total cycles mismatch", failures);
            ExpectNear(saltWater.derived.activeCycles, 56.4f, 0.15f,
                       "salt water active cycles mismatch", failures);
            ExpectNear(saltWater.derived.averageOverallYieldGPerSecond, 3437.3f, 2.0f,
                       "salt water average overall yield mismatch", failures);

            const auto &moltenIron = details[1];
            Expect(moltenIron.index == 1, "molten iron index mismatch", failures);
            Expect(moltenIron.hasParameters, "molten iron should have parameters", failures);
            Expect(moltenIron.parameterKind == "geyser", "molten iron parameter kind mismatch", failures);
            ExpectNear(moltenIron.derived.temperatureCelsius, 2526.9f, 0.1f,
                       "molten iron temperature mismatch", failures);
            ExpectNear(moltenIron.derived.eruptionRateKgPerSecond, 7.3f, 0.15f,
                       "molten iron eruption rate mismatch", failures);
            ExpectNear(moltenIron.native.eruptionPeriodSeconds, 730.0f, 0.6f,
                       "molten iron eruption period mismatch", failures);
            ExpectNear(moltenIron.derived.eruptionSeconds, 53.0f, 0.6f,
                       "molten iron eruption seconds mismatch", failures);
            ExpectNear(moltenIron.derived.totalCycles, 95.9f, 0.15f,
                       "molten iron total cycles mismatch", failures);
            ExpectNear(moltenIron.derived.activeCycles, 63.7f, 0.15f,
                       "molten iron active cycles mismatch", failures);
            ExpectNear(moltenIron.derived.averageOverallYieldGPerSecond, 355.6f, 2.0f,
                       "molten iron average overall yield mismatch", failures);
        }
    }

    {
        const std::vector<GeyserSummary> geysers = {
            {6, 205, 250},
        };
        const auto details = GeyserCalc::BuildGeyserDetails(geyserSeed, worldHeight, geysers);
        const auto wrongYDetails = GeyserCalc::BuildGeyserDetails(geyserSeed, worldHeight, {{6, 205, 130}});

        Expect(details.size() == 1, "single sample should produce one detail", failures);
        Expect(wrongYDetails.size() == 1, "wrong y sample should produce one detail", failures);
        if (details.size() == 1 && wrongYDetails.size() == 1) {
            const auto &detail = details[0];
            const auto &wrongYDetail = wrongYDetails[0];
            const float expectedRate = detail.native.averageActiveYieldKgPerCycle /
                                       (600.0f / detail.native.eruptionPeriodSeconds) /
                                       detail.derived.eruptionSeconds;
            const float expectedAverage = (detail.derived.activeSeconds / detail.native.eruptionPeriodSeconds) *
                                          (detail.derived.eruptionRateKgPerSecond * detail.derived.eruptionSeconds) /
                                          detail.native.activePeriodSeconds * 1000.0f;
            ExpectNear(detail.derived.eruptionRateKgPerSecond, expectedRate, 0.001f,
                       "derived eruption rate formula mismatch", failures);
            ExpectNear(detail.derived.averageOverallYieldGPerSecond, expectedAverage, 0.1f,
                       "derived average overall yield formula mismatch", failures);
            Expect(std::fabs(detail.derived.eruptionRateKgPerSecond - wrongYDetail.derived.eruptionRateKgPerSecond) > 0.5f,
                   "display y should not be treated as internal y", failures);
        }
    }

    {
        const auto details = GeyserCalc::BuildGeyserDetails(geyserSeed, worldHeight, {{27, 180, 337}, {30, 76, 235}});
        Expect(details.size() == 2, "non-geyser objects should still produce detail placeholders", failures);
        if (details.size() == 2) {
            Expect(!details[0].hasParameters, "oil reservoir should not have geyser parameters", failures);
            Expect(details[0].parameterKind == "reservoir", "oil reservoir parameter kind mismatch", failures);
            Expect(!details[1].hasParameters, "warp portal should not have geyser parameters", failures);
            Expect(details[1].parameterKind == "facility", "warp portal parameter kind mismatch", failures);
        }
    }

    {
        GeneratedWorldPreview preview;
        preview.summary.seed = worldSeed;
        preview.summary.worldSize = {256, worldHeight};
        preview.summary.geysers = {{6, 205, 250}};

        const auto report = GeyserCalc::BuildWorldReportData(preview, geyserSeed, 130, sampleCoord);
        Expect(report.geyserDetails.size() == 1,
               "world report should reuse geyser detail calculation",
               failures);
        if (report.geyserDetails.size() == 1) {
            ExpectNear(report.geyserDetails[0].native.eruptionPeriodSeconds, 805.0f, 0.6f,
                       "world report should use authoritative geyser seed", failures);
        }
    }

    if (failures == 0) {
        std::cout << "[PASS] test_geyser_parameter_calculator" << std::endl;
        return 0;
    }
    return 1;
}
