#include "Setting/SettingsCache.hpp"
#include "SearchAnalysis/SearchCatalog.hpp"
#include "config.h"

#include <atomic>
#include <chrono>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
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
    std::ifstream file(SETTING_ASSET_FILEPATH, std::ios::binary);
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

std::string FindExpansionWorldPrefix(const SettingsCache &settings)
{
    const auto &worldPrefixes = SearchAnalysis::GetWorldPrefixes();
    for (const auto &[key, cluster] : settings.clusters) {
        (void)key;
        if (cluster.coordinatePrefix.empty() || cluster.worldPlacements.empty()) {
            continue;
        }
        for (const auto &dlcId : cluster.requiredDlcIds) {
            if (dlcId == "EXPANSION1_ID") {
                const std::string expectedPrefix = cluster.coordinatePrefix + "-";
                for (const auto &worldPrefix : worldPrefixes) {
                    if (worldPrefix == expectedPrefix) {
                        return worldPrefix;
                    }
                }
            }
        }
    }
    return {};
}

std::vector<World *> BuildActiveWorlds(SettingsCache &settings)
{
    std::vector<World *> worlds;
    if (settings.cluster == nullptr) {
        return worlds;
    }
    for (auto &worldPlacement : settings.cluster->worldPlacements) {
        auto itr = settings.worlds.find(worldPlacement.world);
        if (itr == settings.worlds.end()) {
            continue;
        }
        itr->second.locationType = worldPlacement.locationType;
        worlds.push_back(&itr->second);
    }
    if (worlds.size() == 1) {
        worlds[0]->locationType = LocationType::StartWorld;
    }
    return worlds;
}

std::vector<std::string> BuildActiveWorldNames(SettingsCache &settings)
{
    std::vector<std::string> names;
    const auto worlds = BuildActiveWorlds(settings);
    names.reserve(worlds.size());
    for (const auto *world : worlds) {
        if (world != nullptr) {
            names.push_back(world->name);
        }
    }
    return names;
}

std::string FindInactiveWorldName(const SettingsCache &settings,
                                  const std::vector<std::string> &activeWorldNames)
{
    for (const auto &[name, world] : settings.worlds) {
        (void)world;
        if (std::find(activeWorldNames.begin(), activeWorldNames.end(), name) ==
            activeWorldNames.end()) {
            return name;
        }
    }
    return {};
}

std::string EncodeMinMax(const MinMax &value)
{
    std::ostringstream builder;
    builder << std::fixed << std::setprecision(3) << value.min << ',' << value.max;
    return builder.str();
}

std::string FingerprintWorldRuntime(const World &world)
{
    std::ostringstream builder;
    builder << static_cast<int>(world.locationType) << '|';
    builder << EncodeMinMax(world.startingPositionHorizontal2) << '|';
    builder << EncodeMinMax(world.startingPositionVertical2) << '|';
    builder << "features=";
    for (const auto *feature : world.globalFeatures2) {
        builder << feature->type << ',';
    }
    builder << "|subworlds=";
    for (const auto *subworld : world.subworldFiles2) {
        builder << subworld->name << ',';
    }
    builder << "|filters=";
    for (const auto *filter : world.unknownCellsAllowedSubworlds2) {
        builder << filter->tag << ':';
        for (const auto &subworldName : filter->subworldNames) {
            builder << subworldName << ',';
        }
        builder << ';';
    }
    builder << "|rules=";
    for (const auto *rule : world.worldTemplateRules2) {
        builder << rule->ruleId << ',';
    }
    builder << "|mixing=";
    for (const auto &mixingSubworld : world.mixingSubworlds) {
        builder << mixingSubworld.name << ':' << mixingSubworld.minCount << ':' << mixingSubworld.maxCount
                << ',';
    }
    return builder.str();
}

std::string FingerprintSnapshot(const SearchMutableStateSnapshot &snapshot)
{
    std::ostringstream builder;
    builder << "seed=" << snapshot.seed;
    builder << "|cluster=" << (snapshot.cluster != nullptr ? snapshot.cluster->coordinatePrefix : "null");
    builder << "|dlcState=" << snapshot.dlcState;
    builder << "|mixConfigs=";
    for (const auto &config : snapshot.mixConfigs) {
        builder << config.path << ':' << config.type << ':' << static_cast<int>(config.level) << ':'
                << config.minCount << ':' << config.maxCount << ':' << (config.setting != nullptr) << ';';
    }
    builder << "|activeWorlds=";
    for (const auto &name : snapshot.activeWorlds) {
        builder << name << ';';
    }
    return builder.str();
}

std::string FingerprintMutableState(SettingsCache &settings)
{
    std::ostringstream builder;
    builder << FingerprintSnapshot(settings.CaptureSearchMutableState());
    builder << "|activeRuntime=";
    for (const auto *world : BuildActiveWorlds(settings)) {
        if (world == nullptr) {
            continue;
        }
        builder << world->name << '=' << FingerprintWorldRuntime(*world) << ';';
    }
    return builder.str();
}

bool ApplySearchMutablePath(SettingsCache &settings, const std::string &code)
{
    if (!settings.CoordinateChanged(code, settings)) {
        return false;
    }
    auto worlds = BuildActiveWorlds(settings);
    if (worlds.empty()) {
        return false;
    }
    settings.DoSubworldMixing(worlds);
    for (auto *world : worlds) {
        if (world->locationType == LocationType::Cluster) {
            continue;
        }
        const auto traits = settings.GetRandomTraits(*world);
        for (const auto *trait : traits) {
            world->ApplayTraits(*trait, settings);
        }
        break;
    }
    return true;
}

} // namespace

int RunAllTests()
{
    int failures = 0;

    SharedSettingsCache::ResetForTests();

    {
        SettingsCache settings;
        std::string error;
        Expect(LoadFreshSettings(settings, &error), "fresh settings cache should load from asset blob", failures);
        const auto baseline = settings.CaptureSearchMutableState();
        const std::string baselineFingerprint = FingerprintSnapshot(baseline);
        const std::string baselineMutableFingerprint = FingerprintMutableState(settings);

        const auto expansionPrefix = FindExpansionWorldPrefix(settings);
        Expect(!expansionPrefix.empty(),
               "test fixture should find a world prefix that enables EXPANSION1_ID",
               failures);

        if (!expansionPrefix.empty()) {
            const std::string code = BuildCoordinateCode(expansionPrefix, 100123, 625);
            Expect(ApplySearchMutablePath(settings, code),
                   "search mutable path should apply coordinate and subworld mixing",
                   failures);
            Expect(settings.IsSpaceOutEnabled(),
                   "selected expansion cluster should enable space-out dlc state",
                   failures);

            const std::string mutatedFingerprint = FingerprintMutableState(settings);
            Expect(mutatedFingerprint != baselineFingerprint,
                   "mutated search state should differ from baseline snapshot",
                   failures);

            settings.RestoreSearchMutableState(baseline);
            const std::string restoredFingerprint = FingerprintMutableState(settings);
            Expect(restoredFingerprint == baselineMutableFingerprint,
                   "restored search state should return to baseline snapshot",
                   failures);
            Expect(!settings.IsSpaceOutEnabled(),
                   "restored snapshot should also restore dlc state",
                   failures);

            Expect(ApplySearchMutablePath(settings, code),
                   "restored state should support reapplying the same coordinate",
                   failures);
            const std::string replayFingerprint = FingerprintMutableState(settings);
            Expect(replayFingerprint == mutatedFingerprint,
                   "restored state should reproduce the same mutable search runtime state",
                   failures);
        }
    }

    {
        SettingsCache settings;
        std::string error;
        Expect(LoadFreshSettings(settings, &error), "fresh settings cache should load for active-world restore test", failures);

        const auto expansionPrefix = FindExpansionWorldPrefix(settings);
        Expect(!expansionPrefix.empty(),
               "active-world restore fixture should find expansion world prefix",
               failures);

        if (!expansionPrefix.empty()) {
            const std::string code = BuildCoordinateCode(expansionPrefix, 100123, 625);
            Expect(settings.CoordinateChanged(code, settings),
                   "coordinate should resolve before capturing active-world baseline",
                   failures);
            const auto baseline = settings.CaptureSearchMutableState();
            const auto activeWorldNames = BuildActiveWorldNames(settings);
            Expect(!activeWorldNames.empty(),
                   "active-world restore fixture should expose active worlds",
                   failures);

            const std::string inactiveWorldName = FindInactiveWorldName(settings, activeWorldNames);
            Expect(!inactiveWorldName.empty(),
                   "fixture should expose at least one inactive world",
                   failures);

            if (!inactiveWorldName.empty()) {
                auto inactiveItr = settings.worlds.find(inactiveWorldName);
                Expect(inactiveItr != settings.worlds.end(),
                       "inactive world should exist in world map",
                       failures);
                if (inactiveItr != settings.worlds.end()) {
                    World &inactiveWorld = inactiveItr->second;
                    inactiveWorld.startingPositionHorizontal2 = {7.0f, 8.0f};
                    inactiveWorld.startingPositionVertical2 = {9.0f, 10.0f};
                    inactiveWorld.subworldFiles2.clear();
                    inactiveWorld.globalFeatures2.clear();
                    inactiveWorld.unknownCellsAllowedSubworlds2.clear();
                    inactiveWorld.worldTemplateRules2.clear();
                    inactiveWorld.mixingSubworlds.clear();
                    inactiveWorld.mixingSubworlds.push_back(WeightedSubworldName{
                        .name = "synthetic/inactive-world-marker",
                    });

                    const std::string mutatedInactiveFingerprint =
                        FingerprintWorldRuntime(inactiveWorld);
                    settings.RestoreSearchMutableState(baseline);
                    const std::string restoredInactiveFingerprint =
                        FingerprintWorldRuntime(settings.worlds.at(inactiveWorldName));

                    Expect(restoredInactiveFingerprint == mutatedInactiveFingerprint,
                           "active-world restore should not rewrite inactive worlds",
                           failures);
                }
            }
        }
    }

    std::atomic<int> loadCalls{0};
    std::vector<std::shared_ptr<const SettingsCache>> snapshots;
    snapshots.reserve(8);
    std::mutex snapshotMutex;
    std::atomic<int> workerFailures{0};

    auto loader = [&](std::vector<char> &data, std::string *error) {
        loadCalls.fetch_add(1, std::memory_order_relaxed);
        return ReadAssetBlob(data, error);
    };

    std::vector<std::thread> workers;
    workers.reserve(8);
    for (int i = 0; i < 8; ++i) {
        workers.emplace_back([&] {
            std::string error;
            auto snapshot = SharedSettingsCache::GetOrCreate(loader, &error);
            if (!snapshot || !error.empty()) {
                workerFailures.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            if (snapshot->defaults.data.empty()) {
                workerFailures.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            std::lock_guard<std::mutex> lock(snapshotMutex);
            snapshots.push_back(std::move(snapshot));
        });
    }
    for (auto &thread : workers) {
        thread.join();
    }

    Expect(workerFailures.load(std::memory_order_relaxed) == 0,
           "concurrent shared cache init should not fail",
           failures);
    Expect(!snapshots.empty(), "snapshots should not be empty", failures);
    Expect(loadCalls.load(std::memory_order_relaxed) == 1,
           "shared cache loader should run exactly once",
           failures);
    if (!snapshots.empty()) {
        const auto *first = snapshots.front().get();
        bool allSame = true;
        for (const auto &item : snapshots) {
            if (item.get() != first) {
                allSame = false;
                break;
            }
        }
        Expect(allSame, "all snapshot pointers should be identical", failures);
    }

    SharedSettingsCache::ResetForTests();

    if (failures == 0) {
        std::cout << "[PASS] test_settings_cache" << std::endl;
        return 0;
    }
    return 1;
}

