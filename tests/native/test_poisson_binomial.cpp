#include "SearchAnalysis/PoissonBinomial.hpp"

#include <cmath>
#include <iostream>
#include <vector>

namespace {

bool Expect(bool condition, const char *message, int &failures)
{
    if (condition) {
        return true;
    }
    std::cerr << "[FAIL] " << message << std::endl;
    ++failures;
    return false;
}

bool NearlyEqual(double left, double right, double eps = 1e-9)
{
    return std::fabs(left - right) <= eps;
}

} // namespace

int RunAllTests()
{
    int failures = 0;

    {
        const auto pmf = SearchAnalysis::ComputePoissonBinomialPmf({0.2, 0.5});
        Expect(pmf.size() == 3, "pmf size mismatch", failures);
        Expect(NearlyEqual(pmf[0], 0.4), "pmf[0] should be 0.4", failures);
        Expect(NearlyEqual(pmf[1], 0.5), "pmf[1] should be 0.5", failures);
        Expect(NearlyEqual(pmf[2], 0.1), "pmf[2] should be 0.1", failures);
    }

    {
        const double range = SearchAnalysis::ComputePoissonBinomialRangeProbability({0.2, 0.5},
                                                                                     1,
                                                                                     1);
        Expect(NearlyEqual(range, 0.5), "range probability [1,1] should be 0.5", failures);
    }

    {
        const double range = SearchAnalysis::ComputePoissonBinomialRangeProbability({0.1, 0.2, 0.3},
                                                                                     2,
                                                                                     3);
        Expect(NearlyEqual(range, 0.098), "range probability [2,3] should be 0.098", failures);
    }

    {
        const double range = SearchAnalysis::ComputePoissonBinomialRangeProbability({},
                                                                                     0,
                                                                                     0);
        Expect(NearlyEqual(range, 1.0), "empty probability vector [0,0] should be 1", failures);
    }

    if (failures == 0) {
        std::cout << "[PASS] test_poisson_binomial" << std::endl;
        return 0;
    }
    return 1;
}
