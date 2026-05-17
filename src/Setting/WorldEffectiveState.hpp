#pragma once

#include <string>
#include <vector>

#include "ClusterLayout.hpp"
#include "World.hpp"

class SettingsCache;

struct ResolvedWorldPlacement {
    int placementIndex{-1};
    const WorldPlacement *placement = nullptr;
    World *sourceWorld = nullptr;
    const WorldMixingSettings *appliedWorldMixingSetting = nullptr;
    std::string worldAssetId;
};

struct WorldEffectiveState {
    int placementIndex{-1};
    std::string worldAssetId;
    std::vector<std::string> fixedTraitIds;
    std::vector<const WorldTrait *> fixedWorldTraits;
    std::vector<const WorldTrait *> randomTraits;
    World world;
};

bool BuildResolvedWorldPlacements(SettingsCache &settings,
                                  std::vector<ResolvedWorldPlacement> *placements,
                                  std::string *errorMessage = nullptr);

bool InitializeWorldEffectiveStates(const SettingsCache &settings,
                                    const std::vector<ResolvedWorldPlacement> &placements,
                                    std::vector<WorldEffectiveState> *states,
                                    std::string *errorMessage = nullptr);

void ApplySubworldMixingToWorldEffectiveStates(SettingsCache &settings,
                                               std::vector<WorldEffectiveState> &states);

std::vector<World *> CollectWorldEffectivePointers(std::vector<WorldEffectiveState> &states);

WorldEffectiveState *FindWorldEffectiveState(std::vector<WorldEffectiveState> &states,
                                             int placementIndex);

const WorldEffectiveState *FindWorldEffectiveState(const std::vector<WorldEffectiveState> &states,
                                                   int placementIndex);
