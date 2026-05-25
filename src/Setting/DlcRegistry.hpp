#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace DlcRegistry {

struct DlcMeta {
    std::string_view id;
    std::string_view storageKey;
    std::string_view archivePrefix;
    std::string_view resourcePrefix;
    std::string_view displayName;
    uint32_t stateBit = 0;
    std::span<const std::string_view> worldPrefixes;
    std::span<const std::string_view> mixingPrefixes;
};

std::span<const DlcMeta> GetAll();

const DlcMeta *FindById(std::string_view id);
const DlcMeta *FindByStorageKey(std::string_view storageKey);
const DlcMeta *FindByArchivePath(std::string_view archivePath);
const DlcMeta *FindByResourcePath(std::string_view resourcePath);
const DlcMeta *FindByWorldPrefix(std::string_view worldPrefix);

} // namespace DlcRegistry
