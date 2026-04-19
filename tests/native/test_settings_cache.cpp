#include "Setting/SettingsCache.hpp"
#include "config.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
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

bool ReadAssetBlob(std::vector<char> &data, std::string *error)
{
    std::ifstream file(SETTING_ASSET_FILEPATH, std::ios::binary);
    if (!file.is_open()) {
        if (error != nullptr) {
            *error = "failed to open asset blob";
        }
        return false;
    }
    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    if (size <= 0) {
        if (error != nullptr) {
            *error = "asset blob size is invalid";
        }
        return false;
    }
    data.resize(static_cast<size_t>(size));
    if (!file.read(data.data(), size)) {
        if (error != nullptr) {
            *error = "failed to read asset blob";
        }
        return false;
    }
    return true;
}

} // namespace

int RunAllTests()
{
    int failures = 0;

    SharedSettingsCache::ResetForTests();

    std::atomic<int> loadCalls{0};
    std::vector<std::shared_ptr<const SettingsCache>> snapshots;
    snapshots.reserve(8);
    std::mutex snapshotMutex;
    std::atomic<int> workerFailures{0};

    auto loader = [&](std::vector<char> &data, std::string *error) {
        loadCalls.fetch_add(1, std::memory_order_relaxed);
        return ReadAssetBlob(data, error);
    };

    std::vector<std::thread> workers;
    workers.reserve(8);
    for (int i = 0; i < 8; ++i) {
        workers.emplace_back([&] {
            std::string error;
            auto snapshot = SharedSettingsCache::GetOrCreate(loader, &error);
            if (!snapshot || !error.empty()) {
                workerFailures.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            if (snapshot->defaults.data.empty()) {
                workerFailures.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            std::lock_guard<std::mutex> lock(snapshotMutex);
            snapshots.push_back(std::move(snapshot));
        });
    }
    for (auto &thread : workers) {
        thread.join();
    }

    Expect(workerFailures.load(std::memory_order_relaxed) == 0,
           "concurrent shared cache init should not fail",
           failures);
    Expect(!snapshots.empty(), "snapshots should not be empty", failures);
    Expect(loadCalls.load(std::memory_order_relaxed) == 1,
           "shared cache loader should run exactly once",
           failures);
    if (!snapshots.empty()) {
        const auto *first = snapshots.front().get();
        bool allSame = true;
        for (const auto &item : snapshots) {
            if (item.get() != first) {
                allSame = false;
                break;
            }
        }
        Expect(allSame, "all snapshot pointers should be identical", failures);
    }

    SharedSettingsCache::ResetForTests();

    if (failures == 0) {
        std::cout << "[PASS] test_settings_cache" << std::endl;
        return 0;
    }
    return 1;
}

