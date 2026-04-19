#include "SearchAnalysis/PoissonBinomial.hpp"

#include <algorithm>

namespace SearchAnalysis {

namespace {

double ClampProbability(double value)
{
    if (value < 0.0) {
        return 0.0;
    }
    if (value > 1.0) {
        return 1.0;
    }
    return value;
}

} // namespace

std::vector<double> ComputePoissonBinomialPmf(const std::vector<double> &probabilities)
{
    std::vector<double> dp(probabilities.size() + 1, 0.0);
    dp[0] = 1.0;

    size_t used = 0;
    for (const double rawQ : probabilities) {
        const double q = ClampProbability(rawQ);
        const double keep = 1.0 - q;

        for (size_t k = used + 1; k > 0; --k) {
            dp[k] = dp[k] * keep + dp[k - 1] * q;
        }
        dp[0] *= keep;
        ++used;
    }
    return dp;
}

double ComputePoissonBinomialRangeProbability(const std::vector<double> &probabilities,
                                              int minCount,
                                              int maxCount)
{
    const auto pmf = ComputePoissonBinomialPmf(probabilities);
    const int n = static_cast<int>(probabilities.size());
    const int low = std::max(0, minCount);
    const int high = std::min(n, maxCount);
    if (low > high) {
        return 0.0;
    }

    double sum = 0.0;
    for (int k = low; k <= high; ++k) {
        sum += pmf[static_cast<size_t>(k)];
    }
    return ClampProbability(sum);
}

} // namespace SearchAnalysis
