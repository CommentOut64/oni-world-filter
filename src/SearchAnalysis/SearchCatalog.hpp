#pragma once

#include <string>
#include <vector>

class SettingsCache;

namespace SearchAnalysis {

struct WorldCatalogItem {
    int id = 0;
    std::string code;
};

struct GeyserCatalogItem {
    int id = 0;
    std::string key;
};

struct TraitMeta {
    std::string id;
    std::string name;
    std::string description;
    std::vector<std::string> traitTags;
    std::vector<std::string> exclusiveWith;
    std::vector<std::string> exclusiveWithTags;
    std::vector<std::string> forbiddenDLCIds;
    std::vector<std::string> effectSummary;
    bool searchable = false;
};

struct MixingSlotMeta {
    int slot = 0;
    std::string path;
    std::string type;
    std::string name;
    std::string description;
};

struct ParameterSpec {
    std::string id;
    std::string valueType;
    std::string meaning;
    std::string staticRange;
    bool supportsDynamicRange = false;
    std::string source;
};

struct SearchCatalog {
    std::vector<WorldCatalogItem> worlds;
    std::vector<GeyserCatalogItem> geysers;
    std::vector<TraitMeta> traits;
    std::vector<MixingSlotMeta> mixingSlots;
    std::vector<ParameterSpec> parameterSpecs;
};

SearchCatalog BuildSearchCatalog(const SettingsCache &settings);
const std::vector<std::string> &GetWorldPrefixes();

} // namespace SearchAnalysis
