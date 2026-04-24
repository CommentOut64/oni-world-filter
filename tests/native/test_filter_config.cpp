#include "Batch/FilterConfig.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

bool WriteText(const std::filesystem::path &path, const std::string &text)
{
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return false;
    }
    out << text;
    return out.good();
}

bool ContainsErrorCode(const Batch::FilterConfigLoadResult &result,
                       Batch::FilterErrorCode code)
{
    for (const auto &error : result.errors) {
        if (error.code == code) {
            return true;
        }
    }
    return false;
}

bool Expect(bool condition, const char *message, int &failures)
{
    if (condition) {
        return true;
    }
    std::cerr << "[FAIL] " << message << std::endl;
    ++failures;
    return false;
}

} // namespace

int RunAllTests()
{
    int failures = 0;
    const auto tempDir = std::filesystem::temp_directory_path() / "oni_task4_tests";
    std::filesystem::create_directories(tempDir);

    {
        const auto file = tempDir / "legacy-threads-ignored.json";
        const std::string json = R"({
  "worldType": 0,
  "seedStart": 1,
  "seedEnd": 2,
  "mixing": 0,
  "threads": 8
})";
        Expect(WriteText(file, json), "write legacy-threads-ignored json failed", failures);
        const auto result = Batch::LoadFilterConfig(file.string());
        Expect(result.Ok(), "legacy threads field should be ignored during parse", failures);
        Expect(!result.config.hasCpuSection,
               "legacy threads field should not synthesize cpu section",
               failures);
    }

    {
        const auto file = tempDir / "unknown-geyser.json";
        const std::string json = R"({
  "required": ["unknown_required"],
  "forbidden": ["unknown_forbidden"]
})";
        Expect(WriteText(file, json), "write unknown-geyser json failed", failures);
        const auto result = Batch::LoadFilterConfig(file.string());
        Expect(!result.Ok(), "unknown geyser ids should fail", failures);
        Expect(ContainsErrorCode(result, Batch::FilterErrorCode::UnknownRequiredGeyserId),
               "required unknown geyser should report error",
               failures);
        Expect(ContainsErrorCode(result, Batch::FilterErrorCode::UnknownForbiddenGeyserId),
               "forbidden unknown geyser should report error",
               failures);
    }

    {
        const auto file = tempDir / "distance-missing-field.json";
        const std::string json = R"({
  "distance": [
    { "geyser": "steam", "minDist": 10 }
  ]
})";
        Expect(WriteText(file, json), "write distance json failed", failures);
        const auto result = Batch::LoadFilterConfig(file.string());
        Expect(!result.Ok(), "distance missing field should fail", failures);
        Expect(ContainsErrorCode(result, Batch::FilterErrorCode::MissingDistanceField),
               "distance missing field should report error",
               failures);
    }

    {
        const auto file = tempDir / "count-valid.json";
        const std::string json = R"({
  "count": [
    { "geyser": "steam", "minCount": 1, "maxCount": 2 }
  ]
})";
        Expect(WriteText(file, json), "write count-valid json failed", failures);
        const auto result = Batch::LoadFilterConfig(file.string());
        Expect(result.Ok(), "count-valid should parse", failures);
        Expect(result.config.countRules.size() == 1, "count-valid should have one count rule", failures);
        if (!result.config.countRules.empty()) {
            Expect(result.config.countRules[0].minCount == 1, "count min mismatch", failures);
            Expect(result.config.countRules[0].maxCount == 2, "count max mismatch", failures);
        }
    }

    {
        const auto file = tempDir / "count-missing-field.json";
        const std::string json = R"({
  "count": [
    { "geyser": "steam", "minCount": 1 }
  ]
})";
        Expect(WriteText(file, json), "write count-missing-field json failed", failures);
        const auto result = Batch::LoadFilterConfig(file.string());
        Expect(!result.Ok(), "count missing field should fail", failures);
        Expect(ContainsErrorCode(result, Batch::FilterErrorCode::MissingCountField),
               "count missing field should report error",
               failures);
    }

    {
        const auto file = tempDir / "count-invalid-range.json";
        const std::string json = R"({
  "count": [
    { "geyser": "steam", "minCount": 3, "maxCount": 1 }
  ]
})";
        Expect(WriteText(file, json), "write count-invalid-range json failed", failures);
        const auto result = Batch::LoadFilterConfig(file.string());
        Expect(!result.Ok(), "count invalid range should fail", failures);
        Expect(ContainsErrorCode(result, Batch::FilterErrorCode::InvalidCountRange),
               "count invalid range should report error",
               failures);
    }

    {
        const auto file = tempDir / "seed-range-invalid.json";
        const std::string json = R"({
  "seedStart": 20,
  "seedEnd": 10
})";
        Expect(WriteText(file, json), "write seed range json failed", failures);
        const auto result = Batch::LoadFilterConfig(file.string());
        Expect(!result.Ok(), "seedStart > seedEnd should fail", failures);
        Expect(ContainsErrorCode(result, Batch::FilterErrorCode::InvalidSeedRange),
               "invalid seed range should report error",
               failures);
    }

    {
        const auto file = tempDir / "cpu-default-placement.json";
        const std::string json = R"({
  "cpu": {
    "mode": "balanced"
  }
})";
        Expect(WriteText(file, json), "write cpu-default-placement json failed", failures);
        const auto result = Batch::LoadFilterConfig(file.string());
        Expect(result.Ok(), "cpu-default-placement should parse", failures);
        Expect(result.config.hasCpuSection, "cpu-default-placement should keep cpu section", failures);
        Expect(result.config.cpu.placement == "strict",
               "cpu placement should default to strict when omitted",
               failures);
    }

    if (failures == 0) {
        std::cout << "[PASS] test_filter_config" << std::endl;
        return 0;
    }
    return 1;
}
