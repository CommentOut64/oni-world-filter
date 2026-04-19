#pragma once

#include <vector>

#include "SearchAnalysis/SearchCatalog.hpp"

class SettingsCache;

namespace SearchAnalysis {

std::vector<TraitMeta> BuildTraitCatalog(const SettingsCache &settings);

} // namespace SearchAnalysis
