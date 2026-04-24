#pragma once

#include <vector>

namespace SearchAnalysis {

std::vector<double> ComputePoissonBinomialPmf(const std::vector<double> &probabilities);

double ComputePoissonBinomialRangeProbability(const std::vector<double> &probabilities,
                                              int minCount,
                                              int maxCount);

} // namespace SearchAnalysis
