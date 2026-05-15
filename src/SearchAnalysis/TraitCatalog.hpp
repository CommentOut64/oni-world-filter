#pragma once

#include <string>
#include <vector>

#include "SearchAnalysis/SearchCatalog.hpp"

class SettingsCache;

namespace SearchAnalysis {

std::vector<TraitMeta> BuildTraitCatalog(const SettingsCache &settings);
int ResolveTraitSummaryIndexById(const SettingsCache &settings, const std::string &traitId);

} // namespace SearchAnalysis
