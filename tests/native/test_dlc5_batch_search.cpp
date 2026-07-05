#include "App/AppRuntime.hpp"
#include "Batch/BatchMatcher.hpp"
#include "Batch/FilterConfig.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "config.h"

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

class RecordingBatchSink final : public ResultSink
{
public:
    bool RequestResource(uint32_t expectedSize, std::vector<char> &data) override
    {
        data.assign(expectedSize, 0);
        std::ifstream file(SETTING_TEST_ASSET_FILEPATH, std::ios::binary);
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
        if (!summary.isPrimary) {
            return;
        }

        capture.Reset();
        capture.active = true;
        capture.startX = summary.start.x;
        capture.startY = summary.start.y;
        capture.worldW = summary.worldSize.x;
        capture.worldH = summary.worldSize.y;
        capture.geysers.reserve(summary.geysers.size());
        for (const auto &geyser : summary.geysers) {
            capture.geysers.push_back(
                {geyser.type, geyser.x, geyser.y, geyser.worldX, geyser.worldY});
        }
    }

    void OnGeneratedWorldPreview(const GeneratedWorldPreview &preview) override
    {
        (void)preview;
    }

    BatchCaptureRecord capture;
};

std::string BuildWorldCode(const std::string &prefix, int seed, uint64_t mixing)
{
    std::ostringstream builder;
    builder << prefix << seed << "-0-D3-" << SettingsCache::BinaryToBase36(mixing);
    return builder.str();
}

bool GeneratePrimaryCapture(const std::string &code, BatchCaptureRecord *capture)
{
    if (capture == nullptr) {
        return false;
    }
    auto *runtime = AppRuntime::Instance();
    RecordingBatchSink sink;
    runtime->SetResultSink(&sink);
    runtime->SetSkipPolygons(true);
    runtime->Initialize(0);
    if (!runtime->Generate(code, 0)) {
        return false;
    }
    *capture = sink.capture;
    return capture->active;
}

} // namespace

int RunAllTests()
{
    int failures = 0;

    const int murkyBrine = Batch::GeyserIdToIndex("murky_brine");
    const int smallReefGeyser = Batch::GeyserIdToIndex("small_reef_geyser");
    const int underwaterVent = Batch::GeyserIdToIndex("underwater_vent");
    const int methane = Batch::GeyserIdToIndex("methane");
    const int saltWater = Batch::GeyserIdToIndex("salt_water");

    Expect(murkyBrine >= 0, "murky_brine geyser id should exist", failures);
    Expect(smallReefGeyser >= 0, "small_reef_geyser geyser id should exist", failures);
    Expect(underwaterVent >= 0, "underwater_vent geyser id should exist", failures);
    Expect(methane >= 0, "methane geyser id should exist", failures);
    Expect(saltWater >= 0, "salt_water geyser id should exist", failures);

    const std::string sampleCode = BuildWorldCode("AQU-C-", 2124866103, 152841812500ULL);
    BatchCaptureRecord sampleCapture;
    Expect(GeneratePrimaryCapture(sampleCode, &sampleCapture),
           "AQU-C dlc5 sample should generate primary capture",
           failures);
    Expect(sampleCapture.worldW > 0 && sampleCapture.worldH > 0,
           "AQU-C dlc5 sample should include world size",
           failures);
    Expect(sampleCapture.startX > 0 || sampleCapture.startY > 0,
           "AQU-C dlc5 sample should include start position",
           failures);

    {
        Batch::FilterConfig cfg;
        cfg.required = {murkyBrine, smallReefGeyser};
        cfg.forbidden = {methane};
        const auto result = Batch::MatchFilter(cfg, sampleCapture);
        Expect(result.Ok(), "dlc5 required+forbidden matcher run should not produce errors", failures);
        Expect(!result.matched,
               "dlc5 sample should reject forbidden methane when required aquatic vents also exist",
               failures);
    }

    {
        Batch::FilterConfig cfg;
        cfg.required = {murkyBrine, smallReefGeyser, methane};
        cfg.countRules.push_back({smallReefGeyser, 10, 20});
        cfg.countRules.push_back({murkyBrine, 1, 1});
        cfg.countRules.push_back({methane, 1, 1});
        const auto result = Batch::MatchFilter(cfg, sampleCapture);
        Expect(result.Ok(), "dlc5 count matcher run should not produce errors", failures);
        Expect(result.matched,
               "dlc5 sample should satisfy combined required/count rules for aquatic vents",
               failures);
    }

    {
        Batch::FilterConfig cfg;
        cfg.countRules.push_back({smallReefGeyser, 1, 9});
        const auto result = Batch::MatchFilter(cfg, sampleCapture);
        Expect(result.Ok(), "dlc5 count mismatch run should not produce errors", failures);
        Expect(!result.matched,
               "dlc5 sample should fail when small_reef_geyser upper bound is too small",
               failures);
    }

    {
        Batch::FilterConfig cfg;
        cfg.required = {smallReefGeyser};
        cfg.distanceRules.push_back({smallReefGeyser, 70.0f, 90.0f});
        cfg.distanceRules.push_back({murkyBrine, 110.0f, 150.0f});
        const auto result = Batch::MatchFilter(cfg, sampleCapture);
        Expect(result.Ok(), "dlc5 distance matcher run should not produce errors", failures);
        Expect(result.matched,
               "dlc5 sample should satisfy combined aquatic distance rules",
               failures);
    }

    {
        Batch::FilterConfig cfg;
        cfg.required = {smallReefGeyser};
        cfg.distanceRules.push_back({murkyBrine, 0.0f, 80.0f});
        const auto result = Batch::MatchFilter(cfg, sampleCapture);
        Expect(result.Ok(), "dlc5 distance mismatch run should not produce errors", failures);
        Expect(!result.matched,
               "dlc5 sample should fail when murky_brine distance window is too tight",
               failures);
    }

    {
        Batch::FilterConfig cfg;
        cfg.required = {saltWater, murkyBrine};
        cfg.countRules.push_back({smallReefGeyser, 10, 20});
        cfg.distanceRules.push_back({saltWater, 75.0f, 80.0f});
        const auto result = Batch::MatchFilter(cfg, sampleCapture);
        Expect(result.Ok(), "dlc5 mixed aquatic+generic matcher run should not produce errors", failures);
        Expect(result.matched,
               "dlc5 sample should support mixed aquatic and generic geyser constraints together",
               failures);
    }

    {
        Batch::FilterConfig cfg;
        cfg.required = {murkyBrine, underwaterVent};
        cfg.countRules.push_back({underwaterVent, 3, 3});
        cfg.distanceRules.push_back({underwaterVent, 89.0f, 92.0f});
        const auto result = Batch::MatchFilter(cfg, sampleCapture);
        Expect(result.Ok(), "dlc5 underwater_vent matcher run should not produce errors", failures);
        Expect(result.matched,
               "dlc5 sample should support underwater_vent required+count+distance rules",
               failures);
    }

    if (failures == 0) {
        std::cout << "[PASS] test_dlc5_batch_search" << std::endl;
        return 0;
    }
    return 1;
}
