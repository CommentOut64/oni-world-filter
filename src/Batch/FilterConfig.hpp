#pragma once

#include <string>
#include <vector>

namespace Batch {

struct FilterConfig {
    struct CpuConfig {
        std::string mode = "balanced";
        bool allowSmt = true;
        bool allowLowPerf = false;
        std::string placement = "preferred";
        bool printMatches = true;
        bool printProgress = true;
        bool benchmarkSilent = false;
        bool printDiagnostics = true;
    };

    struct DistRule {
        int type = -1;
        float minDist = 0.0f;
        float maxDist = 1e9f;
    };

    struct CountRule {
        int type = -1;
        int minCount = 0;
        int maxCount = 0;
    };

    int worldType = 0;
    int seedStart = 1;
    int seedEnd = 100000;
    int mixing = 0;
    int threads = 0;
    bool hasCpuSection = false;
    CpuConfig cpu;
    std::vector<int> required;
    std::vector<int> forbidden;
    std::vector<DistRule> distanceRules;
    std::vector<CountRule> countRules;
};

enum class FilterErrorCode {
    FileOpenFailed,
    JsonParseFailed,
    InvalidSeedRange,
    UnknownRequiredGeyserId,
    UnknownForbiddenGeyserId,
    MissingDistanceField,
    UnknownDistanceGeyserId,
    MissingCountField,
    UnknownCountGeyserId,
    InvalidCountRange,
};

struct FilterError {
    FilterErrorCode code{};
    std::string field;
    std::string detail;
};

struct FilterConfigLoadResult {
    FilterConfig config;
    std::vector<FilterError> errors;

    bool Ok() const
    {
        return errors.empty();
    }
};

const std::vector<std::string> &GetGeyserIds();
int GeyserIdToIndex(const std::string &id);
std::string FormatFilterError(const FilterError &error);
FilterConfigLoadResult LoadFilterConfig(const std::string &path);

} // namespace Batch
