#include "SearchAnalysis/WorldEnvelopeProfile.hpp"
#include "SearchAnalysis/SearchCatalog.hpp"
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

int FindWorldTypeByPrefix(const std::string &prefixWithDash)
{
    const auto &prefixes = SearchAnalysis::GetWorldPrefixes();
    const auto itr = std::find(prefixes.begin(), prefixes.end(), prefixWithDash);
    if (itr == prefixes.end()) {
        return -1;
    }
    return static_cast<int>(std::distance(prefixes.begin(), itr));
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
        SettingsCache isolated;
        isolated.seed = 246810;
        isolated.traits["traits/FixedCore"] = WorldTrait{
            .filePath = "traits/FixedCore",
            .name = "Fixed Core",
            .exclusiveWith = {"traits/Candidate"},
        };
        isolated.traits["traits/Candidate"] = WorldTrait{
            .filePath = "traits/Candidate",
            .name = "Candidate",
        };

        World world;
        world.name = "Synthetic Fixed Trait Conflict";
        world.fixedTraits = {"traits/FixedCore"};
        world.worldTraitRules.push_back(TraitRule{
            .min = 1,
            .max = 1,
        });

        const auto traits = isolated.GetRandomTraits(world);
        Expect(traits.empty(),
               "fixed traits should exclude mutually exclusive random traits",
               failures);
    }

    {
        SettingsCache profileSettings = settings;
        constexpr const char *kClassicPrefix = "V-SNDST-C-";
        const int worldType = FindWorldTypeByPrefix(kClassicPrefix);
        Expect(worldType >= 0,
               "fixed-trait profile test should resolve classic world prefix",
               failures);

        auto clusterItr = std::ranges::find_if(
            profileSettings.clusters,
            [](const auto &entry) { return entry.second.coordinatePrefix == "V-SNDST-C"; });
        Expect(clusterItr != profileSettings.clusters.end(),
               "fixed-trait profile test should find classic cluster by prefix",
               failures);

        if (worldType >= 0 && clusterItr != profileSettings.clusters.end() &&
            !clusterItr->second.worldPlacements.empty()) {
            auto &cluster = clusterItr->second;
            const std::string originalWorldId = cluster.worldPlacements.front().world;
            auto worldItr = profileSettings.worlds.find(originalWorldId);
            Expect(worldItr != profileSettings.worlds.end(),
                   "fixed-trait profile test should find original cluster world",
                   failures);

            if (worldItr != profileSettings.worlds.end()) {
                auto syntheticWorld = worldItr->second;
                syntheticWorld.name = "Synthetic Profile Fixed Trait Conflict";
                syntheticWorld.fixedTraits = {"traits/FixedCore"};
                syntheticWorld.worldTraitRules.clear();
                syntheticWorld.worldTraitRules.push_back(TraitRule{
                    .min = 1,
                    .max = 1,
                });
                profileSettings.worlds["synthetic/worlds/FixedTraitConflictProfile"] =
                    syntheticWorld;
                cluster.worldPlacements.front().world =
                    "synthetic/worlds/FixedTraitConflictProfile";

                profileSettings.traits["traits/FixedCore"] = WorldTrait{
                    .filePath = "traits/FixedCore",
                    .name = "Fixed Core",
                    .exclusiveWith = {"traits/Candidate"},
                };
                profileSettings.traits["traits/Candidate"] = WorldTrait{
                    .filePath = "traits/Candidate",
                    .name = "Candidate",
                };

                std::string error;
                const auto profile = SearchAnalysis::CompileWorldEnvelopeProfile(
                    profileSettings, worldType, 0, &error);
                Expect(error.empty(),
                       "fixed-trait profile compile should not return error",
                       failures);
                Expect(profile.valid,
                       "fixed-trait profile compile should stay valid",
                       failures);
                Expect(!Contains(profile.possibleTraitIds, "traits/Candidate"),
                       "profile possibleTraitIds should exclude traits blocked by fixed traits",
                       failures);
                Expect(Contains(profile.impossibleTraitIds, "traits/Candidate"),
                       "profile impossibleTraitIds should include traits blocked by fixed traits",
                       failures);
            }
        }
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

    {
        const auto profile = SearchAnalysis::CompileWorldEnvelopeProfile(settings, 38, 0);
        Expect(profile.valid, "AQU-A profile should be valid", failures);
        Expect(Contains(profile.possibleGeyserTypes, "murky_brine"),
               "AQU-A profile should expose murky_brine in possible geyser types",
               failures);
        Expect(Contains(profile.possibleGeyserTypes, "small_reef_geyser"),
               "AQU-A profile should expose small_reef_geyser in possible geyser types",
               failures);
        Expect(Contains(profile.possibleGeyserTypes, "underwater_vent"),
               "AQU-A profile should expose underwater_vent in possible geyser types",
               failures);
    }

    if (failures == 0) {
        std::cout << "[PASS] test_world_envelope_profile" << std::endl;
        return 0;
    }
    return 1;
}
