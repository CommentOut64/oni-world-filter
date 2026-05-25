#pragma once

#include <string_view>

#include "SearchAnalysis/SearchCatalog.hpp"
#include "SearchAnalysis/SearchConstraintModel.hpp"

namespace SearchAnalysis {

SearchAnalysisResult RunSearchAnalysis(const SearchAnalysisRequest &request,
                                       const SearchCatalog &catalog,
                                       const WorldEnvelopeProfile *worldProfile = nullptr,
                                       std::string_view worldProfileErrorMessage = {});

} // namespace SearchAnalysis
