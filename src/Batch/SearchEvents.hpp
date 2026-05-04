#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "App/ResultSink.hpp"
#include "BatchCpu/CpuOptimization.hpp"

namespace Batch {

enum class SearchEventType {
    SearchStarted,
    SearchProgress,
    SearchMatch,
    SearchCompleted,
    SearchFailed,
    SearchCancelled,
};

struct SearchStartedEvent {
    SearchEventType type = SearchEventType::SearchStarted;
    int seedStart = 0;
    int seedEnd = -1;
    int totalSeeds = 0;
    int workerCount = 1;
};

struct SearchProgressEvent {
    SearchEventType type = SearchEventType::SearchProgress;
    int processedSeeds = 0;
    int totalSeeds = 0;
    int totalMatches = 0;
    int activeWorkers = 0;
    double windowSeedsPerSecond = 0.0;
    bool hasWindowSample = false;
    bool activeWorkersReduced = false;
    double peakSeedsPerSecond = 0.0;
};

struct SearchMatchEvent {
    SearchEventType type = SearchEventType::SearchMatch;
    int seed = 0;
    std::string coord;
    BatchCaptureRecord capture;
    int processedSeeds = 0;
    int totalSeeds = 0;
    int totalMatches = 0;
};

struct SearchCompletedEvent {
    SearchEventType type = SearchEventType::SearchCompleted;
    int processedSeeds = 0;
    int totalSeeds = 0;
    int totalMatches = 0;
    int finalActiveWorkers = 0;
    uint32_t autoFallbackCount = 0;
    bool stoppedByBudget = false;
    BatchCpu::ThroughputStats throughput{};
};

struct SearchFailedEvent {
    SearchEventType type = SearchEventType::SearchFailed;
    std::string message;
    int processedSeeds = 0;
    int totalSeeds = 0;
};

struct SearchCancelledEvent {
    SearchEventType type = SearchEventType::SearchCancelled;
    int processedSeeds = 0;
    int totalSeeds = 0;
    int totalMatches = 0;
    int finalActiveWorkers = 0;
    BatchCpu::ThroughputStats throughput{};
};

struct SearchEventCallbacks {
    std::function<void(const SearchStartedEvent &)> onStarted;
    std::function<void(const SearchProgressEvent &)> onProgress;
    std::function<void(const SearchMatchEvent &)> onMatch;
    std::function<void(const SearchCompletedEvent &)> onCompleted;
    std::function<void(const SearchFailedEvent &)> onFailed;
    std::function<void(const SearchCancelledEvent &)> onCancelled;
};

} // namespace Batch
