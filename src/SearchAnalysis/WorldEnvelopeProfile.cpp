#include "SearchAnalysis/WorldEnvelopeProfile.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <ranges>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Batch/FilterConfig.hpp"
#include "SearchAnalysis/SearchCatalog.hpp"
#include "Setting/SettingsCache.hpp"
#include "WorldGen.hpp"

namespace SearchAnalysis {

namespace {

constexpr int kMixingMax = 48828124;
// 11 个 mixing 槽位全部设为 level 1 (Enabled) 的编码值:
// sum(1 * 5^i, i=0..10) = (5^11 - 1) / 4 = 12207031
constexpr int kMixingProbeAllEnabled = 12207031;

std::string BuildWorldCode(int worldType, int mixing)
{
    const auto &prefixes = GetWorldPrefixes();
    if (worldType < 0 || worldType >= static_cast<int>(prefixes.size())) {
        return {};
    }
    const int normalizedMixing = std::clamp(mixing, 0, kMixingMax);
    std::string code = prefixes[static_cast<size_t>(worldType)];
    code += "100000-0-D3-";
    code += SettingsCache::BinaryToBase36(static_cast<uint32_t>(normalizedMixing));
    return code;
}

uint64_t Fnv1a64(const std::string &text)
{
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char c : text) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string BuildRuleId(const TemplateSpawnRules &rule, int ruleIndex)
{
    if (!rule.ruleId.empty()) {
        return rule.ruleId;
    }

    std::string signature;
    signature.reserve(256);
    signature += "idx=" + std::to_string(ruleIndex);
    signature += ";list=" + std::to_string(static_cast<int>(rule.listRule));
    signature += ";some=" + std::to_string(rule.someCount);
    signature += ";more=" + std::to_string(rule.moreCount);
    signature += ";times=" + std::to_string(rule.times);
    signature += ";rangeX=" + std::to_string(rule.range.x);
    signature += ";rangeY=" + std::to_string(rule.range.y);
    signature += ";dup=" + std::to_string(rule.allowDuplicates ? 1 : 0);
    signature += ";names=";
    for (const auto &name : rule.names) {
        signature += name;
        signature.push_back('|');
    }

    const uint64_t hash = Fnv1a64(signature);
    return "sig:" + std::to_string(hash);
}

std::string BuildEnvelopeId(const std::string &ruleId)
{
    if (ruleId.empty()) {
        return {};
    }
    return "env:" + ruleId;
}

int ComputeRuleMaxPerExecution(const TemplateSpawnRules &rule)
{
    const int nameCount = static_cast<int>(rule.names.size());
    switch (rule.listRule) {
    case ListRule::GuaranteeAll:
    case ListRule::TryAll:
        return std::max(0, nameCount);
    case ListRule::GuaranteeSome:
    case ListRule::TrySome:
        return std::max(0, rule.someCount);
    case ListRule::GuaranteeSomeTryMore:
        return std::max(0, rule.someCount + rule.moreCount);
    case ListRule::GuaranteeOne:
    case ListRule::TryOne:
        return 1;
    case ListRule::GuaranteeRange:
    case ListRule::TryRange:
        // 规划文档要求 range 类型按 range.y 作为单次执行上界。
        return std::max(0, static_cast<int>(rule.range.y));
    default:
        break;
    }
    return 0;
}

std::vector<World *> CollectClusterWorlds(SettingsCache *settings)
{
    std::vector<World *> worlds;
    if (settings == nullptr || settings->cluster == nullptr) {
        return worlds;
    }
    worlds.reserve(settings->cluster->worldPlacements.size());
    for (const auto &placement : settings->cluster->worldPlacements) {
        const auto itr = settings->worlds.find(placement.world);
        if (itr == settings->worlds.end()) {
            continue;
        }
        itr->second.locationType = placement.locationType;
        worlds.push_back(&itr->second);
    }
    if (worlds.size() == 1) {
        worlds.front()->locationType = LocationType::StartWorld;
    }
    return worlds;
}

World *PickTargetWorld(const std::vector<World *> &worlds)
{
    for (auto *world : worlds) {
        if (world != nullptr && world->locationType == LocationType::StartWorld) {
            return world;
        }
    }
    if (!worlds.empty()) {
        return worlds.front();
    }
    return nullptr;
}

std::vector<const TemplateSpawnRules *> CollectTemplateRules(const World &world,
                                                             const SettingsCache &settings)
{
    std::vector<const TemplateSpawnRules *> rules;
    rules.reserve(world.worldTemplateRules2.size() + world.subworldFiles2.size() * 2);
    for (const auto *item : world.worldTemplateRules2) {
        if (item != nullptr) {
            rules.push_back(item);
        }
    }
    for (const auto *subworldFile : world.subworldFiles2) {
        if (subworldFile == nullptr) {
            continue;
        }
        const auto itr = settings.subworlds.find(subworldFile->name);
        if (itr == settings.subworlds.end()) {
            continue;
        }
        for (const auto &rule : itr->second.subworldTemplateRules) {
            rules.push_back(&rule);
        }
    }
    return rules;
}

bool DoesCellMatchFilterForEnvelope(const Site &site,
                                    const AllowedCellsFilter &filter,
                                    bool *applied)
{
    if (applied != nullptr) {
        *applied = true;
    }
    switch (filter.tagcommand) {
    case TagCommand::AtTag:
        return site.tags.contains(filter.tag);
    case TagCommand::NotAtTag:
        return !site.tags.contains(filter.tag);
    case TagCommand::Default:
        if (!filter.subworldNames.empty()) {
            for (const auto &subworldName : filter.subworldNames) {
                if (site.tags.contains(subworldName)) {
                    return true;
                }
            }
            return false;
        }
        if (!filter.zoneTypes.empty()) {
            for (const auto &zoneType : filter.zoneTypes) {
                if (site.tags.contains(ZoneTypeToString(zoneType))) {
                    return true;
                }
            }
            return false;
        }
        if (!filter.temperatureRanges.empty()) {
            for (const auto &range : filter.temperatureRanges) {
                if (site.tags.contains(TempRangeToString(range))) {
                    return true;
                }
            }
            return false;
        }
        return true;
    case TagCommand::DistanceFromTag: {
        if (site.parent == nullptr) {
            if (applied != nullptr) {
                *applied = false;
            }
            return true;
        }
        const auto itr = site.parent->minDistanceToTag.find(filter.tag);
        if (itr != site.parent->minDistanceToTag.end()) {
            if (itr->second >= filter.minDistance) {
                return itr->second <= filter.maxDistance;
            }
            return false;
        }
        if (applied != nullptr) {
            *applied = false;
        }
        return true;
    }
    default:
        break;
    }
    return false;
}

bool DoesCellMatchFiltersForEnvelope(const Site &site,
                                     const std::vector<AllowedCellsFilter> &filters)
{
    bool matched = false;
    for (const auto &filter : filters) {
        bool applied = false;
        const bool filterMatched = DoesCellMatchFilterForEnvelope(site, filter, &applied);
        if (!applied) {
            continue;
        }
        switch (filter.command) {
        case Command::All:
            matched = true;
            break;
        case Command::Clear:
            matched = false;
            break;
        case Command::Replace:
            matched = filterMatched;
            break;
        case Command::ExceptWith:
        case Command::SymmetricExceptWith:
            if (filterMatched) {
                matched = false;
            }
            break;
        case Command::UnionWith:
            matched = filterMatched || matched;
            break;
        case Command::IntersectWith:
            matched = filterMatched && matched;
            break;
        }
    }
    return matched;
}

std::vector<const Site *> CollectLeafSites(const std::vector<Site> &sites, const Site **startSite)
{
    std::vector<const Site *> leafSites;
    if (startSite != nullptr) {
        *startSite = nullptr;
    }
    for (const auto &site : sites) {
        if (!site.children) {
            continue;
        }
        for (const auto &child : *site.children) {
            leafSites.push_back(&child);
            if (startSite != nullptr &&
                *startSite == nullptr &&
                child.tags.contains("StartLocation")) {
                *startSite = &child;
            }
        }
    }
    return leafSites;
}

std::vector<std::string> ExtractTemplateGeyserIds(const SettingsCache &settings,
                                                  const std::string &templateName)
{
    std::vector<std::string> result;
    if (templateName == "geysers/generic") {
        return result;
    }

    if (templateName.starts_with("poi/oil/")) {
        result.push_back("oil_reservoir");
        return result;
    }
    if (templateName.starts_with("expansion1::poi/warp/receiver")) {
        result.push_back("warp_sender");
        return result;
    }
    if (templateName.starts_with("expansion1::poi/warp/sender")) {
        result.push_back("warp_receiver");
        return result;
    }
    if (templateName.starts_with("expansion1::poi/warp/teleporter")) {
        result.push_back("warp_portal");
        return result;
    }
    if (templateName.starts_with("expansion1::poi/traits/cryopod")) {
        result.push_back("cryo_tank");
        return result;
    }

    const auto templateItr = settings.templates.find(templateName);
    if (templateItr == settings.templates.end()) {
        return result;
    }

    std::set<std::string> dedup;
    for (const auto &entity : templateItr->second.otherEntities) {
        constexpr const char *prefix = "GeyserGeneric_";
        if (entity.id.rfind(prefix, 0) != 0) {
            continue;
        }
        const std::string geyser = entity.id.substr(14);
        if (Batch::GeyserIdToIndex(geyser) < 0) {
            continue;
        }
        if (dedup.insert(geyser).second) {
            result.push_back(geyser);
        }
    }
    return result;
}

void BuildGenericTypeUpperById(const SettingsCache &settings,
                               std::map<std::string, double> *out)
{
    if (out == nullptr) {
        return;
    }
    out->clear();
    const auto &ids = Batch::GetGeyserIds();
    for (const auto &id : ids) {
        (*out)[id] = 0.0;
    }

    if (settings.IsSpaceOutEnabled()) {
        const double upper = 1.0 / 23.0;
        for (int i = 0; i <= 22 && i < static_cast<int>(ids.size()); ++i) {
            (*out)[ids[static_cast<size_t>(i)]] = upper;
        }
        return;
    }

    const double upper = 1.0 / 20.0;
    for (int i = 0; i <= 18 && i < static_cast<int>(ids.size()); ++i) {
        (*out)[ids[static_cast<size_t>(i)]] = upper;
    }
    if (21 < static_cast<int>(ids.size())) {
        (*out)[ids[21]] = upper;
    }
}

void BuildGeyserEnvelope(const SettingsCache &settings,
                         const World &world,
                         WorldEnvelopeProfile *profile)
{
    if (profile == nullptr) {
        return;
    }
    const auto &geyserIds = Batch::GetGeyserIds();
    profile->possibleMaxCountByType.clear();
    for (const auto &geyserId : geyserIds) {
        profile->possibleMaxCountByType[geyserId] = 0;
    }
    BuildGenericTypeUpperById(settings, &profile->genericTypeUpperById);

    const auto rules = CollectTemplateRules(world, settings);
    profile->exactSourceSummary.clear();
    profile->genericSourceSummary.clear();
    profile->sourcePools.clear();
    profile->genericSlotUpper = 0;
    std::map<std::string, int> poolCapacityUpper;
    int ruleIndex = 0;
    for (const auto *rule : rules) {
        if (rule == nullptr) {
            ++ruleIndex;
            continue;
        }
        const int perExecutionMax = ComputeRuleMaxPerExecution(*rule);
        const int executionTimes = std::max(1, rule->times);
        const int totalRuleUpper = perExecutionMax * executionTimes;
        if (totalRuleUpper <= 0) {
            ++ruleIndex;
            continue;
        }
        const std::string ruleId = BuildRuleId(*rule, ruleIndex);
        const std::string envelopeId = BuildEnvelopeId(ruleId);
        const bool containsGeneric =
            std::ranges::find(rule->names, std::string("geysers/generic")) != rule->names.end();
        if (containsGeneric) {
            profile->genericSlotUpper += totalRuleUpper;
            poolCapacityUpper["generic"] += totalRuleUpper;
            profile->genericSourceSummary.push_back(SourceSummary{
                .ruleId = ruleId,
                .templateName = "geysers/generic",
                .geyserId = "geysers/generic",
                .upperBound = totalRuleUpper,
                .sourceKind = "generic",
                .poolId = "generic",
                .envelopeId = envelopeId,
            });
        }

        for (const auto &name : rule->names) {
            if (name == "geysers/generic") {
                continue;
            }
            const auto geysers = ExtractTemplateGeyserIds(settings, name);
            if (geysers.empty()) {
                continue;
            }
            if (perExecutionMax <= 0) {
                continue;
            }
            const int templateUpper = rule->allowDuplicates ? executionTimes : 1;
            for (const auto &geyserId : geysers) {
                profile->possibleMaxCountByType[geyserId] += templateUpper;
                const auto existing = poolCapacityUpper.find(ruleId);
                if (existing == poolCapacityUpper.end()) {
                    poolCapacityUpper.emplace(ruleId, totalRuleUpper);
                } else {
                    existing->second = std::max(existing->second, totalRuleUpper);
                }
                profile->exactSourceSummary.push_back(SourceSummary{
                    .ruleId = ruleId,
                    .templateName = name,
                    .geyserId = geyserId,
                    .upperBound = templateUpper,
                    .sourceKind = "exact",
                    .poolId = ruleId,
                    .envelopeId = envelopeId,
                });
            }
        }
        ++ruleIndex;
    }

    for (const auto &[geyserId, upper] : profile->genericTypeUpperById) {
        if (upper > 0.0) {
            profile->possibleMaxCountByType[geyserId] += profile->genericSlotUpper;
        }
    }

    profile->possibleGeyserTypes.clear();
    profile->impossibleGeyserTypes.clear();
    for (const auto &geyserId : geyserIds) {
        const auto itr = profile->possibleMaxCountByType.find(geyserId);
        if (itr != profile->possibleMaxCountByType.end() && itr->second > 0) {
            profile->possibleGeyserTypes.push_back(geyserId);
        } else {
            profile->impossibleGeyserTypes.push_back(geyserId);
        }
    }

    for (const auto &[poolId, capacity] : poolCapacityUpper) {
        profile->sourcePools.push_back(SourcePool{
            .poolId = poolId,
            .sourceKind = (poolId == "generic") ? "generic" : "exact",
            .capacityUpper = capacity,
        });
    }
}

void BuildSpatialEnvelopes(SettingsCache &settings,
                           World &world,
                           WorldEnvelopeProfile *profile)
{
    if (profile == nullptr) {
        return;
    }

    profile->spatialEnvelopes.clear();
    profile->envelopeStatsById.clear();

    WorldGen worldGen(world, settings);
    std::vector<Site> generatedSites;
    if (!worldGen.GenerateOverworld(generatedSites)) {
        profile->spatialEnvelopes.push_back(SpatialEnvelope{
            .envelopeId = "global",
            .confidence = "low",
            .method = "generation-failed",
        });
        return;
    }

    const Site *startSite = nullptr;
    const auto leafSites = CollectLeafSites(generatedSites, &startSite);
    if (leafSites.empty() || startSite == nullptr) {
        profile->spatialEnvelopes.push_back(SpatialEnvelope{
            .envelopeId = "global",
            .confidence = "low",
            .method = "candidate-sites-unavailable",
        });
        return;
    }

    const auto start = startSite->polygon.Centroid();
    const auto rules = CollectTemplateRules(world, settings);
    int ruleIndex = 0;
    for (const auto *rule : rules) {
        if (rule == nullptr) {
            ++ruleIndex;
            continue;
        }
        const int perExecutionMax = ComputeRuleMaxPerExecution(*rule);
        const int executionTimes = std::max(1, rule->times);
        const int totalRuleUpper = perExecutionMax * executionTimes;
        if (totalRuleUpper <= 0) {
            ++ruleIndex;
            continue;
        }

        const std::string ruleId = BuildRuleId(*rule, ruleIndex);
        const std::string envelopeId = BuildEnvelopeId(ruleId);
        EnvelopeStats stats;
        stats.confidence = "medium";
        stats.method = "candidate-sites";

        for (const auto *site : leafSites) {
            if (site == nullptr) {
                continue;
            }
            if (!DoesCellMatchFiltersForEnvelope(*site, rule->allowedCellsFilter)) {
                continue;
            }
            const auto &centroid = site->polygon.Centroid();
            const double dx = static_cast<double>(centroid.x - start.x);
            const double dy = static_cast<double>(centroid.y - start.y);
            stats.candidateDistances.push_back(std::sqrt(dx * dx + dy * dy));
        }
        stats.candidateCount = static_cast<int>(stats.candidateDistances.size());

        if (stats.candidateCount > 0) {
            profile->envelopeStatsById.emplace(envelopeId, std::move(stats));
            profile->spatialEnvelopes.push_back(SpatialEnvelope{
                .envelopeId = envelopeId,
                .confidence = "medium",
                .method = "candidate-sites",
            });
        }
        ++ruleIndex;
    }

    if (profile->spatialEnvelopes.empty()) {
        profile->spatialEnvelopes.push_back(SpatialEnvelope{
            .envelopeId = "global",
            .confidence = "low",
            .method = "candidate-sites-empty",
        });
    }
}

} // namespace

WorldEnvelopeProfile CompileWorldEnvelopeProfile(const SettingsCache &baseSettings,
                                                 int worldType,
                                                 int mixing,
                                                 std::string *errorMessage)
{
    return CompileWorldEnvelopeProfile(baseSettings,
                                       worldType,
                                       mixing,
                                       WorldEnvelopeCompileOptions{},
                                       errorMessage);
}

WorldEnvelopeProfile CompileWorldEnvelopeProfile(const SettingsCache &baseSettings,
                                                 int worldType,
                                                 int mixing,
                                                 const WorldEnvelopeCompileOptions &options,
                                                 std::string *errorMessage)
{
    WorldEnvelopeProfile profile;
    profile.worldType = worldType;
    profile.worldCode = BuildWorldCode(worldType, mixing);

    if (profile.worldCode.empty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "worldType 超出有效范围";
        }
        return profile;
    }

    SettingsCache settings = baseSettings;
    if (!settings.CoordinateChanged(profile.worldCode, settings)) {
        if (errorMessage != nullptr) {
            *errorMessage = "world code 解析失败";
        }
        return profile;
    }

    auto worlds = CollectClusterWorlds(&settings);
    if (worlds.empty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "cluster 未绑定可用 world";
        }
        return profile;
    }

    settings.DoSubworldMixing(worlds);
    World *targetWorld = PickTargetWorld(worlds);
    if (targetWorld == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "无法确定目标 world";
        }
        return profile;
    }

    profile.width = static_cast<int>(targetWorld->worldsize.x);
    profile.height = static_cast<int>(targetWorld->worldsize.y);
    profile.diagonal = std::sqrt(
        static_cast<double>(profile.width) * static_cast<double>(profile.width) +
        static_cast<double>(profile.height) * static_cast<double>(profile.height));

    profile.activeMixingSlots.clear();
    profile.disabledMixingSlots.clear();
    for (size_t slot = 0; slot < settings.mixConfigs.size(); ++slot) {
        const auto level = settings.mixConfigs[slot].level;
        if (level != MixingLevel::Disabled) {
            profile.activeMixingSlots.push_back(static_cast<int>(slot));
        }
    }

    // 用全启用 probe 值探测世界结构性禁用槽位（CER/PRE 约束），
    // 而非把用户未开启的槽位也标记为 disabled
    {
        std::string probeCode = BuildWorldCode(worldType, kMixingProbeAllEnabled);
        SettingsCache probeSettings = baseSettings;
        if (probeSettings.CoordinateChanged(probeCode, probeSettings)) {
            for (size_t slot = 0; slot < probeSettings.mixConfigs.size(); ++slot) {
                if (probeSettings.mixConfigs[slot].level == MixingLevel::Disabled) {
                    profile.disabledMixingSlots.push_back(static_cast<int>(slot));
                }
            }
        }
    }

    BuildGeyserEnvelope(settings, *targetWorld, &profile);
    if (options.includeSpatialEnvelopes) {
        BuildSpatialEnvelopes(settings, *targetWorld, &profile);
    } else {
        profile.spatialEnvelopes.push_back(SpatialEnvelope{
            .envelopeId = "global",
            .confidence = "low",
            .method = "placeholder",
        });
    }
    profile.valid = true;
    return profile;
}

} // namespace SearchAnalysis
