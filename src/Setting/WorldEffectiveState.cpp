#include "Setting/WorldEffectiveState.hpp"

#include <algorithm>
#include <ranges>

#include "Setting/ContentActivation.hpp"
#include "Setting/SettingsCache.hpp"
#include "Setting/WorldTraitConflict.hpp"

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
    const ActiveContentSet activeContent = settings.BuildActiveContentSet();
    for (const auto &placement : placements) {
        if (placement.placement == nullptr || placement.sourceWorld == nullptr) {
            if (errorMessage != nullptr) {
                *errorMessage = "resolved world placement is incomplete";
            }
            return false;
        }
        if (!IsWorldAllowed(*placement.sourceWorld, settings.cluster, activeContent)) {
            if (errorMessage != nullptr) {
                *errorMessage = "world content is not active: " + placement.worldAssetId;
            }
            return false;
        }
        const LocationType locationType =
            (placements.size() == 1) ? LocationType::StartWorld : placement.placement->locationType;
        WorldEffectiveState state;
        state.placementIndex = placement.placementIndex;
        state.worldAssetId = placement.worldAssetId;
        state.activeContentIds = activeContent.ids;
        state.world = *placement.sourceWorld;
        state.world.locationType = locationType;
        state.world.ClearMixingsAndTraits();
        if (placement.appliedWorldMixingSetting != nullptr) {
            state.world.ApplayWorldMixing(placement.placement->worldMixing);
        }
        state.fixedTraitIds = state.world.fixedTraits;
        state.fixedWorldTraits.reserve(state.fixedTraitIds.size());
        for (const auto &fixedTraitId : state.fixedTraitIds) {
            const auto itr = FindTraitById(settings.traits, fixedTraitId);
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
