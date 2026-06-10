#pragma once

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "Setting/World.hpp"

struct FixedTraitConflictState {
    std::vector<const WorldTrait *> resolvedFixedTraits;
    std::set<std::string> fixedTraitIds;
    std::set<std::string> blockedExclusiveTags;
};

bool TraitIdEquals(std::string_view left, std::string_view right);
bool TraitIdListContains(const std::vector<std::string> &traitIds,
                         std::string_view traitId);
std::map<std::string, WorldTrait>::const_iterator FindTraitById(
    const std::map<std::string, WorldTrait> &traits,
    std::string_view traitId);

FixedTraitConflictState BuildFixedTraitConflictState(
    const std::map<std::string, WorldTrait> &traits,
    const std::vector<std::string> &fixedTraitIds);

bool TraitConflictsWithFixedTraits(const WorldTrait &candidate,
                                   const FixedTraitConflictState &state);
