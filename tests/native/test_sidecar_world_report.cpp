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

Json::Value RunPreviewCoordAndParse(const std::string &coord,
                                    std::vector<std::string> *failures)
{
    Batch::SidecarPreviewCoordRequest request;
    request.jobId = "job-preview-coord-regression";
    request.coord = coord;

    std::ostringstream capturedStdout;
    auto *originalStdout = std::cout.rdbuf(capturedStdout.rdbuf());
    try {
        RunPreviewCoordCommand(request);
    } catch (...) {
        std::cout.rdbuf(originalStdout);
        throw;
    }
    std::cout.rdbuf(originalStdout);

    const std::string firstLine = ReadFirstNonEmptyLine(capturedStdout.str());
    Expect(!firstLine.empty(), coord + " preview_coord should emit one JSON line", failures);
    return ParseJsonObject(firstLine,
                           failures,
                           (coord + " preview_coord output should be valid json").c_str());
}

std::string FingerprintGeysers(const Json::Value &geysers)
{
    std::ostringstream stream;
    for (Json::ArrayIndex i = 0; i < geysers.size(); ++i) {
        const Json::Value &geyser = geysers[i];
        if (i > 0) {
            stream << '\n';
        }
        stream << geyser["id"].asString() << ','
               << geyser["type"].asInt() << ','
               << geyser["worldX"].asInt() << ','
               << geyser["worldY"].asInt() << ','
               << geyser["x"].asInt() << ','
               << geyser["y"].asInt();
    }
    return stream.str();
}

bool JsonArrayContainsGeyserId(const Json::Value &items, const char *id)
{
    if (id == nullptr) {
        return false;
    }
    for (Json::ArrayIndex i = 0; i < items.size(); ++i) {
        if (items[i]["id"].asString() == id) {
            return true;
        }
        if (items[i]["summary"]["id"].asString() == id) {
            return true;
        }
    }
    return false;
}

} // namespace

int RunAllTests()
{
    std::vector<std::string> failures;
    constexpr uint64_t kLongMixing = 152841815626ULL;

    {
        Batch::SidecarWorldReportRequest request;
        request.jobId = "job-world-report-001";
        request.worldType = 0;
        request.seed = 100123;
        request.mixing = kLongMixing;

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
        Expect(root["report"]["mixing"].asUInt64() == request.mixing,
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
               "M-FLIP-C primary world_report should succeed with generated summary offset",
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

    {
        Batch::SidecarWorldReportRequest request;
        request.jobId = "job-world-report-vaqu-warp-001";
        request.worldType = 39;
        request.seed = 100123;
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
        Expect(!firstLine.empty(), "V-AQU-C warp world_report should emit one JSON line", &failures);
        const Json::Value root =
            ParseJsonObject(firstLine, &failures, "V-AQU-C warp world_report json should be valid");
        const Json::Value &summaryGeysers = root["report"]["preview"]["summary"]["geysers"];
        const Json::Value &geyserDetails = root["report"]["geyserDetails"];

        Expect(JsonArrayContainsGeyserId(summaryGeysers, "warp_sender"),
               "V-AQU-C preview summary should include warp_sender",
               &failures);
        Expect(JsonArrayContainsGeyserId(summaryGeysers, "warp_receiver"),
               "V-AQU-C preview summary should include warp_receiver",
               &failures);
        Expect(JsonArrayContainsGeyserId(summaryGeysers, "warp_portal"),
               "V-AQU-C preview summary should include warp_portal",
               &failures);
        Expect(JsonArrayContainsGeyserId(geyserDetails, "warp_sender"),
               "V-AQU-C geyser details should include warp_sender",
               &failures);
        Expect(JsonArrayContainsGeyserId(geyserDetails, "warp_receiver"),
               "V-AQU-C geyser details should include warp_receiver",
               &failures);
        Expect(JsonArrayContainsGeyserId(geyserDetails, "warp_portal"),
               "V-AQU-C geyser details should include warp_portal",
               &failures);
    }

    {
        const std::string d3Coord = "AQU-C-2124866103-0-D3-SAH2Q7Y1";
        const std::string d9Coord = "AQU-C-2124866103-0-D9-SAH2Q7Y1";
        const std::string expectedGeysers =
            "small_reef_geyser,33,95,190,95,84\n"
            "small_reef_geyser,33,28,166,28,108\n"
            "small_reef_geyser,33,108,109,108,165\n"
            "underwater_vent,34,70,29,70,245\n"
            "underwater_vent,34,80,18,80,256\n"
            "underwater_vent,34,119,36,119,238\n"
            "murky_brine,32,114,47,114,227\n"
            "salt_water,6,22,216,22,58\n"
            "small_reef_geyser,33,15,153,15,121\n"
            "small_reef_geyser,33,78,193,78,81\n"
            "small_reef_geyser,33,82,174,82,100\n"
            "small_reef_geyser,33,18,101,18,173\n"
            "small_reef_geyser,33,125,104,125,170\n"
            "small_reef_geyser,33,24,155,24,119\n"
            "small_reef_geyser,33,91,174,91,100\n"
            "small_reef_geyser,33,51,150,51,124\n"
            "small_reef_geyser,33,49,110,49,164\n"
            "small_reef_geyser,33,114,191,114,83\n"
            "warp_sender,28,107,84,107,190\n"
            "warp_portal,30,79,69,79,205\n"
            "warp_receiver,29,101,215,101,59\n"
            "hot_hydrogen,11,117,128,117,146\n"
            "methane,15,143,33,143,241\n"
            "liquid_co2,9,140,117,140,157";

        const Json::Value d3Root = RunPreviewCoordAndParse(d3Coord, &failures);
        const Json::Value d9Root = RunPreviewCoordAndParse(d9Coord, &failures);
        const Json::Value &d3Summary = d3Root["preview"]["summary"];
        const Json::Value &d9Summary = d9Root["preview"]["summary"];

        Expect(d3Root["event"].asString() == "preview",
               "AQU-C D3 preview_coord should return preview event",
               &failures);
        Expect(d9Root["event"].asString() == "preview",
               "AQU-C D9 preview_coord should return preview event",
               &failures);
        Expect(d3Root["mixing"].asUInt64() == 152841812500ULL,
               "AQU-C D3 preview_coord should decode requested mixing",
               &failures);
        Expect(d9Root["mixing"].asUInt64() == 152841812500ULL,
               "AQU-C D9 preview_coord should decode requested mixing",
               &failures);
        Expect(d3Summary["geyserSeed"].asInt() == 2124866111,
               "AQU-C D3 geyser seed should stay stable",
               &failures);
        Expect(d9Summary["geyserSeed"].asInt() == 2124866111,
               "AQU-C D9 geyser seed should stay stable",
               &failures);
        const std::string d3Geysers = FingerprintGeysers(d3Summary["geysers"]);
        const std::string d9Geysers = FingerprintGeysers(d9Summary["geysers"]);
        Expect(d3Geysers == expectedGeysers,
               "AQU-C D3 geyser baseline changed; manually confirm before accepting\nactual:\n" +
                   d3Geysers,
               &failures);
        Expect(d9Geysers == expectedGeysers,
               "AQU-C D9 geyser baseline changed; manually confirm before accepting\nactual:\n" +
                   d9Geysers,
               &failures);
        Expect(d3Geysers == d9Geysers,
               "AQU-C D3 and D9 geyser placement should remain equivalent",
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
