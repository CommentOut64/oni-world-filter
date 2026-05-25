#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "Geyser/GeyserKind.hpp"

namespace Geyser {

struct CatalogEntry {
    int id{};
    std::string_view key;
    std::string_view displayName;
    GeyserKind kind{GeyserKind::Unknown};
    std::string_view parameterSource;
    bool supportsDynamicParameters{false};
    bool supportsCoordinateParameters{false};
    bool usesGenericParameterAlgorithm{false};
    std::string_view templateEntityId;
};

const std::vector<CatalogEntry> &GetCatalog();
const CatalogEntry *FindById(int id);
const CatalogEntry *FindByKey(std::string_view key);
const CatalogEntry *FindByTemplateEntityId(std::string_view id);
int FindIdByKey(std::string_view key);
const std::vector<int> &GetGenericRandomPoolIds(bool spaceOutEnabled);
const std::vector<std::string> &GetCatalogKeys();
std::string ResolveDisplayName(std::string_view key);

} // namespace Geyser
