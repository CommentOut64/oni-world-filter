#pragma once

#include <string>

#include "Batch/SearchEvents.hpp"
#include "Batch/SearchRequest.hpp"

namespace Batch {

struct BatchSearchResult {
    int totalSeeds = 0;
    int processedSeeds = 0;
    int totalMatches = 0;
    int finalActiveWorkers = 0;
    uint32_t autoFallbackCount = 0;
    bool stoppedByBudget = false;
    bool cancelled = false;
    bool failed = false;
    std::string failureMessage;
    BatchCpu::ThroughputStats throughput{};
};

class BatchSearchService
{
public:
    static BatchSearchResult Run(const SearchRequest &request,
                                 const SearchEventCallbacks &callbacks = {});
};

} // namespace Batch
