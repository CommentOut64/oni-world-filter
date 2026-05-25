#pragma once

#include "App/ResultModels.hpp"

namespace Geyser {

struct CatalogEntry;

bool BuildDetailFromCatalog(const CatalogEntry *entry,
                            int index,
                            const GeyserSummary &summary,
                            int geyserSeed,
                            int worldHeight,
                            int worldOffsetX,
                            int worldOffsetY,
                            GeyserDetail *detail);

} // namespace Geyser
