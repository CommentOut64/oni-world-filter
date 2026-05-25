#pragma once

#include "DefaultSettings.hpp"

struct Feature {
    std::string type;
    std::vector<std::string> tags;
    std::vector<std::string> excludesTags;

    // ignore lines

    bool operator<(const Feature &rhs) const { return type < rhs.type; }
};

struct NoiseNodeRef {
    std::string type;
    std::string name;
};

struct NoiseLink {
    NoiseNodeRef target;
    std::optional<NoiseNodeRef> source0;
    std::optional<NoiseNodeRef> source1;
    std::optional<NoiseNodeRef> source2;
};

struct NoiseGraphSettings {
    float zoom = 1.0f;
    bool normalise{};
    bool seamless{};
    Vector2f lowerBound = {2.0f, 2.0f};
    Vector2f upperBound = {4.0f, 4.0f};
    std::string name;
    Vector2f pos;
};

struct NoisePrimitive {
    std::string primative;
    std::string quality;
    int seed{};
    float offset{};
    std::string name;
    Vector2f pos;
};

struct NoiseFilter {
    std::string filter;
    float frequency = 1.0f;
    float lacunarity = 1.0f;
    int octaves = 1;
    float offset{};
    float gain = 1.0f;
    float exponent = 1.0f;
    float scale = 1.0f;
    float bias{};
    std::string name;
    Vector2f pos;
};

struct NoiseModifier {
    std::string modifyType;
    float lower = -1.0f;
    float upper = 1.0f;
    float exponent = 1.0f;
    float scale = 1.0f;
    float bias{};
    Vector2f scale2d = {1.0f, 1.0f};
    std::string name;
    Vector2f pos;
};

struct NoiseTransformer {
    std::string transformerType;
    float power = 1.0f;
    Vector2f vector;
    std::string name;
    Vector2f pos;
};

struct NoiseSelector {
    std::string selectType;
    float lower{};
    float upper{};
    float edge{};
    std::string name;
    Vector2f pos;
};

struct NoiseCombiner {
    std::string combineType;
    std::string name;
    Vector2f pos;
};

struct NoiseControlPoint {
    float input{};
    float output{};
};

struct NoiseControlPoints {
    std::vector<NoiseControlPoint> points;
    std::string name;
};

struct NoiseTree {
    NoiseGraphSettings settings;
    std::vector<NoiseLink> links;
    std::map<std::string, NoisePrimitive> primitives;
    std::map<std::string, NoiseFilter> filters;
    std::map<std::string, NoiseTransformer> transformers;
    std::map<std::string, NoiseSelector> selectors;
    std::map<std::string, NoiseModifier> modifiers;
    std::map<std::string, NoiseCombiner> combiners;
    std::map<std::string, float> floats;
    std::map<std::string, NoiseControlPoints> controlpoints;
};

struct WeightedBiome {
    std::string name;
    float weight{};
    std::vector<std::string> tags;
};

struct AllowedCellsFilter {
    TagCommand tagcommand{};
    std::string tag;
    int minDistance{};
    int maxDistance{};
    Command command = Command::Replace;
    std::vector<Range> temperatureRanges;
    std::vector<ZoneType> zoneTypes;
    std::vector<std::string> subworldNames;
    int sortOrder{};
    bool ignoreIfMissingTag{};

    // ignore lines

    bool hasBackup{false};
    std::vector<std::string> subworldNames2;

    bool operator<(const AllowedCellsFilter &rhs) const
    {
        return sortOrder < rhs.sortOrder;
    }

    void Backup()
    {
        if (!hasBackup) {
            if (subworldNames2.empty()) {
                subworldNames2 = subworldNames;
            }
            hasBackup = true;
        }
    }

    void Restore()
    {
        if (hasBackup) {
            subworldNames = subworldNames2;
            hasBackup = false;
        }
    }
};

struct TemplateSpawnRules {
    std::string ruleId;
    std::vector<std::string> names;
    ListRule listRule{};
    int someCount{};
    int moreCount{};
    Vector2f range;
    int times = 1;
    float priority{};
    bool allowDuplicates{};
    bool allowExtremeTemperatureOverlap{};
    bool allowNearStart{};
    bool useRelaxedFiltering{};
    Vector2f overrideOffset;
    Vector2f overridePlacement = {-1.0f, -1.0f};
    std::vector<AllowedCellsFilter> allowedCellsFilter;
};

struct WeightedSubworldName {
    std::string name;
    std::string overrideName;
    float overridePower{};
    float weight = 1.0f;
    int minCount{};
    int maxCount = INT_MAX;
    int priority{};
};

struct WorldMixing {
    std::vector<std::string> requiredTags;
    std::vector<std::string> forbiddenTags;
    std::vector<TemplateSpawnRules> additionalWorldTemplateRules;
    std::vector<AllowedCellsFilter> additionalUnknownCellFilters;
    std::vector<WeightedSubworldName> additionalSubworldFiles;
    std::vector<std::string> additionalSeasons;
};

struct WorldMixingSettings {
    std::string name;
    std::string description;
    std::string icon;
    std::vector<std::string> forbiddenClusterTags;
    std::vector<std::string> required_content;
    std::string world;
};

struct TraitRule {
    int min = 0;
    int max = 0;
    std::vector<std::string> requiredTags;
    std::vector<std::string> specificTraits;
    std::vector<std::string> forbiddenTags;
    std::vector<std::string> forbiddenTraits;
};

struct SubworldMixingRule {
    std::string name;
    int minCount{};
    int maxCount = INT_MAX;
    std::vector<std::string> forbiddenTags;
    std::vector<std::string> requiredTags;
};

struct ElementBandModifier {
    std::string element;
    float massMultiplier = 1.0f;
    float bandMultiplier = 1.0f;
};

struct MobReference {
    std::string type;
    MinMax count;
};

struct ElementChoiceGroup {
    Selection selectionMethod{};
    std::vector<WeightedSimHash> choices;
};

struct FeatureSettings {
    Shape shape{};
    std::vector<int> borders;
    MinMax blobSize;
    std::string forceBiome;
    std::vector<std::string> biomeTags;
    std::vector<MobReference> internalMobs;
    std::vector<std::string> tags;
    std::map<std::string, ElementChoiceGroup> ElementChoiceGroups;
    //
    std::vector<std::string> excludeTags;
};

struct ElementGradient {
    std::string content;
    float bandSize{};
    float maxValue{};
    Override overrides;
};

struct BiomeSettings {
    ComposableDictionary<std::vector<ElementGradient>> TerrainBiomeLookupTable;
};

struct SubworldMixingSettings {
    std::string name;
    std::string description;
    std::string icon;
    std::vector<std::string> forbiddenClusterTags;
    std::vector<std::string> required_content;
    WeightedSubworldName subworld;
    std::vector<std::string> mixingTags;
    std::vector<TemplateSpawnRules> additionalWorldTemplateRules;
};

struct SpaceMapPOIPlacement {
    std::vector<std::string> pois;
    int numToSpawn{};
    MinMax allowedRings = {0, 9999};
    bool avoidClumping{};
    bool canSpawnDuplicates{};
    bool guarantee{};
};

struct LoreCollectionOverride {
    std::string id;
    std::string collection;
    OrderRule orderRule{};
};

struct SpaceDestinationMix {
    int minTier = 0;
    int maxTier = 99;
    std::string type;
};

struct DlcMixingSetting {
    std::vector<SpaceDestinationMix> spaceDesinations;
    std::vector<SpaceMapPOIPlacement> spacePois;
    std::vector<LoreCollectionOverride> globalLoreUnlocks;
};

struct ModifyLayoutTagsRule {
    std::vector<std::string> addTags;
    std::vector<std::string> removeTags;
    std::vector<AllowedCellsFilter> allowedCellsFilter;
};
