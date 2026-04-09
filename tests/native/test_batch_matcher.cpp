#include "Batch/BatchMatcher.hpp"
#include "Batch/FilterConfig.hpp"

#include <iostream>

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

BatchCaptureRecord MakeBaseCapture()
{
    BatchCaptureRecord capture;
    capture.active = true;
    capture.startX = 50;
    capture.startY = 50;
    capture.worldW = 256;
    capture.worldH = 256;
    return capture;
}

} // namespace

int RunAllTests()
{
    int failures = 0;
    const int steam = Batch::GeyserIdToIndex("steam");
    const int hotWater = Batch::GeyserIdToIndex("hot_water");
    Expect(steam >= 0, "steam geyser id should exist", failures);
    Expect(hotWater >= 0, "hot_water geyser id should exist", failures);

    {
        Batch::FilterConfig cfg;
        cfg.forbidden = {steam};
        auto capture = MakeBaseCapture();
        capture.geysers.push_back({steam, 60, 60});
        const auto result = Batch::MatchFilter(cfg, capture);
        Expect(result.Ok(), "forbidden test should not produce errors", failures);
        Expect(!result.matched, "forbidden geyser should short-circuit to not matched", failures);
    }

    {
        Batch::FilterConfig cfg;
        cfg.required = {steam, hotWater};
        auto capture = MakeBaseCapture();
        capture.geysers.push_back({steam, 60, 60});
        capture.geysers.push_back({hotWater, 80, 80});
        const auto result = Batch::MatchFilter(cfg, capture);
        Expect(result.Ok(), "required test should not produce errors", failures);
        Expect(result.matched, "all required geysers should match", failures);
    }

    {
        Batch::FilterConfig cfg;
        cfg.distanceRules.push_back({steam, 5.0f, 30.0f});
        cfg.distanceRules.push_back({hotWater, 20.0f, 60.0f});
        auto capture = MakeBaseCapture();
        capture.geysers.push_back({steam, 55, 55});    // sqrt(50)=7.07
        capture.geysers.push_back({hotWater, 90, 50}); // 40
        const auto result = Batch::MatchFilter(cfg, capture);
        Expect(result.Ok(), "distance test should not produce errors", failures);
        Expect(result.matched, "multi distance rules should match", failures);
    }

    {
        Batch::FilterConfig cfg;
        BatchCaptureRecord missingStart;
        missingStart.worldW = 256;
        missingStart.worldH = 256;
        auto result = Batch::MatchFilter(cfg, missingStart);
        Expect(!result.Ok(), "missing start should produce errors", failures);

        auto missingSize = MakeBaseCapture();
        missingSize.worldW = 0;
        missingSize.worldH = 0;
        result = Batch::MatchFilter(cfg, missingSize);
        Expect(!result.Ok(), "missing world size should produce errors", failures);
    }

    if (failures == 0) {
        std::cout << "[PASS] test_batch_matcher" << std::endl;
        return 0;
    }
    return 1;
}
