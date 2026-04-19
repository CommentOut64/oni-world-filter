#pragma once

#include <vector>

#include "SearchAnalysis/SearchConstraintModel.hpp"

namespace SearchAnalysis {

void RunBottleneckSelectivityPredictor(const NormalizedSearchRequest &request,
                                       const WorldEnvelopeProfile *worldProfile,
                                       std::vector<ValidationIssue> *warnings,
                                       std::vector<std::string> *bottlenecks,
                                       double *predictedBottleneckProbability);

} // namespace SearchAnalysis
