#include "Setting/WorldTraitConflict.hpp"

#include <algorithm>

namespace {

bool HasExclusiveIdConflict(const WorldTrait &left, const WorldTrait &right)
{
    return std::find(left.exclusiveWith.begin(), left.exclusiveWith.end(), right.filePath) !=
               left.exclusiveWith.end() ||
           std::find(right.exclusiveWith.begin(), right.exclusiveWith.end(), left.filePath) !=
               right.exclusiveWith.end();
}

} // namespace

FixedTraitConflictState BuildFixedTraitConflictState(
    const std::map<std::string, WorldTrait> &traits,
    const std::vector<std::string> &fixedTraitIds)
{
    FixedTraitConflictState state;
    state.resolvedFixedTraits.reserve(fixedTraitIds.size());
    for (const auto &fixedTraitId : fixedTraitIds) {
        state.fixedTraitIds.insert(fixedTraitId);
        const auto itr = traits.find(fixedTraitId);
        if (itr == traits.end()) {
            continue;
        }
        state.resolvedFixedTraits.push_back(&itr->second);
        for (const auto &exclusiveWithTag : itr->second.exclusiveWithTags) {
            state.blockedExclusiveTags.insert(exclusiveWithTag);
        }
    }
    return state;
}

bool TraitConflictsWithFixedTraits(const WorldTrait &candidate,
                                   const FixedTraitConflictState &state)
{
    if (state.fixedTraitIds.contains(candidate.filePath)) {
        return true;
    }
    if (std::any_of(
            candidate.exclusiveWithTags.begin(),
            candidate.exclusiveWithTags.end(),
            [&state](const std::string &exclusiveWithTag) {
                return state.blockedExclusiveTags.contains(exclusiveWithTag);
            })) {
        return true;
    }
    for (const auto *fixedTrait : state.resolvedFixedTraits) {
        if (fixedTrait == nullptr) {
            continue;
        }
        if (HasExclusiveIdConflict(candidate, *fixedTrait)) {
            return true;
        }
    }
    return false;
}
