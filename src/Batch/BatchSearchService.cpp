#include "Batch/BatchSearchService.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <exception>
#include <mutex>
#include <numeric>
#include <thread>
#include <vector>

namespace Batch {

namespace {

double ComputeStdDev(const std::vector<double> &samples, double avg)
{
    if (samples.size() <= 1) {
        return 0.0;
    }

    double sumSquares = 0.0;
    for (double sample : samples) {
        const double diff = sample - avg;
        sumSquares += diff * diff;
    }
    return std::sqrt(sumSquares / static_cast<double>(samples.size()));
}

bool IsCancellationRequested(const SearchRequest &request)
{
    return request.cancelRequested != nullptr &&
           request.cancelRequested->load(std::memory_order_relaxed);
}

} // namespace

BatchSearchResult BatchSearchService::Run(const SearchRequest &request,
                                          const SearchEventCallbacks &callbacks)
{
    BatchSearchResult result;
    if (request.seedEnd < request.seedStart) {
        return result;
    }
    result.totalSeeds = request.seedEnd - request.seedStart + 1;

    if (!request.evaluateSeed) {
        result.failed = true;
        result.failureMessage = "search evaluator is not set";
        if (callbacks.onFailed) {
            SearchFailedEvent event;
            event.message = result.failureMessage;
            event.processedSeeds = 0;
            event.totalSeeds = result.totalSeeds;
            callbacks.onFailed(event);
        }
        return result;
    }

    const int workerCount = std::max<int>(1, static_cast<int>(request.workerCount));
    const int chunkSize = std::max(1, request.chunkSize);
    const int progressInterval = std::max(1, request.progressInterval);
    const auto sampleWindow = std::max(request.sampleWindow, std::chrono::milliseconds(1));

    std::atomic<int> nextSeed{request.seedStart};
    std::atomic<int> processed{0};
    std::atomic<int> totalMatches{0};
    std::atomic<int> adaptiveWorkers{workerCount};
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> hitBudget{false};
    std::atomic<bool> failed{false};
    std::atomic<bool> cancelled{false};
    std::string failureMessage;
    std::mutex failureMutex;
    std::vector<double> sampledThroughput;
    sampledThroughput.reserve(64);

    BatchCpu::AdaptiveConfig adaptive = request.adaptiveConfig;
    adaptive.enabled = request.enableAdaptive && adaptive.enabled;
    adaptive.minWorkers = std::max<uint32_t>(1u, adaptive.minWorkers);
    adaptive.minWorkers = std::min<uint32_t>(adaptive.minWorkers,
                                             static_cast<uint32_t>(workerCount));
    BatchCpu::AdaptiveConcurrencyController controller(adaptive,
                                                       static_cast<uint32_t>(workerCount));
    BatchCpu::RecoveryConfig recovery = request.recoveryConfig;
    recovery.enabled = request.enableRecovery && recovery.enabled;
    BatchCpu::BoundedRecoveryController recoveryController(
        recovery,
        static_cast<uint32_t>(workerCount));

    auto currentActiveWorkers = [&]() {
        int workers = adaptiveWorkers.load(std::memory_order_relaxed);
        if (request.activeWorkerCap != nullptr) {
            const int cap = request.activeWorkerCap->load(std::memory_order_relaxed);
            if (cap > 0) {
                workers = std::min(workers, cap);
            }
        }
        return std::clamp(workers, 1, workerCount);
    };

    auto isExternallyCapped = [&]() {
        if (request.activeWorkerCap == nullptr) {
            return false;
        }
        const int cap = request.activeWorkerCap->load(std::memory_order_relaxed);
        return cap > 0 && cap < adaptiveWorkers.load(std::memory_order_relaxed);
    };

    const auto startedAt = std::chrono::steady_clock::now();
    auto deadline = startedAt;
    if (request.maxRunDuration.count() > 0) {
        deadline = startedAt + request.maxRunDuration;
    }

    auto markFailure = [&](const std::string &message) {
        failed.store(true, std::memory_order_relaxed);
        stopRequested.store(true, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(failureMutex);
        if (failureMessage.empty()) {
            failureMessage = message.empty() ? "search execution failed" : message;
        }
    };

    if (callbacks.onStarted) {
        SearchStartedEvent event;
        event.seedStart = request.seedStart;
        event.seedEnd = request.seedEnd;
        event.totalSeeds = result.totalSeeds;
        event.workerCount = workerCount;
        callbacks.onStarted(event);
    }

    auto worker = [&](int workerIndex) {
        try {
            if (request.initializeWorker) {
                request.initializeWorker();
            }
        } catch (const std::exception &ex) {
            markFailure(ex.what());
            return;
        } catch (...) {
            markFailure("worker initialization failed");
            return;
        }

        if (request.applyThreadPlacement) {
            std::string placementError;
            request.applyThreadPlacement(static_cast<uint32_t>(workerIndex), &placementError);
        }

        while (true) {
            if (stopRequested.load(std::memory_order_relaxed)) {
                break;
            }
            if (IsCancellationRequested(request)) {
                cancelled.store(true, std::memory_order_relaxed);
                stopRequested.store(true, std::memory_order_relaxed);
                break;
            }
            if (nextSeed.load(std::memory_order_relaxed) > request.seedEnd) {
                break;
            }
            if (workerIndex >= currentActiveWorkers()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            const int chunkStart = nextSeed.fetch_add(chunkSize, std::memory_order_relaxed);
            if (chunkStart > request.seedEnd) {
                break;
            }
            const int chunkEnd = std::min(request.seedEnd, chunkStart + chunkSize - 1);
            for (int seed = chunkStart; seed <= chunkEnd; ++seed) {
                if (stopRequested.load(std::memory_order_relaxed)) {
                    break;
                }
                if (IsCancellationRequested(request)) {
                    cancelled.store(true, std::memory_order_relaxed);
                    stopRequested.store(true, std::memory_order_relaxed);
                    break;
                }

                SearchSeedEvaluation evaluation;
                try {
                    evaluation = request.evaluateSeed(seed);
                } catch (const std::exception &ex) {
                    markFailure(ex.what());
                    break;
                } catch (...) {
                    markFailure("seed evaluation failed with unknown exception");
                    break;
                }
                if (!evaluation.ok) {
                    markFailure(evaluation.errorMessage);
                    break;
                }

                if (evaluation.generated && evaluation.matched) {
                    const int currentMatches =
                        totalMatches.fetch_add(1, std::memory_order_relaxed) + 1;
                    if (callbacks.onMatch) {
                        SearchMatchEvent event;
                        event.seed = seed;
                        event.capture = std::move(evaluation.capture);
                        event.processedSeeds = processed.load(std::memory_order_relaxed);
                        event.totalSeeds = result.totalSeeds;
                        event.totalMatches = currentMatches;
                        callbacks.onMatch(event);
                    }
                }

                const int done = processed.fetch_add(1, std::memory_order_relaxed) + 1;
                if (callbacks.onProgress && done % progressInterval == 0) {
                    SearchProgressEvent event;
                    event.processedSeeds = done;
                    event.totalSeeds = result.totalSeeds;
                    event.totalMatches = totalMatches.load(std::memory_order_relaxed);
                    event.activeWorkers = currentActiveWorkers();
                    callbacks.onProgress(event);
                }

                if (request.maxRunDuration.count() > 0 &&
                    std::chrono::steady_clock::now() >= deadline) {
                    stopRequested.store(true, std::memory_order_relaxed);
                    hitBudget.store(true, std::memory_order_relaxed);
                    break;
                }
            }
        }
    };

    std::thread monitor([&] {
        int lastProcessed = 0;
        auto lastTs = std::chrono::steady_clock::now();

        while (!stopRequested.load(std::memory_order_relaxed)) {
            if (processed.load(std::memory_order_relaxed) >= result.totalSeeds) {
                break;
            }

            const bool externallyCappedAtWindowStart = isExternallyCapped();
            std::this_thread::sleep_for(sampleWindow);
            const auto now = std::chrono::steady_clock::now();

            if (IsCancellationRequested(request)) {
                cancelled.store(true, std::memory_order_relaxed);
                stopRequested.store(true, std::memory_order_relaxed);
            }
            if (request.maxRunDuration.count() > 0 && now >= deadline) {
                stopRequested.store(true, std::memory_order_relaxed);
                hitBudget.store(true, std::memory_order_relaxed);
            }

            const int current = processed.load(std::memory_order_relaxed);
            const int delta = current - lastProcessed;
            const double seconds = std::chrono::duration<double>(now - lastTs).count();
            if (seconds <= 0.0 || delta <= 0) {
                continue;
            }

            const double seedsPerSecond = static_cast<double>(delta) / seconds;
            sampledThroughput.push_back(seedsPerSecond);

            bool reduced = false;
            const bool cappedDuringWindow =
                externallyCappedAtWindowStart || isExternallyCapped();
            if (adaptive.enabled && !cappedDuringWindow) {
                const int effectiveWorkers = currentActiveWorkers();
                const auto nextWorkers = controller.Observe(
                    seedsPerSecond,
                    static_cast<uint32_t>(effectiveWorkers),
                    now);
                if (nextWorkers.has_value() &&
                    static_cast<int>(nextWorkers.value()) <
                        adaptiveWorkers.load(std::memory_order_relaxed)) {
                    adaptiveWorkers.store(static_cast<int>(nextWorkers.value()),
                                          std::memory_order_relaxed);
                    reduced = true;
                }
            }
            if (!reduced && recovery.enabled && !cappedDuringWindow) {
                const int effectiveWorkers = currentActiveWorkers();
                const auto nextWorkers = recoveryController.Observe(
                    seedsPerSecond,
                    static_cast<uint32_t>(effectiveWorkers),
                    now);
                if (nextWorkers.has_value() &&
                    static_cast<int>(nextWorkers.value()) >
                        adaptiveWorkers.load(std::memory_order_relaxed)) {
                    adaptiveWorkers.store(static_cast<int>(nextWorkers.value()),
                                          std::memory_order_relaxed);
                }
            }

            if (callbacks.onProgress) {
                SearchProgressEvent event;
                event.processedSeeds = current;
                event.totalSeeds = result.totalSeeds;
                event.totalMatches = totalMatches.load(std::memory_order_relaxed);
                event.activeWorkers = currentActiveWorkers();
                event.windowSeedsPerSecond = seedsPerSecond;
                event.hasWindowSample = true;
                event.activeWorkersReduced = reduced;
                event.peakSeedsPerSecond = controller.PeakSeedsPerSecond();
                callbacks.onProgress(event);
            }

            lastProcessed = current;
            lastTs = now;
        }
    });

    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(workerCount));
    for (int i = 0; i < workerCount; ++i) {
        workers.emplace_back(worker, i);
    }

    for (auto &thread : workers) {
        thread.join();
    }

    stopRequested.store(true, std::memory_order_relaxed);
    if (monitor.joinable()) {
        monitor.join();
    }

    result.processedSeeds = processed.load(std::memory_order_relaxed);
    result.totalMatches = totalMatches.load(std::memory_order_relaxed);
    result.finalActiveWorkers = currentActiveWorkers();
    result.autoFallbackCount = controller.ReductionCount();
    result.stoppedByBudget = hitBudget.load(std::memory_order_relaxed);
    result.cancelled = cancelled.load(std::memory_order_relaxed);
    result.failed = failed.load(std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock(failureMutex);
        result.failureMessage = failureMessage;
    }

    const auto finishedAt = std::chrono::steady_clock::now();
    const double elapsedSeconds = std::chrono::duration<double>(finishedAt - startedAt).count();
    if (elapsedSeconds > 0.0 && result.processedSeeds > 0) {
        result.throughput.averageSeedsPerSecond =
            static_cast<double>(result.processedSeeds) / elapsedSeconds;
        result.throughput.processedSeeds = static_cast<uint64_t>(result.processedSeeds);
        result.throughput.valid = true;
    }
    if (!sampledThroughput.empty()) {
        const double sampleAvg = std::accumulate(sampledThroughput.begin(),
                                                 sampledThroughput.end(),
                                                 0.0) /
                                 static_cast<double>(sampledThroughput.size());
        result.throughput.stddevSeedsPerSecond =
            ComputeStdDev(sampledThroughput, sampleAvg);
    }

    if (result.failed) {
        if (callbacks.onFailed) {
            SearchFailedEvent event;
            event.message = result.failureMessage;
            event.processedSeeds = result.processedSeeds;
            event.totalSeeds = result.totalSeeds;
            callbacks.onFailed(event);
        }
        return result;
    }

    if (result.cancelled) {
        if (callbacks.onCancelled) {
            SearchCancelledEvent event;
            event.processedSeeds = result.processedSeeds;
            event.totalSeeds = result.totalSeeds;
            event.totalMatches = result.totalMatches;
            event.finalActiveWorkers = result.finalActiveWorkers;
            event.throughput = result.throughput;
            callbacks.onCancelled(event);
        }
        return result;
    }

    if (callbacks.onCompleted) {
        SearchCompletedEvent event;
        event.processedSeeds = result.processedSeeds;
        event.totalSeeds = result.totalSeeds;
        event.totalMatches = result.totalMatches;
        event.finalActiveWorkers = result.finalActiveWorkers;
        event.autoFallbackCount = result.autoFallbackCount;
        event.stoppedByBudget = result.stoppedByBudget;
        event.throughput = result.throughput;
        callbacks.onCompleted(event);
    }

    return result;
}

} // namespace Batch
