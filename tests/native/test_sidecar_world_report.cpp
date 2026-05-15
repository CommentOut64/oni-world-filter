#define main sidecar_entry_main_for_test
#include "../../src/entry_sidecar.cpp"
#undef main

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <json/json.h>

namespace {

void Expect(bool condition, const std::string &message, std::vector<std::string> *failures)
{
    if (condition || failures == nullptr) {
        return;
    }
    failures->push_back(message);
}

Json::Value ParseJsonObject(const std::string &jsonText,
                            std::vector<std::string> *failures,
                            const char *message)
{
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    std::istringstream stream(jsonText);
    const bool ok = Json::parseFromStream(builder, stream, &root, &errors);
    if (!ok) {
        if (failures != nullptr) {
            failures->push_back(std::string(message) + ": " + errors);
        }
    }
    return root;
}

std::string ReadFirstNonEmptyLine(const std::string &text)
{
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty()) {
            return line;
        }
    }
    return {};
}

} // namespace

int RunAllTests()
{
    std::vector<std::string> failures;

    {
        Batch::SidecarWorldReportRequest request;
        request.jobId = "job-world-report-001";
        request.worldType = 0;
        request.seed = 100123;
        request.mixing = 0;

        std::string expectedCoord;
        Expect(BuildWorldCode(request.worldType, request.seed, request.mixing, &expectedCoord),
               "BuildWorldCode should succeed for world report test request",
               &failures);

        std::ostringstream capturedStdout;
        auto *originalStdout = std::cout.rdbuf(capturedStdout.rdbuf());
        try {
            RunGetWorldReportCommand(request);
        } catch (...) {
            std::cout.rdbuf(originalStdout);
            throw;
        }
        std::cout.rdbuf(originalStdout);

        const std::string firstLine = ReadFirstNonEmptyLine(capturedStdout.str());
        Expect(!firstLine.empty(), "world_report command should emit one JSON line", &failures);
        const Json::Value root =
            ParseJsonObject(firstLine, &failures, "world_report command output should be valid json");

        Expect(root["event"].asString() == "world_report",
               "world_report command event type mismatch",
               &failures);
        Expect(root["jobId"].asString() == request.jobId,
               "world_report command jobId mismatch",
               &failures);
        Expect(root["report"]["coord"].asString() == expectedCoord,
               "world_report command coord mismatch",
               &failures);
        Expect(root["report"]["mixing"].asInt() == request.mixing,
               "world_report command mixing mismatch",
               &failures);
        Expect(root["report"]["preview"]["summary"]["seed"].asInt() == request.seed,
               "world_report command preview seed mismatch",
               &failures);
        Expect(root["report"]["preview"]["summary"]["geysers"].size() > 0,
               "world_report command should contain at least one geyser summary",
               &failures);
        Expect(root["report"]["geyserDetails"].size() ==
                   root["report"]["preview"]["summary"]["geysers"].size(),
               "world_report command geyser detail count should match preview geyser count",
               &failures);
    }

    {
        Batch::SidecarWorldReportRequest request;
        request.jobId = "job-world-report-mflip-001";
        request.worldType = 35;
        request.seed = 644400493;
        request.mixing = 0;

        std::ostringstream capturedStdout;
        auto *originalStdout = std::cout.rdbuf(capturedStdout.rdbuf());
        try {
            RunGetWorldReportCommand(request);
        } catch (...) {
            std::cout.rdbuf(originalStdout);
            throw;
        }
        std::cout.rdbuf(originalStdout);

        const std::string firstLine = ReadFirstNonEmptyLine(capturedStdout.str());
        Expect(!firstLine.empty(), "M-FLIP-C world_report should emit one JSON line", &failures);
        const Json::Value root =
            ParseJsonObject(firstLine, &failures, "M-FLIP-C world_report json should be valid");

        Expect(root["event"].asString() == "world_report",
               "M-FLIP-C primary world_report should recover to legacy-equivalent success",
               &failures);
        const Json::Value &geyserDetails = root["report"]["geyserDetails"];
        int hotSteamIndex = -1;
        for (Json::ArrayIndex i = 0; i < geyserDetails.size(); ++i) {
            if (geyserDetails[i]["summary"]["id"].asString() == "hot_steam") {
                hotSteamIndex = static_cast<int>(i);
                break;
            }
        }
        Expect(hotSteamIndex >= 0,
               "M-FLIP-C sample should contain hot_steam detail",
               &failures);
        if (hotSteamIndex >= 0) {
            const Json::Value &detail = geyserDetails[static_cast<Json::ArrayIndex>(hotSteamIndex)];
            Expect(std::fabs(detail["native"]["eruptionPeriodSeconds"].asFloat() - 576.0f) <= 1.0f,
                   "M-FLIP-C sample eruption period should match game",
                   &failures);
            Expect(std::fabs(detail["derived"]["eruptionSeconds"].asFloat() - 260.0f) <= 1.0f,
                   "M-FLIP-C sample eruption seconds should match game",
                   &failures);
            Expect(std::fabs(detail["derived"]["eruptionRateKgPerSecond"].asFloat() - 2.8386f) <= 0.02f,
                   "M-FLIP-C sample eruption rate should match game",
                   &failures);
            Expect(std::fabs(detail["derived"]["temperatureCelsius"].asFloat() - 500.0f) <= 0.05f,
                   "M-FLIP-C sample temperature should match game",
                   &failures);
        }
    }

    if (!failures.empty()) {
        for (const auto &failure : failures) {
            std::cerr << "[FAIL] " << failure << std::endl;
        }
        return 1;
    }
    return 0;
}
