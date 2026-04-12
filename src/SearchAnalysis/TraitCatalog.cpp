#include "SearchAnalysis/TraitCatalog.hpp"

#include <string>

#include "Setting/SettingsCache.hpp"

namespace SearchAnalysis {

namespace {

std::vector<std::string> BuildEffectSummary(const WorldTrait &trait)
{
    std::vector<std::string> summary;
    if (!trait.globalFeatureMods.empty()) {
        summary.push_back("globalFeatureMods=" + std::to_string(trait.globalFeatureMods.size()));
    }
    if (!trait.additionalSubworldFiles.empty()) {
        summary.push_back("additionalSubworldFiles=" +
                          std::to_string(trait.additionalSubworldFiles.size()));
    }
    if (!trait.additionalWorldTemplateRules.empty()) {
        summary.push_back("additionalWorldTemplateRules=" +
                          std::to_string(trait.additionalWorldTemplateRules.size()));
    }
    if (!trait.removeWorldTemplateRulesById.empty()) {
        summary.push_back("removeWorldTemplateRulesById=" +
                          std::to_string(trait.removeWorldTemplateRulesById.size()));
    }
    if (summary.empty()) {
        summary.push_back("no_runtime_modifiers");
    }
    return summary;
}

} // namespace

std::vector<TraitMeta> BuildTraitCatalog(const SettingsCache &settings)
{
    std::vector<TraitMeta> catalog;
    catalog.reserve(settings.traits.size());
    for (const auto &pair : settings.traits) {
        const auto &trait = pair.second;
        TraitMeta meta;
        meta.id = trait.filePath;
        meta.name = trait.name;
        meta.description = trait.description;
        meta.traitTags = trait.traitTags;
        meta.exclusiveWith = trait.exclusiveWith;
        meta.exclusiveWithTags = trait.exclusiveWithTags;
        meta.forbiddenDLCIds = trait.forbiddenDLCIds;
        meta.effectSummary = BuildEffectSummary(trait);
        meta.searchable = false;
        catalog.push_back(std::move(meta));
    }
    return catalog;
}

} // namespace SearchAnalysis
