#pragma once

#include <string>
#include <vector>

#include "App/ResultModels.hpp"
#include "Batch/FilterConfig.hpp"
#include "Batch/SearchEvents.hpp"
#include "SearchAnalysis/SearchCatalog.hpp"
#include "SearchAnalysis/SearchConstraintModel.hpp"

namespace Batch {

enum class SidecarCommandType {
    Unknown,
    Search,
    Preview,
    Cancel,
    SetSearchActiveWorkers,
    GetSearchCatalog,
    AnalyzeSearchRequest,
};

struct SidecarDistanceRule {
    std::string geyserId;
    float minDist = 0.0f;
    float maxDist = 0.0f;
};

struct SidecarCountRule {
    std::string geyserId;
    int minCount = 0;
    int maxCount = 0;
};

struct SidecarConstraints {
    std::vector<std::string> required;
    std::vector<std::string> forbidden;
    std::vector<SidecarDistanceRule> distance;
    std::vector<SidecarCountRule> count;
};

struct SidecarCpuConfig {
    bool hasValue = false;
    std::string mode = "balanced";
    int workers = 0;
    bool allowSmt = true;
    bool allowLowPerf = false;
    std::string placement = "preferred";
    bool enableWarmup = true;
    bool enableAdaptiveDown = true;
    int chunkSize = 64;
    int progressInterval = 1000;
    int sampleWindowMs = 2000;
    int adaptiveMinWorkers = 1;
    double adaptiveDropThreshold = 0.12;
    int adaptiveDropWindows = 3;
    int adaptiveCooldownMs = 8000;
};

struct SidecarSearchRequest {
    std::string jobId;
    int worldType = 0;
    int seedStart = 1;
    int seedEnd = 100000;
    int mixing = 0;
    int threads = 0;
    SidecarConstraints constraints;
    SidecarCpuConfig cpu;
};

struct SidecarPreviewRequest {
    std::string jobId;
    int worldType = 0;
    int seed = 0;
    int mixing = 0;
};

struct SidecarCancelRequest {
    std::string jobId;
};

struct SidecarSetSearchActiveWorkersRequest {
    std::string jobId;
    int activeWorkers = 0;
};

struct SidecarGetSearchCatalogRequest {
    std::string jobId = "search-catalog";
};

struct SidecarAnalyzeSearchRequest {
    std::string jobId;
    int worldType = 0;
    int seedStart = 1;
    int seedEnd = 100000;
    int mixing = 0;
    int threads = 0;
    SidecarConstraints constraints;
    SidecarCpuConfig cpu;
};

struct SidecarRequest {
    SidecarCommandType command = SidecarCommandType::Unknown;
    SidecarSearchRequest search;
    SidecarPreviewRequest preview;
    SidecarCancelRequest cancel;
    SidecarSetSearchActiveWorkersRequest setSearchActiveWorkers;
    SidecarGetSearchCatalogRequest getSearchCatalog;
    SidecarAnalyzeSearchRequest analyze;
};

struct SidecarParseResult {
    SidecarRequest request;
    std::string error;

    bool Ok() const
    {
        return error.empty() && request.command != SidecarCommandType::Unknown;
    }
};

SidecarParseResult ParseSidecarRequest(const std::string &jsonText);
FilterConfig BuildFilterConfigFromSidecarSearch(const SidecarSearchRequest &request,
                                                std::vector<FilterError> *errors = nullptr);

std::string SerializeStartedEvent(const std::string &jobId,
                                  const SearchStartedEvent &event);
std::string SerializeProgressEvent(const std::string &jobId,
                                   const SearchProgressEvent &event);
std::string SerializeMatchEvent(const std::string &jobId,
                                const SearchMatchEvent &event);
std::string SerializeCompletedEvent(const std::string &jobId,
                                    const SearchCompletedEvent &event);
std::string SerializeFailedEvent(const std::string &jobId,
                                 const SearchFailedEvent &event);
std::string SerializeFailedEvent(const std::string &jobId,
                                 const std::string &message);
std::string SerializeCancelledEvent(const std::string &jobId,
                                    const SearchCancelledEvent &event);
std::string SerializePreviewEvent(const std::string &jobId,
                                  const SidecarPreviewRequest &request,
                                  const GeneratedWorldPreview &preview);
std::string SerializeSearchCatalogEvent(const std::string &jobId,
                                        const SearchAnalysis::SearchCatalog &catalog);
std::string SerializeSearchAnalysisEvent(const std::string &jobId,
                                         const SearchAnalysis::SearchAnalysisResult &analysis);

} // namespace Batch
