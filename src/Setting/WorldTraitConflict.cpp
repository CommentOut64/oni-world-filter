#include "Setting/WorldTraitConflict.hpp"

#include <algorithm>

namespace {

char FoldAscii(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return static_cast<char>(ch - 'A' + 'a');
    }
    return ch;
}

bool HasExclusiveIdConflict(const WorldTrait &left, const WorldTrait &right)
{
    return TraitIdListContains(left.exclusiveWith, right.filePath) ||
           TraitIdListContains(right.exclusiveWith, left.filePath);
}

} // namespace

bool TraitIdEquals(std::string_view left, std::string_view right)
{
    if (left.size() != right.size()) {
        return false;
    }
    for (size_t index = 0; index < left.size(); ++index) {
        if (FoldAscii(left[index]) != FoldAscii(right[index])) {
            return false;
        }
    }
    return true;
}

bool TraitIdListContains(const std::vector<std::string> &traitIds,
                         std::string_view traitId)
{
    return std::ranges::any_of(traitIds, [traitId](const std::string &item) {
        return TraitIdEquals(item, traitId);
    });
}

std::map<std::string, WorldTrait>::const_iterator FindTraitById(
    const std::map<std::string, WorldTrait> &traits,
    std::string_view traitId)
{
    auto exact = traits.find(std::string(traitId));
    if (exact != traits.end()) {
        return exact;
    }
    return std::ranges::find_if(traits, [traitId](const auto &pair) {
        return TraitIdEquals(pair.first, traitId);
    });
}

FixedTraitConflictState BuildFixedTraitConflictState(
    const std::map<std::string, WorldTrait> &traits,
    const std::vector<std::string> &fixedTraitIds)
{
    FixedTraitConflictState state;
    state.resolvedFixedTraits.reserve(fixedTraitIds.size());
    for (const auto &fixedTraitId : fixedTraitIds) {
        state.fixedTraitIds.insert(fixedTraitId);
        const auto itr = FindTraitById(traits, fixedTraitId);
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
    if (std::ranges::any_of(state.fixedTraitIds, [&candidate](const std::string &traitId) {
            return TraitIdEquals(traitId, candidate.filePath);
        })) {
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
