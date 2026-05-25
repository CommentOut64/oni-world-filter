#include "Setting/DlcRegistry.hpp"

#include <array>
#include <ranges>

namespace DlcRegistry {

namespace {

constexpr std::string_view kExpansion1WorldPrefixes[] = {
    "V-SNDST-C-", "V-OCAN-C-", "V-SWMP-C-",  "V-SFRZ-C-", "V-LUSH-C-",
    "V-FRST-C-",  "V-VOLCA-C-", "V-BAD-C-",  "V-HTFST-C-", "V-OASIS-C-",
    "SNDST-C-",   "PRE-C-",     "CER-C-",    "FRST-C-",    "SWMP-C-",
    "M-SWMP-C-",  "M-BAD-C-",   "M-FRZ-C-",  "M-FLIP-C-",  "M-RAD-C-",
    "M-CERS-C-"};

constexpr std::string_view kExpansion1MixingPrefixes[] = {
    "expansion1::worldMixing/",
    "expansion1::subworldMixing/",
};

constexpr std::string_view kDlc2WorldPrefixes[] = {
    "CER-A-",
    "CERS-A-",
    "V-CER-C-",
    "V-CERS-C-",
    "CER-C-",
    "M-CERS-C-",
};

constexpr std::string_view kDlc2MixingPrefixes[] = {
    "dlc2::worldMixing/",
    "dlc2::subworldMixing/",
};

constexpr std::string_view kDlc3MixingPrefixes[] = {
    "dlc3::worldMixing/",
    "dlc3::subworldMixing/",
};

constexpr std::string_view kDlc4WorldPrefixes[] = {
    "PRE-A-",
    "PRES-A-",
    "V-PRE-C-",
    "V-PRES-C-",
    "PRE-C-",
};

constexpr std::string_view kDlc4MixingPrefixes[] = {
    "dlc4::worldMixing/",
    "dlc4::subworldMixing/",
};

constexpr std::string_view kDlc5WorldPrefixes[] = {
    "AQU-A-",
    "V-AQU-C-",
    "AQU-C-",
};

constexpr std::string_view kDlc5MixingPrefixes[] = {
    "dlc5::worldMixing/",
    "dlc5::subworldMixing/",
};

constexpr std::array<DlcMeta, 5> kDlcMetas = {{
    DlcMeta{
        .id = "EXPANSION1_ID",
        .storageKey = "expansion1",
        .archivePrefix = "dlc/expansion1/",
        .resourcePrefix = "expansion1::",
        .displayName = "Spaced Out!",
        .stateBit = 1u,
        .worldPrefixes = std::span<const std::string_view>(kExpansion1WorldPrefixes),
        .mixingPrefixes = std::span<const std::string_view>(kExpansion1MixingPrefixes),
    },
    DlcMeta{
        .id = "DLC2_ID",
        .storageKey = "dlc2",
        .archivePrefix = "dlc/dlc2/",
        .resourcePrefix = "dlc2::",
        .displayName = "The Frosty Planet Pack",
        .stateBit = 2u,
        .worldPrefixes = std::span<const std::string_view>(kDlc2WorldPrefixes),
        .mixingPrefixes = std::span<const std::string_view>(kDlc2MixingPrefixes),
    },
    DlcMeta{
        .id = "DLC3_ID",
        .storageKey = "dlc3",
        .archivePrefix = "dlc/dlc3/",
        .resourcePrefix = "dlc3::",
        .displayName = "The Bionic Booster Pack",
        .stateBit = 4u,
        .worldPrefixes = {},
        .mixingPrefixes = std::span<const std::string_view>(kDlc3MixingPrefixes),
    },
    DlcMeta{
        .id = "DLC4_ID",
        .storageKey = "dlc4",
        .archivePrefix = "dlc/dlc4/",
        .resourcePrefix = "dlc4::",
        .displayName = "The Prehistoric Planet Pack",
        .stateBit = 8u,
        .worldPrefixes = std::span<const std::string_view>(kDlc4WorldPrefixes),
        .mixingPrefixes = std::span<const std::string_view>(kDlc4MixingPrefixes),
    },
    DlcMeta{
        .id = "DLC5_ID",
        .storageKey = "dlc5",
        .archivePrefix = "dlc/dlc5/",
        .resourcePrefix = "dlc5::",
        .displayName = "Aquatic Package",
        .stateBit = 16u,
        .worldPrefixes = std::span<const std::string_view>(kDlc5WorldPrefixes),
        .mixingPrefixes = std::span<const std::string_view>(kDlc5MixingPrefixes),
    },
}};

template<typename Predicate>
const DlcMeta *FindMeta(Predicate predicate)
{
    const auto itr = std::ranges::find_if(kDlcMetas, predicate);
    if (itr == kDlcMetas.end()) {
        return nullptr;
    }
    return &(*itr);
}

} // namespace

std::span<const DlcMeta> GetAll()
{
    return kDlcMetas;
}

const DlcMeta *FindById(std::string_view id)
{
    return FindMeta([&](const DlcMeta &meta) { return meta.id == id; });
}

const DlcMeta *FindByStorageKey(std::string_view storageKey)
{
    return FindMeta([&](const DlcMeta &meta) { return meta.storageKey == storageKey; });
}

const DlcMeta *FindByArchivePath(std::string_view archivePath)
{
    return FindMeta([&](const DlcMeta &meta) {
        return archivePath.find(meta.archivePrefix) != std::string_view::npos;
    });
}

const DlcMeta *FindByResourcePath(std::string_view resourcePath)
{
    return FindMeta([&](const DlcMeta &meta) {
        return resourcePath.starts_with(meta.resourcePrefix);
    });
}

const DlcMeta *FindByWorldPrefix(std::string_view worldPrefix)
{
    return FindMeta([&](const DlcMeta &meta) {
        return std::ranges::find(meta.worldPrefixes, worldPrefix) != meta.worldPrefixes.end();
    });
}

} // namespace DlcRegistry
