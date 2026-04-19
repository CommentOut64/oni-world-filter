#pragma once

#include <string>
#include <vector>

#include "SearchAnalysis/SearchConstraintModel.hpp"

namespace SearchAnalysis {

ValidationIssue BuildLowProbabilityWarning(double probabilityUpper,
                                           const std::vector<std::string> &bottlenecks,
                                           bool strongWarning,
                                           bool hasLowConfidenceDistance);

ValidationIssue BuildGenericCapacityPrunedWarning(const std::vector<std::string> &groups,
                                                  int demandedSlots,
                                                  int capacityUpper);

ValidationIssue BuildDependencyFallbackWarning(const std::vector<std::string> &groups);

} // namespace SearchAnalysis
