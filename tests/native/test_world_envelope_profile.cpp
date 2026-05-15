#include "SearchAnalysis/WorldEnvelopeProfile.hpp"
#include "Setting/SettingsCache.hpp"
#include "config.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>
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

bool ReadAssetBlob(std::vector<char> &data)
{
    std::ifstream file(SETTING_TEST_ASSET_FILEPATH, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    if (size <= 0) {
        return false;
    }
    data.resize(static_cast<size_t>(size));
    return file.read(data.data(), size).good();
}

bool HasAllSlots(const std::vector<int> &slots, const std::vector<int> &expected)
{
    std::set<int> table(slots.begin(), slots.end());
    for (int slot : expected) {
        if (!table.contains(slot)) {
            return false;
        }
    }
    return true;
}

bool Contains(const std::vector<std::string> &items, const char *value)
{
    return std::find(items.begin(), items.end(), value) != items.end();
}

} // namespace

int RunAllTests()
{
    int failures = 0;
    std::vector<char> data;
    if (!ReadAssetBlob(data)) {
        std::cerr << "[FAIL] failed to load settings asset blob" << std::endl;
        return 1;
    }

    SettingsCache settings;
    const std::string_view content(data.data(), data.size());
    if (!settings.LoadSettingsCache(content)) {
        std::cerr << "[FAIL] failed to parse settings cache" << std::endl;
        return 1;
    }

    {
        std::string error;
        const auto profile = SearchAnalysis::CompileWorldEnvelopeProfile(settings, 13, 625, &error);
        Expect(error.empty(), "compile profile should not return error", failures);
        Expect(profile.valid, "profile should be valid", failures);
        Expect(profile.width > 0, "profile width should be positive", failures);
        Expect(profile.height > 0, "profile height should be positive", failures);
        Expect(profile.diagonal > 0.0, "profile diagonal should be positive", failures);
        Expect(!profile.sourcePools.empty(), "profile sourcePools should not be empty", failures);

        bool hasGenericPool = false;
        for (const auto &pool : profile.sourcePools) {
            if (pool.poolId == "generic") {
                hasGenericPool = true;
                Expect(pool.capacityUpper == profile.genericSlotUpper,
                       "generic pool capacity should equal genericSlotUpper",
                       failures);
            }
        }
        Expect(hasGenericPool, "profile should include generic source pool", failures);

        bool hasRuleIndexFallback = false;
        for (const auto &source : profile.exactSourceSummary) {
            if (source.ruleId.rfind("rule-", 0) == 0) {
                hasRuleIndexFallback = true;
                break;
            }
        }
        Expect(!hasRuleIndexFallback,
               "exact source ruleId should not use unstable rule-index fallback",
               failures);
    }

    {
        const auto profile = SearchAnalysis::CompileWorldEnvelopeProfile(settings, 13, 0);
        Expect(profile.valid, "V-SNDST-C profile should be valid", failures);
        Expect(profile.possibleTraitIds.empty(),
               "V-SNDST-C primary should not expose selectable traits",
               failures);
        Expect(Contains(profile.impossibleTraitIds, "traits/Geodes"),
               "V-SNDST-C primary should mark regular traits impossible",
               failures);
    }

    {
        const auto profile = SearchAnalysis::CompileWorldEnvelopeProfile(settings, 17, 0);
        Expect(profile.valid, "V-LUSH-C profile should be valid", failures);
        Expect(!profile.possibleTraitIds.empty(),
               "not every classic primary should be treated as no-trait world",
               failures);
    }

    {
        const auto profile = SearchAnalysis::CompileWorldEnvelopeProfile(settings, 9, 625);
        Expect(profile.valid, "CER profile should be valid", failures);
        Expect(HasAllSlots(profile.disabledMixingSlots, {0, 1, 2, 3, 4}),
               "CER profile should disable slots 0..4",
               failures);
    }

    {
        const auto profile = SearchAnalysis::CompileWorldEnvelopeProfile(settings, 11, 625);
        Expect(profile.valid, "PRE profile should be valid", failures);
        Expect(HasAllSlots(profile.disabledMixingSlots, {6, 7, 8, 9, 10}),
               "PRE profile should disable slots 6..10",
               failures);
    }

    {
        const auto profile = SearchAnalysis::CompileWorldEnvelopeProfile(settings, 28, 0);
        Expect(profile.valid, "PRE-C profile should be valid", failures);
        Expect(Contains(profile.possibleGeyserTypes, "molten_iron"),
               "PRE-C profile should include GeoActive generic metal geysers",
               failures);
        Expect(Contains(profile.possibleGeyserTypes, "small_volcano"),
               "PRE-C profile should include GeoActive generic volcano geysers",
               failures);
        Expect(Contains(profile.possibleGeyserTypes, "liquid_sulfur"),
               "PRE-C profile should include SpaceOut generic-only sulfur geyser",
               failures);
        Expect(!Contains(profile.impossibleGeyserTypes, "molten_iron"),
               "PRE-C profile should not mark GeoActive metal geysers impossible",
               failures);
    }

    {
        const auto profile = SearchAnalysis::CompileWorldEnvelopeProfile(settings, 29, 0);
        Expect(profile.valid, "CER-C profile should be valid", failures);
        Expect(Contains(profile.possibleGeyserTypes, "big_volcano"),
               "CER-C profile should include Volcanoes trait volcanoes",
               failures);
        Expect(Contains(profile.possibleGeyserTypes, "molten_gold"),
               "CER-C profile should include GeoActive generic metals",
               failures);
    }

    {
        const auto profile = SearchAnalysis::CompileWorldEnvelopeProfile(settings, 34, 0);
        Expect(profile.valid, "M-FRZ-C profile should be valid", failures);
        Expect(Contains(profile.possibleGeyserTypes, "big_volcano"),
               "M-FRZ-C profile should include Volcanoes trait volcanoes",
               failures);
        Expect(Contains(profile.impossibleGeyserTypes, "molten_iron"),
               "M-FRZ-C profile should still keep generic metals impossible",
               failures);
    }

    if (failures == 0) {
        std::cout << "[PASS] test_world_envelope_profile" << std::endl;
        return 0;
    }
    return 1;
}
