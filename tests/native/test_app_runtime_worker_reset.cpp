#include "App/AppRuntime.hpp"
#include "SearchAnalysis/SearchCatalog.hpp"
#include "config.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
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

std::string BuildWorldCode(int worldType, int seed, int mixing)
{
    const auto &worldPrefixes = SearchAnalysis::GetWorldPrefixes();
    if (worldType < 0 || static_cast<int>(worldPrefixes.size()) <= worldType) {
        return {};
    }
    std::ostringstream builder;
    builder << worldPrefixes[static_cast<size_t>(worldType)] << seed << "-0-D3-"
            << SettingsCache::BinaryToBase36(static_cast<uint32_t>(mixing));
    return builder.str();
}

class RecordingSink final : public ResultSink
{
public:
    bool RequestResource(uint32_t expectedSize, std::vector<char> &data) override
    {
        data.assign(expectedSize, 0);
        std::ifstream file(SETTING_ASSET_FILEPATH, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        const auto size = file.seekg(0, std::ios::end).tellg();
        if (size != static_cast<std::streamoff>(expectedSize)) {
            return false;
        }
        file.seekg(0).read(data.data(), expectedSize);
        return file.good();
    }

    void OnGeneratedWorldSummary(const GeneratedWorldSummary &summary) override
    {
        summaries.push_back(summary);
    }

    void OnGeneratedWorldPreview(const GeneratedWorldPreview &preview) override
    {
        previews.push_back(preview);
    }

    void Reset()
    {
        summaries.clear();
        previews.clear();
    }

    std::vector<GeneratedWorldSummary> summaries;
    std::vector<GeneratedWorldPreview> previews;
};

std::string FingerprintSummary(const GeneratedWorldSummary &summary)
{
    std::ostringstream builder;
    builder << summary.seed << '|' << summary.worldType << '|';
    builder << summary.start.x << ',' << summary.start.y << '|';
    builder << summary.worldSize.x << ',' << summary.worldSize.y << '|';
    for (const auto &trait : summary.traits) {
        builder << trait.id << ',';
    }
    builder << '|';
    for (const auto &geyser : summary.geysers) {
        builder << geyser.type << ':' << geyser.x << ':' << geyser.y << ',';
    }
    return builder.str();
}

std::string FingerprintSummaries(const std::vector<GeneratedWorldSummary> &summaries)
{
    std::ostringstream builder;
    for (const auto &summary : summaries) {
        builder << FingerprintSummary(summary) << ';';
    }
    return builder.str();
}

std::string RunLegacySummaryOnly(AppRuntime *runtime,
                                 RecordingSink &sink,
                                 const std::string &code)
{
    runtime->SetSkipPolygons(true);
    runtime->Initialize(0);
    sink.Reset();
    if (!runtime->Generate(code, 0)) {
        return {};
    }
    return FingerprintSummaries(sink.summaries);
}

std::string RunPreparedSummaryOnly(AppRuntime *runtime,
                                   RecordingSink &sink,
                                   const std::string &code)
{
    sink.Reset();
    if (!runtime->ResetSearchSeed(code)) {
        return {};
    }
    if (!runtime->GeneratePrepared(0)) {
        return {};
    }
    return FingerprintSummaries(sink.summaries);
}

} // namespace

int RunAllTests()
{
    int failures = 0;

    AppRuntime *runtime = AppRuntime::Instance();
    RecordingSink sink;
    runtime->SetResultSink(&sink);

    const int worldType = 13;
    const int mixing = 625;
    const std::vector<int> seedFixture{100000, 100030, 100123, 100500, 101337};

    runtime->SetSkipPolygons(true);
    runtime->PrepareSearchWorker(BuildWorldCode(worldType, seedFixture.front(), mixing));
    for (int seed : seedFixture) {
        const std::string code = BuildWorldCode(worldType, seed, mixing);
        const std::string legacy = RunLegacySummaryOnly(runtime, sink, code);
        const std::string optimized = RunPreparedSummaryOnly(runtime, sink, code);

        Expect(!legacy.empty(), "legacy summary fingerprint should not be empty", failures);
        Expect(!optimized.empty(), "optimized summary fingerprint should not be empty", failures);
        Expect(legacy == optimized,
               "optimized worker-reset summary output should match legacy path",
               failures);
    }

    {
        const std::string code = BuildWorldCode(worldType, seedFixture.front(), mixing);
        runtime->SetSkipPolygons(false);
        runtime->Initialize(0);
        sink.Reset();
        Expect(runtime->Generate(code, 0), "legacy preview path should generate successfully", failures);
        Expect(!sink.previews.empty(),
               "legacy Generate path should continue emitting previews",
               failures);
    }

    if (failures == 0) {
        std::cout << "[PASS] test_app_runtime_worker_reset" << std::endl;
        return 0;
    }
    return 1;
}
