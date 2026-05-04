#define main sidecar_entry_main_for_test
#include "../../src/entry_sidecar.cpp"
#undef main

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

void Expect(bool condition, const std::string &message, std::vector<std::string> *failures)
{
    if (condition || failures == nullptr) {
        return;
    }
    failures->push_back(message);
}

} // namespace

int RunAllTests()
{
    std::vector<std::string> failures;

    {
        auto cancelToken = std::make_shared<std::atomic<bool>>(false);
        auto activeWorkerCap = std::make_shared<std::atomic<int>>(0);
        std::atomic<bool> observedCancel = false;
        BatchCpu::CompiledSearchCpuPlan cpuPlan{};

        std::thread worker([cancelToken, &observedCancel]() {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(400);
            while (std::chrono::steady_clock::now() < deadline) {
                if (cancelToken->load(std::memory_order_relaxed)) {
                    observedCancel.store(true, std::memory_order_relaxed);
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });

        std::string startError;
        const bool started = StartSearchWorker("shutdown-job",
                                               cancelToken,
                                               activeWorkerCap,
                                               cpuPlan,
                                               std::move(worker),
                                               &startError);
        Expect(started, "StartSearchWorker should accept the shutdown regression worker", &failures);
        Expect(startError.empty(), "StartSearchWorker should not report an error for shutdown test", &failures);

        const auto startedAt = std::chrono::steady_clock::now();
        ShutdownSearchWorker();
        const auto elapsed = std::chrono::steady_clock::now() - startedAt;

        Expect(observedCancel.load(std::memory_order_relaxed),
               "ShutdownSearchWorker should request cancellation before joining the worker",
               &failures);
        Expect(elapsed < std::chrono::milliseconds(200),
               "ShutdownSearchWorker should return promptly instead of waiting for a natural worker timeout",
               &failures);
    }

    if (!failures.empty()) {
        for (const auto &failure : failures) {
            std::cerr << "[FAIL] " << failure << std::endl;
        }
        return 1;
    }
    return 0;
}
