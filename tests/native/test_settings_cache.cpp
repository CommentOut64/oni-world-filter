#include "Setting/SettingsCache.hpp"
#include "Setting/NativeCoordinate.hpp"
#include "Setting/WorldEffectiveState.hpp"
#include "WorldGen.hpp"
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

bool BuildEffectiveStates(SettingsCache &settings, std::vector<WorldEffectiveState> &states)
{
    std::vector<ResolvedWorldPlacement> placements;
    std::string error;
    if (!BuildResolvedWorldPlacements(settings, &placements, &error)) {
        return false;
    }
    if (!InitializeWorldEffectiveStates(settings, placements, &states, &error)) {
        return false;
    }
    ApplySubworldMixingToWorldEffectiveStates(settings, states);
    return true;
}

bool LeafSitesCarryDistanceTags(const std::vector<Site> &sites)
{
    bool foundLeaf = false;
    for (const auto &site : sites) {
        if (!site.children) {
            continue;
        }
        for (const auto &child : *site.children) {
            foundLeaf = true;
            if (child.minDistanceToTag.empty()) {
                return false;
            }
        }
    }
    return foundLeaf;
}

std::vector<std::string> BuildActiveWorldNames(SettingsCache &settings)
{
    std::vector<std::string> names;
    if (settings.cluster == nullptr) {
        return names;
    }
    names.reserve(settings.cluster->worldPlacements.size());
    for (const auto &placement : settings.cluster->worldPlacements) {
        names.push_back(placement.world);
    }
    std::vector<ResolvedWorldPlacement> placements;
    std::string error;
    if (!BuildResolvedWorldPlacements(settings, &placements, &error)) {
        return names;
    }
    for (const auto &resolved : placements) {
        if (resolved.placement != nullptr &&
            resolved.worldAssetId != resolved.placement->world) {
            names.push_back(resolved.worldAssetId);
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

bool OrderedSubworldPointersBindToLocalCopy(const SettingsCache &settings)
{
    for (const auto &[key, ordered] : settings.orderedSubworlds) {
        (void)key;
        for (const auto *subworld : ordered) {
            if (subworld == nullptr) {
                return false;
            }
            const auto itr = settings.subworlds.find(subworld->name);
            if (itr == settings.subworlds.end()) {
                return false;
            }
            if (subworld != &itr->second) {
                return false;
            }
        }
    }
    return true;
}

bool WorldRuntimeBasePointersBindToLocalCopy(const World &world)
{
    if (world.subworldFiles.size() != world.subworldFiles2.size()) {
        return false;
    }
    for (size_t index = 0; index < world.subworldFiles.size(); ++index) {
        if (world.subworldFiles2[index] != &world.subworldFiles[index]) {
            return false;
        }
    }

    if (world.unknownCellsAllowedSubworlds.size() != world.unknownCellsAllowedSubworlds2.size()) {
        return false;
    }
    for (size_t index = 0; index < world.unknownCellsAllowedSubworlds.size(); ++index) {
        if (world.unknownCellsAllowedSubworlds2[index] != &world.unknownCellsAllowedSubworlds[index]) {
            return false;
        }
    }

    if (world.worldTemplateRules.size() != world.worldTemplateRules2.size()) {
        return false;
    }
    for (size_t index = 0; index < world.worldTemplateRules.size(); ++index) {
        if (world.worldTemplateRules2[index] != &world.worldTemplateRules[index]) {
            return false;
        }
    }

    return world.globalFeatures2.empty() && world.mixingSubworlds.empty();
}

bool CopiedWorldRuntimePointersBindToLocalCopy(const SettingsCache &settings)
{
    for (const auto &[name, world] : settings.worlds) {
        (void)name;
        if (!WorldRuntimeBasePointersBindToLocalCopy(world)) {
            return false;
        }
    }
    return true;
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
    std::vector<WorldEffectiveState> states;
    if (BuildEffectiveStates(settings, states)) {
        for (const auto &state : states) {
            builder << state.worldAssetId << '=' << FingerprintWorldRuntime(state.world) << ';';
        }
    }
    return builder.str();
}

bool WorldRuntimeContainsTemplateName(const World &world, const std::string &templateName)
{
    for (const auto *rule : world.worldTemplateRules2) {
        if (rule == nullptr) {
            continue;
        }
        for (const auto &name : rule->names) {
            if (name == templateName) {
                return true;
            }
        }
    }
    return false;
}

bool WorldRuntimeHasMixingProxyNames(const World &world)
{
    for (const auto *filter : world.unknownCellsAllowedSubworlds2) {
        if (filter == nullptr) {
            continue;
        }
        for (const auto &subworldName : filter->subworldNames) {
            if (!subworldName.empty() && subworldName.front() == '(') {
                return true;
            }
        }
    }
    return false;
}

bool ApplySearchMutablePath(SettingsCache &settings, const std::string &code)
{
    if (!settings.CoordinateChanged(code, settings)) {
        return false;
    }
    std::vector<WorldEffectiveState> states;
    if (!BuildEffectiveStates(settings, states) || states.empty()) {
        return false;
    }
    const int baseSeed = settings.seed;
    for (auto &state : states) {
        auto *world = &state.world;
        if (world->locationType == LocationType::Cluster) {
            continue;
        }
        settings.seed = baseSeed + state.placementIndex;
        const auto traits = settings.GetRandomTraits(*world);
        for (const auto *trait : traits) {
            world->ApplayTraits(*trait, settings);
        }
        break;
    }
    return true;
}

std::vector<const WorldTrait *> GetStartWorldTraitsForCode(SettingsCache &settings,
                                                           const std::string &code)
{
    if (!settings.CoordinateChanged(code, settings)) {
        return {};
    }
    std::vector<WorldEffectiveState> states;
    if (!BuildEffectiveStates(settings, states) || states.empty()) {
        return {};
    }
    const int baseSeed = settings.seed;
    for (const auto &state : states) {
        auto *world = const_cast<World *>(&state.world);
        if (world == nullptr || world->locationType != LocationType::StartWorld) {
            continue;
        }
        settings.seed = baseSeed + state.placementIndex;
        return settings.GetRandomTraits(*world);
    }
    return {};
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
            Expect(settings.IsContentEnabled("EXPANSION1_ID"),
                   "selected expansion cluster should expose EXPANSION1_ID via active content set",
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
        Expect(LoadFreshSettings(settings, &error),
               "fresh settings cache should load for mixing sanitization checks",
               failures);

        if (error.empty()) {
            const std::string code = BuildCoordinateCode("PRE-C-", 123456, 1);
            Expect(settings.CoordinateChanged(code, settings),
                   "PRE-C coordinate should resolve for mixing sanitization checks",
                   failures);
            if (!settings.mixConfigs.empty()) {
                Expect(settings.mixConfigs[0].level == MixingLevel::Disabled,
                       "PRE cluster should sanitize DLC2 mixing slot to disabled when unsupported",
                       failures);
            }
        }
    }

    {
        SettingsCache settings;
        std::string error;
        Expect(LoadFreshSettings(settings, &error),
               "fresh settings cache should load for prehistoric world mixing checks",
               failures);

        if (error.empty()) {
            const std::string baseCode = BuildCoordinateCode("SNDST-C-", 123456, 0);
            const std::string mixedCode = BuildCoordinateCode("SNDST-C-", 123456, 626);
            std::vector<ResolvedWorldPlacement> basePlacements;
            std::vector<ResolvedWorldPlacement> mixedPlacements;

            Expect(settings.CoordinateChanged(baseCode, settings),
                   "SNDST-C base coordinate should resolve",
                   failures);
            Expect(BuildResolvedWorldPlacements(settings, &basePlacements, &error),
                   "SNDST-C base resolved placements should build",
                   failures);

            bool hasBasePrehistoricMixingWorld = false;
            for (const auto &placement : basePlacements) {
                if (placement.worldAssetId == "dlc4::worlds/MixingPrehistoricAsteroid") {
                    hasBasePrehistoricMixingWorld = true;
                    break;
                }
            }
            Expect(!hasBasePrehistoricMixingWorld,
                   "SNDST-C without DLC4 mixing should not replace any placement with MixingPrehistoricAsteroid",
                   failures);

            Expect(settings.CoordinateChanged(mixedCode, settings),
                   "SNDST-C mixed coordinate should resolve",
                   failures);
            Expect(BuildResolvedWorldPlacements(settings, &mixedPlacements, &error),
                   "SNDST-C mixed resolved placements should build",
                   failures);

            bool hasMixedPrehistoricWorld = false;
            for (const auto &placement : mixedPlacements) {
                if (placement.worldAssetId == "dlc4::worlds/MixingPrehistoricAsteroid") {
                    hasMixedPrehistoricWorld = true;
                    break;
                }
            }
            Expect(hasMixedPrehistoricWorld,
                   "SNDST-C with DLC4 world mixing should replace at least one placement with MixingPrehistoricAsteroid",
                   failures);

            std::vector<std::string> expectedPlacementMixingTemplateNames;
            int mixedPrehistoricPlacementIndex = -1;
            for (const auto &placement : mixedPlacements) {
                if (placement.worldAssetId != "dlc4::worlds/MixingPrehistoricAsteroid" ||
                    placement.placement == nullptr ||
                    placement.appliedWorldMixingSetting == nullptr) {
                    continue;
                }
                for (const auto &rule : placement.placement->worldMixing.additionalWorldTemplateRules) {
                    for (const auto &name : rule.names) {
                        expectedPlacementMixingTemplateNames.push_back(name);
                    }
                }
                if (!expectedPlacementMixingTemplateNames.empty()) {
                    mixedPrehistoricPlacementIndex = placement.placementIndex;
                    break;
                }
            }
            Expect(mixedPrehistoricPlacementIndex >= 0,
                   "SNDST-C mixed prehistoric placement should keep explicit placement world mixing rules",
                   failures);

            std::vector<WorldEffectiveState> mixedStates;
            Expect(BuildEffectiveStates(settings, mixedStates),
                   "SNDST-C mixed effective world states should build",
                   failures);

            bool validatedMixedStartWorldCleanup = false;
            for (const auto &state : mixedStates) {
                if (state.world.locationType != LocationType::StartWorld) {
                    continue;
                }
                validatedMixedStartWorldCleanup = true;
                Expect(!WorldRuntimeHasMixingProxyNames(state.world),
                       "SNDST-C mixed start world should remove leftover mixing proxy names after subworld mixing",
                       failures);
            }
            Expect(validatedMixedStartWorldCleanup,
                   "SNDST-C mixed effective states should include a start world for proxy cleanup checks",
                   failures);

            if (mixedPrehistoricPlacementIndex >= 0) {
                const auto *mixedState = FindWorldEffectiveState(mixedStates, mixedPrehistoricPlacementIndex);
                Expect(mixedState != nullptr,
                       "SNDST-C mixed prehistoric placement should resolve to an effective world state",
                       failures);
                if (mixedState != nullptr) {
                    for (const auto &templateName : expectedPlacementMixingTemplateNames) {
                        Expect(WorldRuntimeContainsTemplateName(mixedState->world, templateName),
                               ("SNDST-C mixed prehistoric runtime should preserve placement world mixing template " +
                                templateName)
                                   .c_str(),
                               failures);
                    }

                    World runtimeWorld = mixedState->world;
                    const int baseSeed = settings.seed;
                    settings.seed = baseSeed + mixedPrehistoricPlacementIndex;
                    const auto traits = settings.GetRandomTraits(runtimeWorld);
                    for (const auto *trait : traits) {
                        runtimeWorld.ApplayTraits(*trait, settings);
                    }

                    WorldGen worldGen(runtimeWorld, settings);
                    std::vector<Site> generatedSites;
                    Expect(worldGen.GenerateOverworld(generatedSites),
                           "SNDST-C mixed prehistoric world should generate for leaf distance checks",
                           failures);
                    if (!generatedSites.empty()) {
                        Expect(LeafSitesCarryDistanceTags(generatedSites),
                               "SNDST-C mixed prehistoric generated leaf sites should carry DistanceToTag data",
                               failures);
                    }
                }
            }
        }
    }

    {
        SettingsCache settings;
        std::string error;
        Expect(LoadFreshSettings(settings, &error),
               "fresh settings cache should load for primary trait regression checks",
               failures);

        if (error.empty()) {
            const auto vanillaSandstoneTraits =
                GetStartWorldTraitsForCode(settings, BuildCoordinateCode("V-SNDST-C-", 123456, 0));
            Expect(vanillaSandstoneTraits.empty(),
                   "V-SNDST-C primary should not generate runtime traits",
                   failures);

            const auto terraMoonletTraits =
                GetStartWorldTraitsForCode(settings, BuildCoordinateCode("SNDST-C-", 123456, 0));
            Expect(terraMoonletTraits.empty(),
                   "SNDST-C primary should not generate runtime traits",
                   failures);

            const auto vanillaArboriaTraits =
                GetStartWorldTraitsForCode(settings, BuildCoordinateCode("V-LUSH-C-", 123456, 0));
            Expect(!vanillaArboriaTraits.empty(),
                   "classic primary trait generation should stay enabled on non-Terra worlds",
                   failures);
        }
    }

    {
        SettingsCache settings;
        std::string error;
        Expect(LoadFreshSettings(settings, &error),
               "fresh settings cache should load for copy isolation test",
               failures);

        if (settings.defaults.data.empty()) {
            ++failures;
            std::cerr << "[FAIL] copy isolation fixture should load non-empty defaults" << std::endl;
        } else {
            SettingsCache copied = settings;
            Expect(OrderedSubworldPointersBindToLocalCopy(copied),
                   "copied settings should rebind orderedSubworlds to the copied subworld map",
                   failures);
            Expect(CopiedWorldRuntimePointersBindToLocalCopy(copied),
                   "copied settings should rebuild world runtime pointer caches against copied storage",
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

    {
        NativeCoordinate::NativeCoordinateResolution nativeCoord;
        Expect(NativeCoordinate::ResolveNativeCoordinate("V-SNDST-C-1927980015-0-3A-0",
                                                         &nativeCoord),
               "native coord should resolve",
               failures);
        Expect(nativeCoord.worldType == 13,
               "native coord should resolve sandstone cluster worldType",
               failures);
        Expect(nativeCoord.seed == 1927980015,
               "native coord should preserve seed",
               failures);
        Expect(nativeCoord.mixing == 0,
               "native coord should decode zero mixing from trailing 0",
               failures);
    }

    {
        NativeCoordinate::NativeCoordinateResolution nativeCoord;
        Expect(NativeCoordinate::ResolveNativeCoordinate("V-SNDST-C-123456-0-D3-HD",
                                                         &nativeCoord),
               "short non-zero trailing mixing code should resolve",
               failures);
        Expect(nativeCoord.mixing == static_cast<int>(SettingsCache::Base36ToBinary("HD")),
               "short non-zero trailing mixing code should decode mixing value",
               failures);
    }

    {
        NativeCoordinate::NativeCoordinateResolution invalidCoord;
        Expect(!NativeCoordinate::ResolveNativeCoordinate("V-SNDST-C-123456-0-D3-ABCDE1",
                                                          &invalidCoord),
               "non-zero trailing mixing code longer than five chars should be rejected",
               failures);
    }

    {
        NativeCoordinate::NativeCoordinateResolution invalidCoord;
        Expect(!NativeCoordinate::ResolveNativeCoordinate("V-SNDST-C-123456-0-D3-ZZZZZ",
                                                          &invalidCoord),
               "non-zero trailing mixing code outside mixing range should be rejected",
               failures);
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

