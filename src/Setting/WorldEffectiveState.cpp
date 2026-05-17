#include "Setting/WorldEffectiveState.hpp"

#include <algorithm>
#include <ranges>

#include "Setting/SettingsCache.hpp"
#include "Utils/KRandom.hpp"
#include "Utils/PointGenerator.hpp"

namespace {

bool MatchesWorldMixingTags(const World &world, const WorldMixing &mixing)
{
    if (!mixing.requiredTags.empty() &&
        !std::ranges::all_of(mixing.requiredTags, [&world](const std::string &tag) {
            return std::ranges::contains(world.worldTags, tag);
        })) {
        return false;
    }
    if (!mixing.forbiddenTags.empty() &&
        std::ranges::any_of(mixing.forbiddenTags, [&world](const std::string &tag) {
            return std::ranges::contains(world.worldTags, tag);
        })) {
        return false;
    }
    return true;
}

MixingConfig *FindWorldMixingForPlacement(const WorldPlacement &placement,
                                          const SettingsCache &settings,
                                          std::vector<MixingConfig *> &configs)
{
    std::vector<MixingConfig *> sorted = configs;
    std::sort(sorted.begin(), sorted.end(), [](const MixingConfig *lhs, const MixingConfig *rhs) {
        if (lhs == nullptr || rhs == nullptr) {
            return lhs != nullptr;
        }
        if (rhs->minCount != lhs->minCount) {
            return rhs->minCount < lhs->minCount;
        }
        return rhs->maxCount < lhs->maxCount;
    });
    for (auto *config : sorted) {
        if (config == nullptr || config->maxCount <= 0) {
            continue;
        }
        auto *setting = static_cast<WorldMixingSettings *>(config->setting);
        if (setting == nullptr) {
            continue;
        }
        const auto worldItr = settings.worlds.find(setting->world);
        if (worldItr == settings.worlds.end()) {
            continue;
        }
        if (!MatchesWorldMixingTags(worldItr->second, placement.worldMixing)) {
            continue;
        }
        return config;
    }
    return nullptr;
}

} // namespace

bool BuildResolvedWorldPlacements(SettingsCache &settings,
                                  std::vector<ResolvedWorldPlacement> *placements,
                                  std::string *errorMessage)
{
    if (placements == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "resolved placement output is null";
        }
        return false;
    }
    placements->clear();
    if (settings.cluster == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "cluster is null before resolving world placements";
        }
        return false;
    }

    placements->reserve(settings.cluster->worldPlacements.size());
    for (size_t index = 0; index < settings.cluster->worldPlacements.size(); ++index) {
        auto &placement = settings.cluster->worldPlacements[index];
        const auto worldItr = settings.worlds.find(placement.world);
        if (worldItr == settings.worlds.end()) {
            if (errorMessage != nullptr) {
                *errorMessage = "world placement target is missing";
            }
            return false;
        }
        worldItr->second.locationType = placement.locationType;
        placements->push_back(ResolvedWorldPlacement{
            .placementIndex = static_cast<int>(index),
            .placement = &placement,
            .sourceWorld = &worldItr->second,
            .worldAssetId = placement.world,
        });
    }

    if (placements->size() == 1 && placements->front().sourceWorld != nullptr) {
        placements->front().sourceWorld->locationType = LocationType::StartWorld;
    }

    std::vector<MixingConfig *> worldMixingConfigs;
    for (auto &config : settings.mixConfigs) {
        if (config.level == MixingLevel::Disabled || config.type != 1) {
            continue;
        }
        auto itr = settings.worldMixing.find(config.path);
        if (itr == settings.worldMixing.end()) {
            continue;
        }
        config.setting = &itr->second;
        worldMixingConfigs.push_back(&config);
    }
    if (worldMixingConfigs.empty()) {
        return true;
    }

    std::vector<int> candidateIndexes;
    candidateIndexes.reserve(placements->size());
    for (const auto &placement : *placements) {
        if (placement.placement != nullptr && placement.placement->IsMixingPlacement()) {
            candidateIndexes.push_back(placement.placementIndex);
        }
    }
    if (candidateIndexes.empty()) {
        return true;
    }

    KRandom random(settings.seed);
    ShuffleSeeded(candidateIndexes, random);
    for (const int placementIndex : candidateIndexes) {
        if (worldMixingConfigs.empty()) {
            break;
        }
        auto &resolved = (*placements)[static_cast<size_t>(placementIndex)];
        if (resolved.placement == nullptr) {
            continue;
        }
        ShuffleSeeded(worldMixingConfigs, random);
        MixingConfig *config =
            FindWorldMixingForPlacement(*resolved.placement, settings, worldMixingConfigs);
        if (config == nullptr) {
            continue;
        }
        auto *setting = static_cast<WorldMixingSettings *>(config->setting);
        if (setting == nullptr) {
            continue;
        }
        const auto worldItr = settings.worlds.find(setting->world);
        if (worldItr == settings.worlds.end()) {
            if (errorMessage != nullptr) {
                *errorMessage = "world mixing target is missing";
            }
            return false;
        }
        worldItr->second.locationType = resolved.placement->locationType;
        resolved.sourceWorld = &worldItr->second;
        resolved.appliedWorldMixingSetting = setting;
        resolved.worldAssetId = setting->world;

        config->maxCount--;
        config->minCount--;
        if (config->maxCount <= 0) {
            const auto itr = std::ranges::find(worldMixingConfigs, config);
            if (itr != worldMixingConfigs.end()) {
                worldMixingConfigs.erase(itr);
            }
        }
    }
    return true;
}

bool InitializeWorldEffectiveStates(const SettingsCache &settings,
                                    const std::vector<ResolvedWorldPlacement> &placements,
                                    std::vector<WorldEffectiveState> *states,
                                    std::string *errorMessage)
{
    if (states == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "world effective state output is null";
        }
        return false;
    }
    states->clear();
    states->reserve(placements.size());
    for (const auto &placement : placements) {
        if (placement.placement == nullptr || placement.sourceWorld == nullptr) {
            if (errorMessage != nullptr) {
                *errorMessage = "resolved world placement is incomplete";
            }
            return false;
        }
        const LocationType locationType =
            (placements.size() == 1) ? LocationType::StartWorld : placement.placement->locationType;
        WorldEffectiveState state;
        state.placementIndex = placement.placementIndex;
        state.worldAssetId = placement.worldAssetId;
        state.world = *placement.sourceWorld;
        state.world.locationType = locationType;
        state.world.ClearMixingsAndTraits();
        if (placement.appliedWorldMixingSetting != nullptr) {
            state.world.ApplayWorldMixing(placement.placement->worldMixing);
        }
        state.fixedTraitIds = state.world.fixedTraits;
        state.fixedWorldTraits.reserve(state.fixedTraitIds.size());
        for (const auto &fixedTraitId : state.fixedTraitIds) {
            const auto itr = settings.traits.find(fixedTraitId);
            if (itr == settings.traits.end()) {
                continue;
            }
            state.fixedWorldTraits.push_back(&itr->second);
        }
        for (const auto *trait : state.fixedWorldTraits) {
            if (trait != nullptr) {
                state.world.ApplayTraits(*trait, settings);
            }
        }
        states->push_back(std::move(state));
    }
    return true;
}

void ApplySubworldMixingToWorldEffectiveStates(SettingsCache &settings,
                                               std::vector<WorldEffectiveState> &states)
{
    std::vector<World *> worlds = CollectWorldEffectivePointers(states);
    settings.DoSubworldMixing(worlds, false);
}

std::vector<World *> CollectWorldEffectivePointers(std::vector<WorldEffectiveState> &states)
{
    std::vector<World *> worlds;
    worlds.reserve(states.size());
    for (auto &state : states) {
        worlds.push_back(&state.world);
    }
    return worlds;
}

WorldEffectiveState *FindWorldEffectiveState(std::vector<WorldEffectiveState> &states,
                                             int placementIndex)
{
    const auto itr = std::ranges::find(states, placementIndex, &WorldEffectiveState::placementIndex);
    return itr == states.end() ? nullptr : &*itr;
}

const WorldEffectiveState *FindWorldEffectiveState(const std::vector<WorldEffectiveState> &states,
                                                   int placementIndex)
{
    const auto itr = std::ranges::find(states, placementIndex, &WorldEffectiveState::placementIndex);
    return itr == states.end() ? nullptr : &*itr;
}
