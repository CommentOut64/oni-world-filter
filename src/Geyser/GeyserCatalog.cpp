#include "Geyser/GeyserCatalog.hpp"

#include <array>

namespace Geyser {

namespace {

struct CatalogInitEntry {
    std::string_view key;
    std::string_view displayName;
    GeyserKind kind;
    std::string_view parameterSource;
    bool supportsDynamicParameters;
    bool supportsCoordinateParameters;
    bool usesGenericParameterAlgorithm;
    std::string_view templateEntityId;
};

constexpr std::array<CatalogInitEntry, 35> kCatalogInit = {
    CatalogInitEntry{"steam", "Cool Steam Vent", GeyserKind::GenericGeyser, "generic_config", true, true, true, ""},
    CatalogInitEntry{"hot_steam", "Steam Vent", GeyserKind::GenericGeyser, "generic_config", true, true, true, ""},
    CatalogInitEntry{"hot_water", "Water Geyser", GeyserKind::GenericGeyser, "generic_config", true, true, true, ""},
    CatalogInitEntry{"slush_water", "Cool Slush Geyser", GeyserKind::GenericGeyser, "generic_config", true, true, true, ""},
    CatalogInitEntry{"filthy_water", "Polluted Water Vent", GeyserKind::GenericGeyser, "generic_config", true, true, true, ""},
    CatalogInitEntry{"slush_salt_water", "Cool Salt Slush Geyser", GeyserKind::GenericGeyser, "generic_config", true, true, true, ""},
    CatalogInitEntry{"salt_water", "Salt Water Geyser", GeyserKind::GenericGeyser, "generic_config", true, true, true, ""},
    CatalogInitEntry{"small_volcano", "Minor Volcano", GeyserKind::GenericGeyser, "generic_config", true, true, true, ""},
    CatalogInitEntry{"big_volcano", "Volcano", GeyserKind::GenericGeyser, "generic_config", true, true, true, ""},
    CatalogInitEntry{"liquid_co2", "Carbon Dioxide Geyser", GeyserKind::GenericGeyser, "generic_config", true, true, true, ""},
    CatalogInitEntry{"hot_co2", "Carbon Dioxide Vent", GeyserKind::GenericGeyser, "generic_config", true, true, true, ""},
    CatalogInitEntry{"hot_hydrogen", "Hydrogen Vent", GeyserKind::GenericGeyser, "generic_config", true, true, true, ""},
    CatalogInitEntry{"hot_po2", "Hot Polluted Oxygen Vent", GeyserKind::GenericGeyser, "generic_config", true, true, true, ""},
    CatalogInitEntry{"slimy_po2", "InFectious Polluted Oxygen Vent", GeyserKind::GenericGeyser, "generic_config", true, true, true, ""},
    CatalogInitEntry{"chlorine_gas", "Chlorine Gas Vent", GeyserKind::GenericGeyser, "generic_config", true, true, true, ""},
    CatalogInitEntry{"methane", "Natural Gas Geyser", GeyserKind::GenericGeyser, "generic_config", true, true, true, ""},
    CatalogInitEntry{"molten_copper", "Copper Volcano", GeyserKind::GenericGeyser, "generic_config", true, true, true, ""},
    CatalogInitEntry{"molten_iron", "Iron Volcano", GeyserKind::GenericGeyser, "generic_config", true, true, true, ""},
    CatalogInitEntry{"molten_gold", "Gold Volcano", GeyserKind::GenericGeyser, "generic_config", true, true, true, ""},
    CatalogInitEntry{"molten_aluminum", "Aluminum Volcano", GeyserKind::GenericGeyser, "generic_config", true, true, true, ""},
    CatalogInitEntry{"molten_cobalt", "Cobalt Volcano", GeyserKind::GenericGeyser, "generic_config", true, true, true, ""},
    CatalogInitEntry{"oil_drip", "Leaky Oil Fissure", GeyserKind::GenericGeyser, "generic_config", true, true, true, ""},
    CatalogInitEntry{"liquid_sulfur", "Liquid Sulfur Geyser", GeyserKind::GenericGeyser, "generic_config", true, true, true, ""},
    CatalogInitEntry{"chlorine_gas_cool", "Cool Chlorine Gas Vent", GeyserKind::FixedTemplateGeyser, "generic_config", true, true, true, ""},
    CatalogInitEntry{"molten_tungsten", "Tungsten Volcano", GeyserKind::FixedTemplateGeyser, "generic_config", true, true, true, ""},
    CatalogInitEntry{"molten_niobium", "Niobium Volcano", GeyserKind::FixedTemplateGeyser, "generic_config", true, true, true, ""},
    CatalogInitEntry{"printing_pod", "Printing Pod", GeyserKind::Facility, "printing_pod", false, false, false, ""},
    CatalogInitEntry{"oil_reservoir", "Oil Reservoir", GeyserKind::Reservoir, "oil_reservoir", false, false, false, "OilWell"},
    CatalogInitEntry{"warp_sender", "Supply Teleporter Output", GeyserKind::WarpPortal, "warp_facility", false, false, false, "WarpConduitSender"},
    CatalogInitEntry{"warp_receiver", "Supply Teleporter Input", GeyserKind::WarpPortal, "warp_facility", false, false, false, "WarpConduitReceiver"},
    CatalogInitEntry{"warp_portal", "Teleporter Transmitter", GeyserKind::WarpPortal, "warp_facility", false, false, false, "WarpPortal"},
    CatalogInitEntry{"cryo_tank", "Cryotank 3000", GeyserKind::Cryopod, "cryopod", false, false, false, "CryoTank"},
    CatalogInitEntry{"murky_brine", "Polluted Brine Vent", GeyserKind::Aquatic, "generic_config", true, true, true, "GeyserGeneric_murky_brine"},
    CatalogInitEntry{"small_reef_geyser", "Tidal Spring", GeyserKind::Aquatic, "small_reef_geyser_runtime", false, false, false, "SmallReefGeyser"},
    CatalogInitEntry{"underwater_vent", "Thermal Gas Fissure", GeyserKind::Aquatic, "underwater_vent_runtime", false, false, false, "UnderwaterVent"},
};

} // namespace

const std::vector<CatalogEntry> &GetCatalog()
{
    static const std::vector<CatalogEntry> entries = [] {
        std::vector<CatalogEntry> result;
        result.reserve(kCatalogInit.size());
        for (size_t index = 0; index < kCatalogInit.size(); ++index) {
            const auto &item = kCatalogInit[index];
            result.push_back(CatalogEntry{
                .id = static_cast<int>(index),
                .key = item.key,
                .displayName = item.displayName,
                .kind = item.kind,
                .parameterSource = item.parameterSource,
                .supportsDynamicParameters = item.supportsDynamicParameters,
                .supportsCoordinateParameters = item.supportsCoordinateParameters,
                .usesGenericParameterAlgorithm = item.usesGenericParameterAlgorithm,
                .templateEntityId = item.templateEntityId,
            });
        }
        return result;
    }();
    return entries;
}

const CatalogEntry *FindById(int id)
{
    const auto &entries = GetCatalog();
    if (id < 0 || id >= static_cast<int>(entries.size())) {
        return nullptr;
    }
    return &entries[static_cast<size_t>(id)];
}

const CatalogEntry *FindByKey(std::string_view key)
{
    const auto &entries = GetCatalog();
    for (const auto &entry : entries) {
        if (entry.key == key) {
            return &entry;
        }
    }
    return nullptr;
}

const CatalogEntry *FindByTemplateEntityId(std::string_view id)
{
    const auto &entries = GetCatalog();
    for (const auto &entry : entries) {
        if (!entry.templateEntityId.empty() && entry.templateEntityId == id) {
            return &entry;
        }
    }
    return nullptr;
}

int FindIdByKey(std::string_view key)
{
    const auto *entry = FindByKey(key);
    return entry == nullptr ? -1 : entry->id;
}

const std::vector<int> &GetGenericRandomPoolIds(bool spaceOutEnabled)
{
    static const std::vector<int> kBase = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
        10, 11, 12, 13, 14, 15, 16, 17, 18, 21,
    };
    static const std::vector<int> kSpaceOut = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
        10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
        20, 21, 22,
    };
    return spaceOutEnabled ? kSpaceOut : kBase;
}

const std::vector<std::string> &GetCatalogKeys()
{
    static const std::vector<std::string> keys = [] {
        std::vector<std::string> result;
        result.reserve(GetCatalog().size());
        for (const auto &entry : GetCatalog()) {
            result.emplace_back(entry.key);
        }
        return result;
    }();
    return keys;
}

std::string ResolveDisplayName(std::string_view key)
{
    const auto *entry = FindByKey(key);
    if (entry == nullptr) {
        return std::string(key);
    }
    return std::string(entry->displayName);
}

} // namespace Geyser
