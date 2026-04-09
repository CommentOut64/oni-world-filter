#pragma once

#include <string>
#include <vector>

namespace Batch {

struct FilterConfig {
    struct CpuConfig {
        std::string mode = "balanced";
        int workers = 0;
        bool allowSmt = true;
        bool allowLowPerf = false;
        std::string placement = "preferred";
        bool enableWarmup = true;
        int warmupTotalMs = 10000;
        int warmupPerCandidateMs = 2500;
        int warmupSeedCount = 4000;
        double warmupTieTolerance = 0.03;
        int warmupMinSampledSeeds = 512;
        int warmupMaxRetry = 2;
        bool enableAdaptiveDown = true;
        int adaptiveMinWorkers = 1;
        double adaptiveDropThreshold = 0.12;
        int adaptiveDropWindows = 3;
        int adaptiveCooldownMs = 8000;
        int sampleWindowMs = 2000;
        int chunkSize = 64;
        int progressInterval = 1000;
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
};

enum class FilterErrorCode {
    FileOpenFailed,
    JsonParseFailed,
    InvalidSeedRange,
    UnknownRequiredGeyserId,
    UnknownForbiddenGeyserId,
    MissingDistanceField,
    UnknownDistanceGeyserId,
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
