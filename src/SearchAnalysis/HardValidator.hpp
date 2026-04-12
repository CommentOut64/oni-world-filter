#pragma once

#include "SearchAnalysis/SearchCatalog.hpp"
#include "SearchAnalysis/SearchConstraintModel.hpp"

namespace SearchAnalysis {

SearchAnalysisResult RunSearchAnalysis(const SearchAnalysisRequest &request,
                                       const SearchCatalog &catalog);

} // namespace SearchAnalysis
