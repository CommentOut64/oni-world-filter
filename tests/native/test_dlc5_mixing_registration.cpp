#include "Setting/SettingsCache.hpp"
#include "Setting/WorldEffectiveState.hpp"
#include "SearchAnalysis/SearchCatalog.hpp"
#include "config.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <ranges>
#include <sstream>
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

bool ReadAssetBlob(std::vector<char> &data, std::string *error)
{
    std::ifstream file(SETTING_TEST_ASSET_FILEPATH, std::ios::binary);
    if (!file.is_open()) {
        if (error != nullptr) {
            *error = "failed to open asset blob";
        }
        return false;
    }
    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    if (size <= 0) {
        if (error != nullptr) {
            *error = "asset blob size is invalid";
        }
        return false;
    }
    data.resize(static_cast<size_t>(size));
    if (!file.read(data.data(), size)) {
        if (error != nullptr) {
            *error = "failed to read asset blob";
        }
        return false;
    }
    return true;
}

bool LoadFreshSettings(SettingsCache &settings, std::string *error)
{
    std::vector<char> data;
    if (!ReadAssetBlob(data, error)) {
        return false;
    }
    const std::string_view content(data.data(), data.size());
    if (!settings.LoadSettingsCache(content)) {
        if (error != nullptr) {
            *error = "failed to parse settings cache from blob";
        }
        return false;
    }
    return true;
}

std::string BuildCoordinateCode(const std::string &prefix, int seed, int mixing)
{
    std::ostringstream builder;
    builder << prefix;
    if (!prefix.empty() && prefix.back() != '-') {
        builder << '-';
    }
    builder << seed << "-0-D3-" << SettingsCache::BinaryToBase36(static_cast<uint32_t>(mixing));
    return builder.str();
}

bool BuildEffectiveStates(SettingsCache &settings, std::vector<WorldEffectiveState> &states)
{
    std::vector<ResolvedWorldPlacement> placements;
    std::string error;
    if (!BuildResolvedWorldPlacements(settings, &placements, &error)) {
        std::cerr << "[FAIL] BuildResolvedWorldPlacements failed: " << error << std::endl;
        return false;
    }
    if (!InitializeWorldEffectiveStates(settings, placements, &states, &error)) {
        std::cerr << "[FAIL] InitializeWorldEffectiveStates failed: " << error << std::endl;
        return false;
    }
    ApplySubworldMixingToWorldEffectiveStates(settings, states);
    return true;
}

bool WorldRuntimeHasMixingProxyNames(const World &world)
{
    for (const auto *filter : world.unknownCellsAllowedSubworlds2) {
        if (filter == nullptr) {
            continue;
        }
        for (const auto &subworldName : filter->subworldNames) {
            if (!subworldName.empty() && subworldName.front() == '(') {
                std::cerr << "[TRACE] unresolved proxy subworld: " << subworldName << std::endl;
                return true;
            }
        }
    }
    return false;
}

} // namespace

int RunAllTests()
{
    int failures = 0;

    SettingsCache settings;
    std::string error;
    Expect(LoadFreshSettings(settings, &error),
           "fresh settings cache should load for DLC5 mixing registration test",
           failures);

    if (error.empty()) {
        const auto hasMixConfigPath = [&settings](std::string_view path, int expectedType) {
            return std::ranges::any_of(
                settings.mixConfigs,
                [path, expectedType](const MixingConfig &config) {
                    return config.path == path && config.type == expectedType;
                });
        };

        Expect(hasMixConfigPath("DLC5_ID", 0),
               "mix configs should register DLC5 dlc mixing slot",
               failures);
        Expect(hasMixConfigPath("dlc5::worldMixing/AquaticMixingSettings", 1),
               "mix configs should register DLC5 aquatic world mixing setting",
               failures);
        Expect(hasMixConfigPath("dlc5::subworldMixing/BeachMixingSettings", 2),
               "mix configs should register DLC5 beach subworld mixing setting",
               failures);
        Expect(hasMixConfigPath("dlc5::subworldMixing/ReefMixingSettings", 2),
               "mix configs should register DLC5 reef subworld mixing setting",
               failures);
        Expect(hasMixConfigPath("dlc5::subworldMixing/KelpForestMixingSettings", 2),
               "mix configs should register DLC5 kelp forest subworld mixing setting",
               failures);
        Expect(hasMixConfigPath("dlc5::subworldMixing/AbyssMixingSettings", 2),
               "mix configs should register DLC5 abyss subworld mixing setting",
               failures);
        Expect(settings.subworlds.contains("subworlds/ocean/OceanDeepSlush"),
               "settings cache should load shared OceanDeepSlush subworld required by DLC5 aquatic worlds",
               failures);

        const std::string code = BuildCoordinateCode("AQU-A-", 100123, 0);
        Expect(settings.CoordinateChanged(code, settings),
               "AQU-A coordinate should resolve for DLC5 mixing registration test",
               failures);

        std::vector<WorldEffectiveState> states;
        Expect(BuildEffectiveStates(settings, states),
               "AQU-A effective states should build for DLC5 mixing registration test",
               failures);

        bool validatedAquaticStartWorld = false;
        for (const auto &state : states) {
            if (state.world.locationType != LocationType::StartWorld) {
                continue;
            }
            validatedAquaticStartWorld = true;
            Expect(!WorldRuntimeHasMixingProxyNames(state.world),
                   "AQU-A start world should not retain unresolved DLC5 mixing proxy names after effective-state mixing",
                   failures);
        }
        Expect(validatedAquaticStartWorld,
               "AQU-A effective states should include a start world for DLC5 mixing registration test",
               failures);
    }

    return failures;
}
