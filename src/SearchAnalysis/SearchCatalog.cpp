#include "SearchAnalysis/SearchCatalog.hpp"

#include <array>
#include <string>

#include "Batch/FilterConfig.hpp"
#include "SearchAnalysis/TraitCatalog.hpp"
#include "Setting/SettingsCache.hpp"

namespace SearchAnalysis {

namespace {

static const std::array<const char *, 38> kWorldPrefixes = {
    "SNDST-A-",  "OCAN-A-",    "S-FRZ-",     "LUSH-A-",    "FRST-A-",
    "VOLCA-",    "BAD-A-",     "HTFST-A-",   "OASIS-A-",   "CER-A-",
    "CERS-A-",   "PRE-A-",     "PRES-A-",    "V-SNDST-C-", "V-OCAN-C-",
    "V-SWMP-C-", "V-SFRZ-C-",  "V-LUSH-C-",  "V-FRST-C-",  "V-VOLCA-C-",
    "V-BAD-C-",  "V-HTFST-C-", "V-OASIS-C-", "V-CER-C-",   "V-CERS-C-",
    "V-PRE-C-",  "V-PRES-C-",  "SNDST-C-",   "PRE-C-",     "CER-C-",
    "FRST-C-",   "SWMP-C-",    "M-SWMP-C-",  "M-BAD-C-",   "M-FRZ-C-",
    "M-FLIP-C-", "M-RAD-C-",   "M-CERS-C-"};

std::string ToMixingSlotType(int type)
{
    switch (type) {
    case 0:
        return "dlc";
    case 1:
        return "world";
    case 2:
        return "subworld";
    default:
        return "unknown";
    }
}

void FillMixingSlotLabel(const SettingsCache &settings,
                         const MixingConfig &config,
                         MixingSlotMeta *slot)
{
    if (slot == nullptr) {
        return;
    }

    slot->name = config.path;
    slot->description.clear();

    if (config.type == 1) {
        const auto itr = settings.worldMixing.find(config.path);
        if (itr != settings.worldMixing.end()) {
            slot->name = itr->second.name.empty() ? config.path : itr->second.name;
            slot->description = itr->second.description;
        }
        return;
    }

    if (config.type == 2) {
        const auto itr = settings.subworldMixing.find(config.path);
        if (itr != settings.subworldMixing.end()) {
            slot->name = itr->second.name.empty() ? config.path : itr->second.name;
            slot->description = itr->second.description;
        }
        return;
    }

    if (config.path == "DLC2_ID") {
        slot->name = "The Frosty Planet Pack";
    } else if (config.path == "DLC3_ID") {
        slot->name = "The Bionic Booster Pack";
    } else if (config.path == "DLC4_ID") {
        slot->name = "The Prehistoric Planet Pack";
    }
}

void AddParameterSpec(std::vector<ParameterSpec> *specs,
                      std::string id,
                      std::string valueType,
                      std::string meaning,
                      std::string staticRange,
                      bool supportsDynamicRange,
                      std::string source)
{
    if (specs == nullptr) {
        return;
    }
    specs->push_back(ParameterSpec{
        .id = std::move(id),
        .valueType = std::move(valueType),
        .meaning = std::move(meaning),
        .staticRange = std::move(staticRange),
        .supportsDynamicRange = supportsDynamicRange,
        .source = std::move(source),
    });
}

std::vector<ParameterSpec> BuildParameterSpecs()
{
    std::vector<ParameterSpec> specs;
    specs.reserve(9);
    AddParameterSpec(&specs,
                     "worldType",
                     "enum",
                     "世界前缀索引",
                     "0..worldCatalog.length-1",
                     true,
                     "src/entry_sidecar.cpp");
    AddParameterSpec(&specs,
                     "mixing",
                     "base5-encoded-int",
                     "11 个 mixing slot 的 base-5 编码",
                     "0..48828124",
                     true,
                     "SettingsCache::ParseAndApplyMixingSettingsCode");
    AddParameterSpec(&specs,
                     "seedStart",
                     "int",
                     "搜索起始 seed（闭区间）",
                     "0..INT_MAX",
                     false,
                     "sidecar search request");
    AddParameterSpec(&specs,
                     "seedEnd",
                     "int",
                     "搜索结束 seed（闭区间）",
                     "0..INT_MAX",
                     false,
                     "sidecar search request");
    AddParameterSpec(&specs,
                     "required",
                     "geyserId[]",
                     "指定喷口至少出现一次",
                     "成员必须来自 geyserCatalog",
                     true,
                     "BatchMatcher");
    AddParameterSpec(&specs,
                     "forbidden",
                     "geyserId[]",
                     "指定喷口不能出现",
                     "成员必须来自 geyserCatalog",
                     true,
                     "BatchMatcher");
    AddParameterSpec(&specs,
                     "distance",
                     "{geyser,minDist,maxDist}[]",
                     "喷口到起点距离约束",
                     "minDist>=0, maxDist>=0, minDist<=maxDist",
                     true,
                     "BatchMatcher");
    AddParameterSpec(&specs,
                     "cpu",
                     "object",
                     "搜索执行策略参数",
                     "按 FilterConfig/SidecarProtocol clamp",
                     false,
                     "src/Batch/FilterConfig.*");
    AddParameterSpec(&specs,
                     "traitCatalog",
                     "catalog",
                     "trait 目录（本期只读，不参与搜索约束）",
                     "searchable=false",
                     false,
                     "asset/worldgen/traits/*.json");
    return specs;
}

} // namespace

const std::vector<std::string> &GetWorldPrefixes()
{
    static const std::vector<std::string> prefixes = [] {
        std::vector<std::string> result;
        result.reserve(kWorldPrefixes.size());
        for (const char *code : kWorldPrefixes) {
            result.emplace_back(code);
        }
        return result;
    }();
    return prefixes;
}

SearchCatalog BuildSearchCatalog(const SettingsCache &settings)
{
    SearchCatalog catalog;
    const auto &worldPrefixes = GetWorldPrefixes();
    catalog.worlds.reserve(worldPrefixes.size());
    for (size_t i = 0; i < worldPrefixes.size(); ++i) {
        catalog.worlds.push_back(WorldCatalogItem{
            .id = static_cast<int>(i),
            .code = worldPrefixes[i],
        });
    }

    const auto &geyserIds = Batch::GetGeyserIds();
    catalog.geysers.reserve(geyserIds.size());
    for (size_t i = 0; i < geyserIds.size(); ++i) {
        catalog.geysers.push_back(GeyserCatalogItem{
            .id = static_cast<int>(i),
            .key = geyserIds[i],
        });
    }

    catalog.traits = BuildTraitCatalog(settings);

    catalog.mixingSlots.reserve(settings.mixConfigs.size());
    for (size_t i = 0; i < settings.mixConfigs.size(); ++i) {
        const auto &config = settings.mixConfigs[i];
        MixingSlotMeta slot;
        slot.slot = static_cast<int>(i);
        slot.path = config.path;
        slot.type = ToMixingSlotType(config.type);
        FillMixingSlotLabel(settings, config, &slot);
        catalog.mixingSlots.push_back(std::move(slot));
    }

    catalog.parameterSpecs = BuildParameterSpecs();
    return catalog;
}

} // namespace SearchAnalysis
