#include "SettingsCache.hpp"

#include <cstring>
#include <string_view>
#include <set>
#include <algorithm>
#include <filesystem>
#include <mutex>
#include <ranges>
#include <regex>

#include <json/reader.h>
#include <miniz.h>

#include "JsonDeserializeGen.hpp"
#include "Setting/ContentActivation.hpp"
#include "Setting/DlcRegistry.hpp"
#include "Setting/MixingCode.hpp"
#include "Setting/WorldTraitConflict.hpp"
#include "Utils/KRandom.hpp"
#include "Utils/Polygon.hpp"
#include "Utils/PointGenerator.hpp"
#include "Utils/SortHelper.hpp"

Variant SettingsCache::m_nil;

namespace {

std::mutex g_sharedSettingsMutex;
std::shared_ptr<const SettingsCache> g_sharedSettingsCache;

} // namespace

namespace Setting
{
template<>
bool deserialize(const Json::Value &json, std::map<Range, Temperature> &obj)
{
    for (auto itr = json.begin(); itr != json.end(); ++itr) {
        Range key;
        Temperature value;
        if (!Deserializer<Range>::deserialize(itr.name(), key) ||
            !Deserializer<Temperature>::deserialize(*itr, value)) {
            LogE("object std::map<Range, Temperature> parse failed.");
            return false;
        }
        obj[key] = value;
    }
    return true;
}
} // namespace Setting

SettingsCache::SettingsCache(const SettingsCache &other)
{
    *this = other;
}

SettingsCache &SettingsCache::operator=(const SettingsCache &other)
{
    if (this == &other) {
        return *this;
    }

    borders = other.borders;
    defaults = other.defaults;
    layers = other.layers;
    mobs = other.mobs;
    rivers = other.rivers;
    rooms = other.rooms;
    temperatures = other.temperatures;
    biomes = other.biomes;
    clusters = other.clusters;
    features = other.features;
    noise = other.noise;
    subworldMixing = other.subworldMixing;
    subworlds = other.subworlds;
    orderedSubworlds = other.orderedSubworlds;
    traits = other.traits;
    worldMixing = other.worldMixing;
    worlds = other.worlds;
    dlcMixings = other.dlcMixings;
    templates = other.templates;
    traitFeatures = other.traitFeatures;
    mixConfigs = other.mixConfigs;
    cluster = other.cluster;
    seed = other.seed;
    m_dlcState = other.m_dlcState;

    RepairTransientPointersAfterCopy();
    return *this;
}

void SettingsCache::RepairTransientPointersAfterCopy()
{
    if (cluster != nullptr) {
        const std::string coordinatePrefix = cluster->coordinatePrefix;
        cluster = nullptr;
        for (auto &pair : clusters) {
            if (pair.second.coordinatePrefix == coordinatePrefix) {
                cluster = &pair.second;
                break;
            }
        }
    }

    for (auto &config : mixConfigs) {
        config.setting = nullptr;
        if (config.type == 2) {
            auto itr = subworldMixing.find(config.path);
            if (itr != subworldMixing.end()) {
                config.setting = &itr->second;
            }
            continue;
        }
        if (config.type == 1) {
            auto itr = worldMixing.find(config.path);
            if (itr != worldMixing.end()) {
                config.setting = &itr->second;
            }
        }
    }
    for (auto &pair : worlds) {
        pair.second.ClearMixingsAndTraits();
    }

    for (auto &pair : orderedSubworlds) {
        std::vector<SubWorld *> rebound;
        rebound.reserve(pair.second.size());
        for (const auto *subworld : pair.second) {
            if (subworld == nullptr) {
                continue;
            }
            auto itr = subworlds.find(subworld->name);
            if (itr != subworlds.end()) {
                rebound.push_back(&itr->second);
            }
        }
        pair.second = std::move(rebound);
    }
}

template<typename T>
static bool LoadJsonFile(mz_zip_archive &zip, int index, T &result)
{
    size_t size;
    char *ptr = (char *)mz_zip_reader_extract_to_heap(&zip, index, &size, 0);
    if (!ptr) {
        LogE("can not open file %d.", index);
        return false;
    }
    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;
    Json::Value root;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader{builder.newCharReader()};
    auto ret = reader->parse(ptr, ptr + size, &root, &errs);
    mz_free(ptr);
    if (!ret) {
        LogE("parse %d failed, error: %s.", index, errs.c_str());
        return false;
    }
    if (!Setting::deserialize(root, result)) {
        LogE("deserialize failed, file: %d", index);
        return false;
    }
    return true;
}

static std::string GenerateKey(const char *filename)
{
    std::string_view view(filename);
    std::string key;
    if (const auto *dlc = DlcRegistry::FindByArchivePath(view); dlc != nullptr) {
        key = std::string(dlc->resourcePrefix);
    }

    std::string_view relative = view;
    if (const size_t templatePos = relative.find("templates/");
        templatePos != std::string_view::npos) {
        relative.remove_prefix(templatePos + 10);
    } else if (const size_t worldgenPos = relative.find("worldgen/");
               worldgenPos != std::string_view::npos) {
        relative.remove_prefix(worldgenPos + 9);
    }

    key += relative;
    key.resize(key.size() - 5);
    return key;
}

static std::string ResolveOwnedResourcePath(std::string_view resourcePath,
                                            std::string_view ownerResourcePath)
{
    if (resourcePath.empty()) {
        return {};
    }
    if (DlcRegistry::FindByResourcePath(resourcePath) != nullptr) {
        return std::string(resourcePath);
    }
    if (resourcePath.starts_with("noise/")) {
        if (const auto *dlc = DlcRegistry::FindByResourcePath(ownerResourcePath);
            dlc != nullptr) {
            return std::string(dlc->resourcePrefix) + std::string(resourcePath);
        }
    }
    return std::string(resourcePath);
}

const NoiseTree *SettingsCache::FindNoise(std::string_view resourcePath,
                                          std::string_view ownerResourcePath) const
{
    if (resourcePath.empty()) {
        return nullptr;
    }
    if (const auto itr = noise.find(std::string(resourcePath)); itr != noise.end()) {
        return &itr->second;
    }
    const std::string resolvedPath =
        ResolveOwnedResourcePath(resourcePath, ownerResourcePath);
    if (resolvedPath.empty()) {
        return nullptr;
    }
    if (const auto itr = noise.find(resolvedPath); itr != noise.end()) {
        return &itr->second;
    }
    return nullptr;
}

bool SettingsCache::LoadSettingsCache(const std::string_view &content)
{
    if (!defaults.data.empty()) {
        return false;
    }
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, content.data(), content.size(), 0)) {
        LogE("wrong content");
        return false;
    }
    std::map<std::string, std::vector<std::string>> Asubworlds;
    auto count = mz_zip_reader_get_num_files(&zip);
    for (unsigned i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) {
            LogE("Failed to get file stat for file index: %d", i);
            continue;
        }
        if (mz_zip_reader_is_file_a_directory(&zip, i)) {
            continue;
        }
        if (strstr(stat.m_filename, "Asubworlds.json") != nullptr) {
            LoadJsonFile(zip, i, Asubworlds);
            continue;
        }
        if (strstr(stat.m_filename, "worldgen/defaults.json") != nullptr) {
            LoadJsonFile(zip, i, defaults);
            continue;
        }
        if (strstr(stat.m_filename, "worldgen/layers.json") != nullptr) {
            LoadJsonFile(zip, i, layers);
            continue;
        }
        if (strstr(stat.m_filename, "worldgen/rooms.json") != nullptr) {
            LoadJsonFile(zip, i, rooms);
            continue;
        }
        if (strstr(stat.m_filename, "worldgen/rivers.json") != nullptr) {
            LoadJsonFile(zip, i, rivers);
            continue;
        }
        if (strstr(stat.m_filename, "worldgen/temperatures.json") != nullptr) {
            LoadJsonFile(zip, i, temperatures);
            continue;
        }
        if (strstr(stat.m_filename, "worldgen/borders.json") != nullptr) {
            ComposableDictionary<std::vector<WeightedSimHash>> borders2;
            LoadJsonFile(zip, i, borders2);
            borders.Merge(borders2);
            continue;
        }
        if (strstr(stat.m_filename, "worldgen/mobs.json") != nullptr) {
            MobSettings mobs2;
            LoadJsonFile(zip, i, mobs2);
            mobs.MobLookupTable.Merge(mobs2.MobLookupTable);
            continue;
        }
        if (strstr(stat.m_filename, "worldgen/mixing.json") != nullptr) {
            if (const auto *dlc = DlcRegistry::FindByArchivePath(stat.m_filename);
                dlc != nullptr) {
                LoadJsonFile(zip, i, dlcMixings[std::string(dlc->storageKey)]);
            }
            continue;
        }
        if (strstr(stat.m_filename, "worldgen/biomes/") != nullptr) {
            std::string key = GenerateKey(stat.m_filename);
            BiomeSettings biomes2;
            LoadJsonFile(zip, i, biomes2);
            for (auto &pair : biomes2.TerrainBiomeLookupTable.add) {
                std::string name = key + "/" + pair.first;
                biomes.emplace(name, std::move(pair.second));
            }
            continue;
        }
        if (strstr(stat.m_filename, "worldgen/clusters/") != nullptr) {
            std::string key = GenerateKey(stat.m_filename);
            LoadJsonFile(zip, i, clusters[key]);
            continue;
        }
        if (strstr(stat.m_filename, "worldgen/features/") != nullptr) {
            std::string key = GenerateKey(stat.m_filename);
            LoadJsonFile(zip, i, features[key]);
            continue;
        }
        if (strstr(stat.m_filename, "worldgen/noise/") != nullptr) {
            std::string key = GenerateKey(stat.m_filename);
            LoadJsonFile(zip, i, noise[key]);
            continue;
        }
        if (strstr(stat.m_filename, "worldgen/storytraits/") != nullptr) {
            // std::string key = GenerateKey(stat.m_filename);
            // LoadJsonFile(zip, i, storytraits[key]);
            continue;
        }
        if (strstr(stat.m_filename, "worldgen/subworldMixing/") != nullptr) {
            std::string key = GenerateKey(stat.m_filename);
            LoadJsonFile(zip, i, subworldMixing[key]);
            continue;
        }
        if (strstr(stat.m_filename, "worldgen/subworlds/") != nullptr) {
            std::string key = GenerateKey(stat.m_filename);
            SubWorld &subworld = subworlds[key];
            LoadJsonFile(zip, i, subworld);
            subworld.name = key;
            subworld.EnforceTemplateSpawnRuleSelfConsistency();
            continue;
        }
        if (strstr(stat.m_filename, "worldgen/traits/") != nullptr) {
            std::string key = GenerateKey(stat.m_filename);
            WorldTrait &trait = traits[key];
            LoadJsonFile(zip, i, trait);
            trait.filePath = key;
            for (auto &item : trait.globalFeatureMods) {
                traitFeatures.emplace(item.first);
            }
            continue;
        }
        if (strstr(stat.m_filename, "worldgen/worldMixing/") != nullptr) {
            std::string key = GenerateKey(stat.m_filename);
            LoadJsonFile(zip, i, worldMixing[key]);
            continue;
        }
        if (strstr(stat.m_filename, "worldgen/worlds/") != nullptr) {
            std::string key = GenerateKey(stat.m_filename);
            LoadJsonFile(zip, i, worlds[key]);
            continue;
        }
        if (strstr(stat.m_filename, "templates/") != nullptr) {
            std::string key = GenerateKey(stat.m_filename);
            TemplateContainer &templt = templates[key];
            LoadJsonFile(zip, i, templt);
            templt.name = key;
            continue;
        }
        LogE("unknown file: %s", stat.m_filename);
    }
    mz_zip_end(&zip);
    for (auto &pair : Asubworlds) {
        auto &list = orderedSubworlds[pair.first];
        list.reserve(pair.second.size());
        for (auto &name : pair.second) {
            auto itr = subworlds.find(name);
            if (itr != subworlds.end()) {
                list.push_back(&itr->second);
            }
        }
    }
    mixConfigs.clear();
    mixConfigs.reserve(MixingCode::kSupportedSlots.size());
    for (const auto &slot : MixingCode::kSupportedSlots) {
        mixConfigs.push_back(MixingConfig{
            .path = std::string(slot.path),
            .type = slot.type,
        });
    }
    return true;
}

std::shared_ptr<const SettingsCache> SharedSettingsCache::GetOrCreate(
    const BlobLoader &loader,
    std::string *errorMessage)
{
    std::lock_guard<std::mutex> lock(g_sharedSettingsMutex);
    if (g_sharedSettingsCache) {
        return g_sharedSettingsCache;
    }
    if (!loader) {
        if (errorMessage != nullptr) {
            *errorMessage = "shared settings cache loader is empty";
        }
        return nullptr;
    }

    std::vector<char> data;
    std::string loadError;
    if (!loader(data, &loadError)) {
        if (errorMessage != nullptr) {
            *errorMessage = loadError.empty() ? "failed to load settings blob" : loadError;
        }
        return nullptr;
    }
    if (data.empty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "settings blob is empty";
        }
        return nullptr;
    }

    auto cache = std::make_shared<SettingsCache>();
    const std::string_view content(data.data(), data.size());
    if (!cache->LoadSettingsCache(content)) {
        if (errorMessage != nullptr) {
            *errorMessage = "failed to parse settings cache from blob";
        }
        return nullptr;
    }

    g_sharedSettingsCache = cache;
    return g_sharedSettingsCache;
}

void SharedSettingsCache::ResetForTests()
{
    std::lock_guard<std::mutex> lock(g_sharedSettingsMutex);
    g_sharedSettingsCache.reset();
}

static std::vector<std::string> ParseSettingCoordinate(const std::string &coord)
{
    std::regex regex("(.+)-(\\d+)-(.+)-(.+)-(.+)");
    std::smatch match;
    std::vector<std::string> result;
    if (std::regex_match(coord, match, regex)) {
        for (auto &item : match) {
            result.emplace_back(item.str());
        }
    }
    return result;
}

uint64_t SettingsCache::Base36ToBinary(const std::string &input)
{
    uint8_t dict[] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  // 0-9
                      0,  0,  0,  0,  0,  0,  0,              // 3A-40
                      10, 11, 12, 13, 14, 15, 16, 17, 18, 19, // A-J
                      20, 21, 22, 23, 24, 25, 26, 27, 28, 29, // K-T
                      30, 31, 32, 33, 34, 35};                // U-Z
    uint64_t result = 0;
    for (auto itr = input.rbegin(); itr != input.rend(); ++itr) {
        result *= 36;
        result += dict[*itr - '0'];
    }
    return result;
}

std::string SettingsCache::BinaryToBase36(uint64_t input)
{
    if (input == 0) {
        return "0";
    }
    char dict[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::string result;
    while (input > 0) {
        result.push_back(dict[input % 36]);
        input /= 36;
    }
    return result;
}

bool SettingsCache::CoordinateChanged(const std::string &text,
                                      SettingsCache &settings)
{
    (void)settings;
    return CoordinateChanged(text);
}

bool SettingsCache::CoordinateChanged(const std::string &text)
{
    std::vector<std::string> codes = ParseSettingCoordinate(text);
    if (codes.size() < 4 || codes.size() > 6) {
        return false;
    }
    seed = std::stoi(codes[2]);
    ParseAndApplyMixingSettingsCode(codes[5]);
    return InitializeCluster(codes[1]);
}

bool SettingsCache::CoordinateChanged(int type, int seedValue, uint64_t mix)
{
    const char *clusterPrefix[] = {
        "SNDST-A",   "OCAN-A",   "S-FRZ",     "LUSH-A",    "FRST-A",
        "VOLCA",     "BAD-A",    "HTFST-A",   "OASIS-A",   "CER-A",
        "CERS-A",    "PRE-A",    "PRES-A",    "AQU-A",     "V-SNDST-C",
        "V-OCAN-C",  "V-SWMP-C", "V-SFRZ-C",  "V-LUSH-C",  "V-FRST-C",
        "V-VOLCA-C", "V-BAD-C",  "V-HTFST-C", "V-OASIS-C", "V-CER-C",
        "V-CERS-C",  "V-PRE-C",  "V-PRES-C",  "V-AQU-C",   "SNDST-C",
        "AQU-C",     "PRE-C",    "CER-C",     "FRST-C",    "SWMP-C",
        "M-SWMP-C",  "M-BAD-C",  "M-FRZ-C",   "M-FLIP-C",  "M-RAD-C",
        "M-CERS-C"};
    if (type < 0 || type >= static_cast<int>(std::size(clusterPrefix))) {
        return false;
    }

    seed = seedValue;
    ParseAndApplyMixingSettingsCode(mix);
    return InitializeCluster(clusterPrefix[type]);
}

bool SettingsCache::InitializeCluster(std::string_view coord)
{
    cluster = nullptr;
    for (auto &pair : clusters) {
        if (pair.second.coordinatePrefix == coord) {
            cluster = &pair.second;
            break;
        }
    }
    if (cluster == nullptr) {
        LogE("cluster %.*s was wrong.", static_cast<int>(coord.size()), coord.data());
        return false;
    }

    m_dlcState = 0;
    for (auto &id : cluster->requiredDlcIds) {
        if (const auto *dlc = DlcRegistry::FindById(id); dlc != nullptr) {
            m_dlcState |= static_cast<int>(dlc->stateBit);
        }
    }

    MinMax mixIndex;
    if (coord.find("CER") != std::string_view::npos) {
        mixIndex = {0, 5};
    } else if (coord.find("PRE") != std::string_view::npos) {
        mixIndex = {6, 11};
    } else if (coord.find("AQU") != std::string_view::npos) {
        mixIndex = {11, 17};
    }
    for (int i = mixIndex.min; i < mixIndex.max; ++i) {
        mixConfigs[i].level = MixingLevel::Disabled;
    }
    return true;
}

bool SettingsCache::InitializeWorlds(std::vector<World *> &chosenWorlds)
{
    chosenWorlds.clear();
    if (cluster == nullptr) {
        return false;
    }
    chosenWorlds.reserve(cluster->worldPlacements.size());
    for (auto &worldPlacement : cluster->worldPlacements) {
        auto itr = worlds.find(worldPlacement.world);
        if (itr == worlds.end()) {
            LogE("world %s was wrong.", worldPlacement.world.c_str());
            return false;
        }
        itr->second.locationType = worldPlacement.locationType;
        chosenWorlds.push_back(&itr->second);
    }
    if (chosenWorlds.size() == 1) {
        chosenWorlds[0]->locationType = LocationType::StartWorld;
    }
    return true;
}

void SettingsCache::ParseAndApplyMixingSettingsCode(uint64_t num)
{
    for (auto itr = mixConfigs.rbegin(); itr != mixConfigs.rend(); ++itr) {
        itr->level = (MixingLevel)(num % 5);
        itr->minCount = itr->level == MixingLevel::GuranteeMixing ? 1 : 0;
        itr->maxCount = 3;
        num /= 5;
    }
}

void SettingsCache::ParseAndApplyMixingSettingsCode(const std::string &code)
{
    ParseAndApplyMixingSettingsCode(Base36ToBinary(code));
}

ActiveContentSet SettingsCache::BuildActiveContentSet() const
{
    return ::BuildActiveContentSet(cluster, mixConfigs);
}

bool SettingsCache::IsContentEnabled(const std::string &id) const
{
    return BuildActiveContentSet().HasContent(id);
}

void SettingsCache::SanitizeMixingConfigsForCurrentCluster()
{
    const ActiveContentSet activeContent = BuildActiveContentSet();
    for (auto &config : mixConfigs) {
        config.setting = nullptr;
        const WorldMixingSettings *worldSetting = nullptr;
        const SubworldMixingSettings *subworldSetting = nullptr;
        if (config.type == 1) {
            auto itr = worldMixing.find(config.path);
            if (itr != worldMixing.end()) {
                worldSetting = &itr->second;
            }
        } else if (config.type == 2) {
            auto itr = subworldMixing.find(config.path);
            if (itr != subworldMixing.end()) {
                subworldSetting = &itr->second;
            }
        }

        if (!IsMixingConfigAllowed(config,
                                   cluster,
                                   activeContent,
                                   worldSetting,
                                   subworldSetting)) {
            config.level = MixingLevel::Disabled;
            config.minCount = 0;
            config.maxCount = 3;
            continue;
        }

        if (worldSetting != nullptr) {
            config.setting = const_cast<WorldMixingSettings *>(worldSetting);
        } else if (subworldSetting != nullptr) {
            config.setting = const_cast<SubworldMixingSettings *>(subworldSetting);
        }
    }
}

std::vector<const WorldTrait *>
SettingsCache::GetRandomTraits(const World &world, int seedValue) const
{
    if (seedValue == 0 || world.disableWorldTraits ||
        world.worldTraitRules.empty()) {
        return {};
    }
    KRandom kRandom(seedValue);
    std::vector<const WorldTrait *> total;
    total.reserve(traits.size());
    for (auto &pair : traits) {
        if (pair.first[0] == 't') {
            if (IsSpaceOutEnabled() && pair.second.ForbiddenSpaceOut()) {
                continue;
            }
            total.push_back(&pair.second);
        }
    }
    for (auto &pair : traits) {
        if (pair.first[0] == 'e' && IsSpaceOutEnabled()) {
            total.push_back(&pair.second);
        }
    }
    std::vector<const WorldTrait *> result;
    std::set<std::string> except;
    std::vector<const WorldTrait *> filtered;
    filtered.reserve(total.size());
    for (auto &rule : world.worldTraitRules) {
        for (auto &specificTrait : rule.specificTraits) {
            const auto itr = traits.find(specificTrait);
            if (itr != traits.end()) {
                result.push_back(&itr->second);
            }
        }
        filtered.clear();
        for (auto &trait : total) {
            if (!rule.requiredTags.empty() &&
                !std::ranges::all_of(
                    trait->traitTags, [&rule](const std::string &elem) {
                        return std::ranges::contains(rule.requiredTags, elem);
                    })) {
                continue;
            }
            if (!rule.forbiddenTags.empty() &&
                std::ranges::any_of(
                    trait->traitTags, [&rule](const std::string &elem) {
                        return std::ranges::contains(rule.forbiddenTags, elem);
                    })) {
                continue;
            }
            if (std::ranges::contains(rule.forbiddenTraits, trait->filePath)) {
                continue;
            }
            if (trait->IsValid(world)) {
                filtered.push_back(trait);
            }
        }
        int num = kRandom.Next(rule.min, std::max(rule.min, rule.max + 1));
        int count = (int)result.size();
        while ((int)result.size() < count + num && filtered.size() > 0) {
            int index = kRandom.Next((int)filtered.size());
            auto *worldTrait = filtered[index];
            filtered.erase(filtered.begin() + index);
            bool flag = false;
            for (auto &exclusiveId : worldTrait->exclusiveWith) {
                if (std::ranges::contains(
                        result, exclusiveId,
                        [](const WorldTrait *trait) -> const std::string & {
                            return trait->filePath;
                        })) {
                    flag = true;
                    break;
                }
            }
            for (auto &exclusiveWithTag : worldTrait->exclusiveWithTags) {
                if (std::ranges::contains(except, exclusiveWithTag)) {
                    flag = true;
                    break;
                }
            }
            if (!flag) {
                result.emplace_back(worldTrait);
                for (auto &exclusiveWithTag2 : worldTrait->exclusiveWithTags) {
                    except.emplace(exclusiveWithTag2);
                }
                auto itr = std::remove(total.begin(), total.end(), worldTrait);
                total.erase(itr, total.end());
            }
        }
        if ((int)result.size() != count + num) {
            LogI("TraitRule on %s tried to generate %d but only generated %d",
                 world.name.c_str(), num, (int)result.size() - count);
        }
    }
    return result;
}

std::vector<const WorldTrait *>
SettingsCache::GetRandomTraits(const World &world) const
{
    return GetRandomTraits(world, seed);
}

void SettingsCache::SetSeedWithTraits(const std::vector<World *> &activeWorlds,
                                      int traitsFlag,
                                      KRandom &random)
{
    constexpr int MaxTryTimes = 1000;
    std::vector<const WorldTrait *> presets;
    int index = 0;
    for (auto &pair : traits) {
        if ((traitsFlag >> index & 1) == 1) {
            presets.push_back(&pair.second);
        }
        ++index;
    }
    if (presets.empty()) {
        seed = random.Next();
        return;
    }

    index = 0;
    World *world = activeWorlds[index];
    for (size_t i = 0; i < activeWorlds.size(); ++i) {
        world = activeWorlds[i];
        if (world->locationType == LocationType::StartWorld) {
            index = static_cast<int>(i);
            break;
        }
    }

    size_t maxCount = 0;
    int maxCountSeed = 0;
    for (int i = 0; i < MaxTryTimes; ++i) {
        int trySeed = random.Next();
        auto worldTraits = GetRandomTraits(*world, trySeed + index);
        size_t count = 0;
        for (auto *preset : presets) {
            if (std::ranges::contains(worldTraits, preset)) {
                ++count;
            }
        }
        if (count == presets.size()) {
            seed = trySeed;
            return;
        }
        if (maxCount < count) {
            maxCount = count;
            maxCountSeed = trySeed;
        }
    }
    seed = maxCountSeed;
    LogI("can not find seed for preset traits");
}

void SettingsCache::DoSubworldMixing(std::vector<World *> asteroids, bool resetWorldRuntime)
{
    std::vector<MixingConfig *> filtered;
    filtered.reserve(mixConfigs.size());
    for (auto &config : mixConfigs) {
        if (config.level == MixingLevel::Disabled || config.type != 2) {
            continue;
        }
        auto itr = subworldMixing.find(config.path);
        if (itr == subworldMixing.end()) {
            continue;
        }
        bool forbidden = false;
        for (auto &tag : itr->second.forbiddenClusterTags) {
            if (cluster != nullptr &&
                std::ranges::find(cluster->clusterTags, tag) != cluster->clusterTags.end()) {
                forbidden = true;
                break;
            }
        }
        if (!forbidden) {
            config.setting = &(itr->second);
            filtered.push_back(&config);
        }
    }
    KRandom random(seed);
    ShuffleSeeded(asteroids, random);
    ArraySortHelper::Sort(
        asteroids, 0, (int)asteroids.size(), [](World *a, World *b) {
            const int dict[] = {3, 1, 2};
            return dict[(int)a->locationType] < dict[(int)b->locationType];
        });
    for (auto world : asteroids) {
        ShuffleSeeded(filtered, random);
        if (resetWorldRuntime) {
            world->ClearMixingsAndTraits();
        }
        world->ApplayMixings(filtered);
    }
}

SearchMutableStateSnapshot SettingsCache::CaptureSearchMutableState() const
{
    SearchMutableStateSnapshot snapshot;
    snapshot.cluster = cluster;
    snapshot.seed = seed;
    snapshot.dlcState = m_dlcState;
    snapshot.mixConfigs = mixConfigs;
    if (cluster != nullptr) {
        snapshot.activeWorlds.reserve(cluster->worldPlacements.size());
        for (const auto &placement : cluster->worldPlacements) {
            snapshot.activeWorlds.push_back(placement.world);
        }
    }
    return snapshot;
}

void SettingsCache::RestoreSearchMutableState(const SearchMutableStateSnapshot &snapshot)
{
    cluster = snapshot.cluster;
    seed = snapshot.seed;
    m_dlcState = snapshot.dlcState;
    mixConfigs = snapshot.mixConfigs;
    for (const auto &worldName : snapshot.activeWorlds) {
        auto itr = worlds.find(worldName);
        if (itr == worlds.end()) {
            LogE("world %s was wrong during RestoreSearchMutableState().", worldName.c_str());
            continue;
        }
        itr->second.ClearMixingsAndTraits();
    }
}
