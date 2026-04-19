#include "SearchAnalysis/WorldEnvelopeProfile.hpp"
#include "Setting/SettingsCache.hpp"
#include "config.h"

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
    std::ifstream file(SETTING_ASSET_FILEPATH, std::ios::binary);
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

    if (failures == 0) {
        std::cout << "[PASS] test_world_envelope_profile" << std::endl;
        return 0;
    }
    return 1;
}
