#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "Setting/World.hpp"

struct FixedTraitConflictState {
    std::vector<const WorldTrait *> resolvedFixedTraits;
    std::set<std::string> fixedTraitIds;
    std::set<std::string> blockedExclusiveTags;
};

FixedTraitConflictState BuildFixedTraitConflictState(
    const std::map<std::string, WorldTrait> &traits,
    const std::vector<std::string> &fixedTraitIds);

bool TraitConflictsWithFixedTraits(const WorldTrait &candidate,
                                   const FixedTraitConflictState &state);
