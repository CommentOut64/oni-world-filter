#include "Batch/SidecarProtocol.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <json/json.h>

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

std::filesystem::path FixturePath(const char *name)
{
    const auto testDir = std::filesystem::path(__FILE__).parent_path();
    return testDir.parent_path() / "fixtures" / "protocol" / name;
}

std::string ReadText(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return {};
    }
    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

Json::Value ParseJsonObject(const std::string &jsonText, int &failures, const char *message)
{
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    std::istringstream stream(jsonText);
    const bool ok = Json::parseFromStream(builder, stream, &root, &errors);
    if (!ok) {
        std::cerr << "[FAIL] " << message << ": " << errors << std::endl;
        ++failures;
    }
    return root;
}

} // namespace

int RunAllTests()
{
    int failures = 0;

    {
        const auto path = FixturePath("search-request.json");
        const auto jsonText = ReadText(path);
        Expect(!jsonText.empty(), "search fixture should be readable", failures);

        const auto result = Batch::ParseSidecarRequest(jsonText);
        Expect(result.Ok(), "search request should parse", failures);
        Expect(result.request.command == Batch::SidecarCommandType::Search,
               "search request command mismatch",
               failures);
        Expect(result.request.search.jobId == "job-search-001", "search request jobId mismatch", failures);
        Expect(result.request.search.worldType == 13, "search request worldType mismatch", failures);
        Expect(result.request.search.seedStart == 100000, "search request seedStart mismatch", failures);
        Expect(result.request.search.seedEnd == 101000, "search request seedEnd mismatch", failures);
        Expect(result.request.search.mixing == 625, "search request mixing mismatch", failures);
        Expect(result.request.search.threads == 8, "search request threads mismatch", failures);
        Expect(result.request.search.cpu.hasValue, "search request cpu should exist", failures);
        Expect(result.request.search.cpu.mode == "custom", "search request cpu mode mismatch", failures);
        Expect(result.request.search.cpu.workers == 8, "search request cpu workers mismatch", failures);
        Expect(!result.request.search.cpu.allowSmt, "search request cpu allowSmt mismatch", failures);
        Expect(result.request.search.cpu.chunkSize == 32, "search request cpu chunkSize mismatch", failures);
        Expect(result.request.search.cpu.progressInterval == 400,
               "search request cpu progressInterval mismatch",
               failures);
        Expect(result.request.search.constraints.required.size() == 2,
               "search request required size mismatch",
               failures);
        Expect(result.request.search.constraints.forbidden.size() == 1,
               "search request forbidden size mismatch",
               failures);
        Expect(result.request.search.constraints.distance.size() == 2,
               "search request distance size mismatch",
               failures);
        Expect(result.request.search.constraints.distance[0].geyserId == "hot_water",
               "search request distance geyser mismatch",
               failures);
        Expect(result.request.search.constraints.count.empty(),
               "search request count should default to empty",
               failures);
    }

    {
        const auto path = FixturePath("preview-request.json");
        const auto jsonText = ReadText(path);
        Expect(!jsonText.empty(), "preview fixture should be readable", failures);

        const auto result = Batch::ParseSidecarRequest(jsonText);
        Expect(result.Ok(), "preview request should parse", failures);
        Expect(result.request.command == Batch::SidecarCommandType::Preview,
               "preview request command mismatch",
               failures);
        Expect(result.request.preview.jobId == "job-preview-001",
               "preview request jobId mismatch",
               failures);
        Expect(result.request.preview.worldType == 13, "preview request worldType mismatch", failures);
        Expect(result.request.preview.seed == 100123, "preview request seed mismatch", failures);
        Expect(result.request.preview.mixing == 625, "preview request mixing mismatch", failures);
    }

    {
        const auto path = FixturePath("analyze-search-request.json");
        const auto jsonText = ReadText(path);
        Expect(!jsonText.empty(), "analyze_search_request fixture should be readable", failures);

        const auto result = Batch::ParseSidecarRequest(jsonText);
        Expect(result.Ok(), "analyze_search_request should parse", failures);
        Expect(result.request.command == Batch::SidecarCommandType::AnalyzeSearchRequest,
               "analyze_search_request command mismatch",
               failures);
        Expect(result.request.analyze.jobId == "job-analyze-001",
               "analyze_search_request jobId mismatch",
               failures);
        Expect(result.request.analyze.constraints.count.size() == 1,
               "analyze_search_request count size mismatch",
               failures);
        Expect(result.request.analyze.constraints.count[0].geyserId == "hot_water",
               "analyze_search_request count geyser mismatch",
               failures);
        Expect(result.request.analyze.constraints.count[0].minCount == 1,
               "analyze_search_request minCount mismatch",
               failures);
        Expect(result.request.analyze.constraints.count[0].maxCount == 2,
               "analyze_search_request maxCount mismatch",
               failures);
    }

    {
        const auto path = FixturePath("get-search-catalog-request.json");
        const auto jsonText = ReadText(path);
        Expect(!jsonText.empty(), "get_search_catalog fixture should be readable", failures);

        const auto result = Batch::ParseSidecarRequest(jsonText);
        Expect(result.Ok(), "get_search_catalog request should parse", failures);
        Expect(result.request.command == Batch::SidecarCommandType::GetSearchCatalog,
               "get_search_catalog command mismatch",
               failures);
        Expect(result.request.getSearchCatalog.jobId == "job-search-catalog-001",
               "get_search_catalog jobId mismatch",
               failures);
    }

    {
        const auto result = Batch::ParseSidecarRequest(R"({"command":"unknown","jobId":"x"})");
        Expect(!result.Ok(), "unknown command should fail", failures);
        Expect(!result.error.empty(), "unknown command should report error", failures);
    }

    {
        Batch::SearchStartedEvent started;
        started.seedStart = 1;
        started.seedEnd = 100;
        started.totalSeeds = 100;
        started.workerCount = 8;

        Batch::SearchProgressEvent progress;
        progress.processedSeeds = 50;
        progress.totalSeeds = 100;
        progress.totalMatches = 3;
        progress.activeWorkers = 7;
        progress.windowSeedsPerSecond = 1200.5;
        progress.hasWindowSample = true;
        progress.activeWorkersReduced = true;
        progress.peakSeedsPerSecond = 1300.0;

        Batch::SearchMatchEvent match;
        match.seed = 42;
        match.processedSeeds = 50;
        match.totalSeeds = 100;
        match.totalMatches = 3;
        match.capture.startX = 128;
        match.capture.startY = 200;
        match.capture.worldW = 256;
        match.capture.worldH = 384;
        match.capture.traits = {1, 5};
        const int steam = Batch::GeyserIdToIndex("steam");
        Expect(steam >= 0, "steam geyser should exist", failures);
        match.capture.geysers.push_back({steam, 60, 80});

        Batch::SearchCompletedEvent completed;
        completed.processedSeeds = 100;
        completed.totalSeeds = 100;
        completed.totalMatches = 7;
        completed.finalActiveWorkers = 6;
        completed.autoFallbackCount = 1;
        completed.stoppedByBudget = false;
        completed.throughput.averageSeedsPerSecond = 1500.0;
        completed.throughput.stddevSeedsPerSecond = 35.5;
        completed.throughput.processedSeeds = 100;
        completed.throughput.valid = true;

        Batch::SearchFailedEvent failed;
        failed.message = "mock failure";
        failed.processedSeeds = 20;
        failed.totalSeeds = 100;

        Batch::SearchCancelledEvent cancelled;
        cancelled.processedSeeds = 30;
        cancelled.totalSeeds = 100;
        cancelled.totalMatches = 1;
        cancelled.finalActiveWorkers = 2;
        cancelled.throughput.averageSeedsPerSecond = 800.0;
        cancelled.throughput.valid = true;

        Batch::SidecarPreviewRequest previewRequest;
        previewRequest.jobId = "job-preview-001";
        previewRequest.worldType = 13;
        previewRequest.seed = 100123;
        previewRequest.mixing = 625;

        GeneratedWorldPreview preview;
        preview.summary.seed = 100123;
        preview.summary.worldType = 13;
        preview.summary.start = {128, 200};
        preview.summary.worldSize = {256, 384};
        preview.summary.traits.push_back({2});
        preview.summary.geysers.push_back({steam, 70, 90});
        PolygonSummary polygon;
        polygon.hasHole = false;
        polygon.zoneType = 3;
        polygon.vertices.push_back({1, 2});
        polygon.vertices.push_back({3, 4});
        preview.polygons.push_back(std::move(polygon));

        SearchAnalysis::SearchCatalog catalog;
        catalog.worlds.push_back({.id = 13, .code = "V-SNDST-C-"});
        catalog.geysers.push_back({.id = 2, .key = "hot_water"});
        catalog.traits.push_back(SearchAnalysis::TraitMeta{
            .id = "traits/SunnySpeed",
            .name = "Sunny Speed",
            .description = "speed boost",
            .traitTags = {"temperature"},
            .exclusiveWith = {"traits/Other"},
            .exclusiveWithTags = {"unique"},
            .forbiddenDLCIds = {"EXPANSION1_ID"},
            .effectSummary = {"globalFeatureMods=1"},
            .searchable = false,
        });
        catalog.mixingSlots.push_back(SearchAnalysis::MixingSlotMeta{
            .slot = 0,
            .path = "DLC2_ID",
            .type = "dlc",
            .name = "The Frosty Planet Pack",
            .description = "",
        });
        catalog.parameterSpecs.push_back(SearchAnalysis::ParameterSpec{
            .id = "mixing",
            .valueType = "base5-encoded-int",
            .meaning = "mixing code",
            .staticRange = "0..48828124",
            .supportsDynamicRange = true,
            .source = "SettingsCache::ParseAndApplyMixingSettingsCode",
        });

        SearchAnalysis::SearchAnalysisResult analysis;
        analysis.normalizedRequest.worldType = 13;
        analysis.normalizedRequest.mixing = 625;
        analysis.normalizedRequest.seedStart = 100000;
        analysis.normalizedRequest.seedEnd = 101000;
        analysis.normalizedRequest.groups.push_back(SearchAnalysis::ConstraintGroup{
            .geyserId = "hot_water",
            .geyserIndex = 2,
            .minCount = 1,
            .maxCount = 2,
            .hasRequired = true,
            .hasForbidden = false,
            .hasExplicitCount = true,
            .distanceRules = {SearchAnalysis::DistanceConstraint{
                .geyserId = "hot_water",
                .minDist = 0.0,
                .maxDist = 80.0,
            }},
        });
        analysis.errors.push_back(SearchAnalysis::ValidationIssue{
            .layer = "layer3",
            .code = "conflict.required_forbidden",
            .field = "constraints.required/constraints.forbidden",
            .message = "同一 geyser 不能同时 required 和 forbidden",
        });

        const auto startedJson = ParseJsonObject(
            Batch::SerializeStartedEvent("job-1", started),
            failures,
            "started event json parse failed");
        const auto progressJson = ParseJsonObject(
            Batch::SerializeProgressEvent("job-1", progress),
            failures,
            "progress event json parse failed");
        const auto matchJson = ParseJsonObject(
            Batch::SerializeMatchEvent("job-1", match),
            failures,
            "match event json parse failed");
        const auto completedJson = ParseJsonObject(
            Batch::SerializeCompletedEvent("job-1", completed),
            failures,
            "completed event json parse failed");
        const auto failedJson = ParseJsonObject(
            Batch::SerializeFailedEvent("job-1", failed),
            failures,
            "failed event json parse failed");
        const auto cancelledJson = ParseJsonObject(
            Batch::SerializeCancelledEvent("job-1", cancelled),
            failures,
            "cancelled event json parse failed");
        const auto previewJson = ParseJsonObject(
            Batch::SerializePreviewEvent("job-preview-001", previewRequest, preview),
            failures,
            "preview event json parse failed");
        const auto searchCatalogJson = ParseJsonObject(
            Batch::SerializeSearchCatalogEvent("job-search-catalog-001", catalog),
            failures,
            "search catalog event json parse failed");
        const auto searchAnalysisJson = ParseJsonObject(
            Batch::SerializeSearchAnalysisEvent("job-analyze-001", analysis),
            failures,
            "search analysis event json parse failed");

        Expect(startedJson["event"].asString() == "started", "started event type mismatch", failures);
        Expect(progressJson["event"].asString() == "progress", "progress event type mismatch", failures);
        Expect(matchJson["event"].asString() == "match", "match event type mismatch", failures);
        Expect(completedJson["event"].asString() == "completed", "completed event type mismatch", failures);
        Expect(failedJson["event"].asString() == "failed", "failed event type mismatch", failures);
        Expect(cancelledJson["event"].asString() == "cancelled", "cancelled event type mismatch", failures);
        Expect(previewJson["event"].asString() == "preview", "preview event type mismatch", failures);

        Expect(matchJson["summary"]["geysers"].size() == 1, "match event geyser size mismatch", failures);
        Expect(matchJson["summary"]["geysers"][0]["id"].asString() == "steam",
               "match event geyser id mismatch",
               failures);
        Expect(completedJson["throughput"]["valid"].asBool(), "completed throughput valid mismatch", failures);
        Expect(previewJson["preview"]["polygons"].size() == 1, "preview polygon size mismatch", failures);
        Expect(previewJson["preview"]["summary"]["seed"].asInt() == 100123,
               "preview summary seed mismatch",
               failures);
        Expect(searchCatalogJson["event"].asString() == "search_catalog",
               "search catalog event type mismatch",
               failures);
        Expect(searchCatalogJson["catalog"]["worlds"].size() == 1,
               "search catalog worlds size mismatch",
               failures);
        Expect(searchCatalogJson["catalog"]["mixingSlots"].size() == 1,
               "search catalog mixingSlots size mismatch",
               failures);
        Expect(searchCatalogJson["catalog"]["parameterSpecs"][0]["supportsDynamicRange"].asBool(),
               "search catalog parameter spec dynamic flag mismatch",
               failures);
        Expect(searchAnalysisJson["event"].asString() == "search_analysis",
               "search analysis event type mismatch",
               failures);
        Expect(searchAnalysisJson["analysis"]["normalizedRequest"]["groups"].size() == 1,
               "search analysis normalized groups size mismatch",
               failures);
        Expect(searchAnalysisJson["analysis"]["errors"].size() == 1,
               "search analysis errors size mismatch",
               failures);
    }

    if (failures == 0) {
        std::cout << "[PASS] test_sidecar_protocol" << std::endl;
        return 0;
    }
    return 1;
}
