#pragma once

#include <string>
#include <vector>

#include "App/ResultModels.hpp"
#include "Batch/FilterConfig.hpp"
#include "Batch/SearchEvents.hpp"

namespace Batch {

enum class SidecarCommandType {
    Unknown,
    Search,
    Preview,
    Cancel,
};

struct SidecarDistanceRule {
    std::string geyserId;
    float minDist = 0.0f;
    float maxDist = 0.0f;
};

struct SidecarConstraints {
    std::vector<std::string> required;
    std::vector<std::string> forbidden;
    std::vector<SidecarDistanceRule> distance;
};

struct SidecarSearchRequest {
    std::string jobId;
    int worldType = 0;
    int seedStart = 1;
    int seedEnd = 100000;
    int mixing = 0;
    int threads = 0;
    SidecarConstraints constraints;
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

struct SidecarRequest {
    SidecarCommandType command = SidecarCommandType::Unknown;
    SidecarSearchRequest search;
    SidecarPreviewRequest preview;
    SidecarCancelRequest cancel;
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

} // namespace Batch
