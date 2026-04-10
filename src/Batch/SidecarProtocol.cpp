#include "Batch/SidecarProtocol.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

#include <json/json.h>

namespace Batch {

namespace {

void SetParseError(SidecarParseResult &result, std::string message)
{
    if (result.error.empty()) {
        result.error = std::move(message);
    }
}

bool RequireString(const Json::Value &obj,
                   const char *field,
                   std::string *out,
                   std::string *error)
{
    if (!obj.isMember(field)) {
        *error = std::string("missing field: ") + field;
        return false;
    }
    if (!obj[field].isString()) {
        *error = std::string("field must be string: ") + field;
        return false;
    }
    *out = obj[field].asString();
    return true;
}

bool RequireInt(const Json::Value &obj,
                const char *field,
                int *out,
                std::string *error)
{
    if (!obj.isMember(field)) {
        *error = std::string("missing field: ") + field;
        return false;
    }
    if (!obj[field].isInt()) {
        *error = std::string("field must be int: ") + field;
        return false;
    }
    *out = obj[field].asInt();
    return true;
}

bool RequireNumber(const Json::Value &obj,
                   const char *field,
                   float *out,
                   std::string *error)
{
    if (!obj.isMember(field)) {
        *error = std::string("missing field: ") + field;
        return false;
    }
    if (!obj[field].isNumeric()) {
        *error = std::string("field must be numeric: ") + field;
        return false;
    }
    *out = obj[field].asFloat();
    return true;
}

bool ParseStringArray(const Json::Value &array,
                      const char *field,
                      std::vector<std::string> *out,
                      std::string *error)
{
    if (array.isNull()) {
        return true;
    }
    if (!array.isArray()) {
        *error = std::string("field must be string array: ") + field;
        return false;
    }
    for (const auto &item : array) {
        if (!item.isString()) {
            *error = std::string("field must be string array: ") + field;
            return false;
        }
        out->push_back(item.asString());
    }
    return true;
}

Json::Value ToThroughputJson(const BatchCpu::ThroughputStats &throughput)
{
    Json::Value value(Json::objectValue);
    value["averageSeedsPerSecond"] = throughput.averageSeedsPerSecond;
    value["stddevSeedsPerSecond"] = throughput.stddevSeedsPerSecond;
    value["processedSeeds"] = static_cast<Json::UInt64>(throughput.processedSeeds);
    value["valid"] = throughput.valid;
    return value;
}

Json::Value BuildBaseEventJson(const char *eventType, const std::string &jobId)
{
    Json::Value root(Json::objectValue);
    root["event"] = eventType;
    root["jobId"] = jobId;
    return root;
}

std::string WriteCompactJson(const Json::Value &root)
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, root);
}

Json::Value BuildCaptureSummaryJson(const BatchCaptureRecord &capture)
{
    Json::Value summary(Json::objectValue);
    summary["start"]["x"] = capture.startX;
    summary["start"]["y"] = capture.startY;
    summary["worldSize"]["w"] = capture.worldW;
    summary["worldSize"]["h"] = capture.worldH;

    Json::Value traits(Json::arrayValue);
    for (int trait : capture.traits) {
        traits.append(trait);
    }
    summary["traits"] = traits;

    const auto &ids = GetGeyserIds();
    Json::Value geysers(Json::arrayValue);
    for (const auto &item : capture.geysers) {
        Json::Value geyser(Json::objectValue);
        geyser["type"] = item.type;
        geyser["x"] = item.x;
        geyser["y"] = item.y;
        if (item.type >= 0 && item.type < static_cast<int>(ids.size())) {
            geyser["id"] = ids[static_cast<size_t>(item.type)];
        }
        geysers.append(geyser);
    }
    summary["geysers"] = geysers;
    return summary;
}

Json::Value BuildPreviewJson(const GeneratedWorldPreview &preview)
{
    Json::Value root(Json::objectValue);
    root["summary"]["seed"] = preview.summary.seed;
    root["summary"]["worldType"] = preview.summary.worldType;
    root["summary"]["start"]["x"] = preview.summary.start.x;
    root["summary"]["start"]["y"] = preview.summary.start.y;
    root["summary"]["worldSize"]["w"] = preview.summary.worldSize.x;
    root["summary"]["worldSize"]["h"] = preview.summary.worldSize.y;

    Json::Value traits(Json::arrayValue);
    for (const auto &item : preview.summary.traits) {
        traits.append(item.id);
    }
    root["summary"]["traits"] = traits;

    const auto &ids = GetGeyserIds();
    Json::Value geysers(Json::arrayValue);
    for (const auto &item : preview.summary.geysers) {
        Json::Value geyser(Json::objectValue);
        geyser["type"] = item.type;
        geyser["x"] = item.x;
        geyser["y"] = item.y;
        if (item.type >= 0 && item.type < static_cast<int>(ids.size())) {
            geyser["id"] = ids[static_cast<size_t>(item.type)];
        }
        geysers.append(geyser);
    }
    root["summary"]["geysers"] = geysers;

    Json::Value polygons(Json::arrayValue);
    for (const auto &polygon : preview.polygons) {
        Json::Value polygonJson(Json::objectValue);
        polygonJson["hasHole"] = polygon.hasHole;
        polygonJson["zoneType"] = polygon.zoneType;
        Json::Value vertices(Json::arrayValue);
        for (const auto &vertex : polygon.vertices) {
            Json::Value point(Json::arrayValue);
            point.append(vertex.x);
            point.append(vertex.y);
            vertices.append(point);
        }
        polygonJson["vertices"] = vertices;
        polygons.append(polygonJson);
    }
    root["polygons"] = polygons;
    return root;
}

} // namespace

SidecarParseResult ParseSidecarRequest(const std::string &jsonText)
{
    SidecarParseResult result;

    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    std::istringstream stream(jsonText);
    if (!Json::parseFromStream(builder, stream, &root, &errors)) {
        result.error = "json parse failed: " + errors;
        return result;
    }
    if (!root.isObject()) {
        result.error = "request must be JSON object";
        return result;
    }

    std::string command;
    if (!RequireString(root, "command", &command, &result.error)) {
        return result;
    }

    if (command == "search") {
        result.request.command = SidecarCommandType::Search;
        auto &request = result.request.search;
        if (!RequireString(root, "jobId", &request.jobId, &result.error)) {
            return result;
        }
        if (!RequireInt(root, "worldType", &request.worldType, &result.error)) {
            return result;
        }
        if (!RequireInt(root, "seedStart", &request.seedStart, &result.error)) {
            return result;
        }
        if (!RequireInt(root, "seedEnd", &request.seedEnd, &result.error)) {
            return result;
        }
        request.mixing = root.get("mixing", request.mixing).asInt();
        request.threads = std::max(0, root.get("threads", request.threads).asInt());
        if (request.seedEnd < request.seedStart) {
            SetParseError(result, "seedEnd must be >= seedStart");
            return result;
        }

        const Json::Value cpu = root["cpu"];
        if (!cpu.isNull()) {
            if (!cpu.isObject()) {
                SetParseError(result, "cpu must be object");
                return result;
            }
            request.cpu.hasValue = true;
            request.cpu.mode = cpu.get("mode", request.cpu.mode).asString();
            request.cpu.workers = std::max(0, cpu.get("workers", request.cpu.workers).asInt());
            request.cpu.allowSmt = cpu.get("allowSmt", request.cpu.allowSmt).asBool();
            request.cpu.allowLowPerf = cpu.get("allowLowPerf", request.cpu.allowLowPerf).asBool();
            request.cpu.placement = cpu.get("placement", request.cpu.placement).asString();
            request.cpu.enableWarmup = cpu.get("enableWarmup", request.cpu.enableWarmup).asBool();
            request.cpu.enableAdaptiveDown = cpu.get("enableAdaptiveDown", request.cpu.enableAdaptiveDown).asBool();
            request.cpu.chunkSize = cpu.get("chunkSize", request.cpu.chunkSize).asInt();
            request.cpu.progressInterval = cpu.get("progressInterval", request.cpu.progressInterval).asInt();
            request.cpu.sampleWindowMs = cpu.get("sampleWindowMs", request.cpu.sampleWindowMs).asInt();
            request.cpu.adaptiveMinWorkers = cpu.get("adaptiveMinWorkers", request.cpu.adaptiveMinWorkers).asInt();
            request.cpu.adaptiveDropThreshold = cpu.get("adaptiveDropThreshold", request.cpu.adaptiveDropThreshold).asDouble();
            request.cpu.adaptiveDropWindows = cpu.get("adaptiveDropWindows", request.cpu.adaptiveDropWindows).asInt();
            request.cpu.adaptiveCooldownMs = cpu.get("adaptiveCooldownMs", request.cpu.adaptiveCooldownMs).asInt();
        }

        const Json::Value constraints = root["constraints"];
        if (!constraints.isNull() && !constraints.isObject()) {
            SetParseError(result, "constraints must be object");
            return result;
        }
        if (!ParseStringArray(constraints["required"],
                              "constraints.required",
                              &request.constraints.required,
                              &result.error)) {
            return result;
        }
        if (!ParseStringArray(constraints["forbidden"],
                              "constraints.forbidden",
                              &request.constraints.forbidden,
                              &result.error)) {
            return result;
        }

        const Json::Value distance = constraints["distance"];
        if (!distance.isNull()) {
            if (!distance.isArray()) {
                SetParseError(result, "constraints.distance must be array");
                return result;
            }
            int index = 0;
            for (const auto &item : distance) {
                if (!item.isObject()) {
                    SetParseError(result, "constraints.distance item must be object");
                    return result;
                }
                SidecarDistanceRule rule;
                if (!RequireString(item, "geyser", &rule.geyserId, &result.error)) {
                    result.error = "constraints.distance[" + std::to_string(index) +
                                   "]: " + result.error;
                    return result;
                }
                if (GeyserIdToIndex(rule.geyserId) < 0) {
                    result.error = "constraints.distance[" + std::to_string(index) +
                                   "]: unknown geyser id";
                    return result;
                }
                if (!RequireNumber(item, "minDist", &rule.minDist, &result.error)) {
                    result.error = "constraints.distance[" + std::to_string(index) +
                                   "]: " + result.error;
                    return result;
                }
                if (!RequireNumber(item, "maxDist", &rule.maxDist, &result.error)) {
                    result.error = "constraints.distance[" + std::to_string(index) +
                                   "]: " + result.error;
                    return result;
                }
                request.constraints.distance.push_back(std::move(rule));
                ++index;
            }
        }
        return result;
    }

    if (command == "preview") {
        result.request.command = SidecarCommandType::Preview;
        auto &request = result.request.preview;
        if (!RequireString(root, "jobId", &request.jobId, &result.error)) {
            return result;
        }
        if (!RequireInt(root, "worldType", &request.worldType, &result.error)) {
            return result;
        }
        if (!RequireInt(root, "seed", &request.seed, &result.error)) {
            return result;
        }
        request.mixing = root.get("mixing", request.mixing).asInt();
        return result;
    }

    if (command == "cancel") {
        result.request.command = SidecarCommandType::Cancel;
        if (!RequireString(root, "jobId", &result.request.cancel.jobId, &result.error)) {
            return result;
        }
        return result;
    }

    result.error = "unknown command: " + command;
    return result;
}

FilterConfig BuildFilterConfigFromSidecarSearch(const SidecarSearchRequest &request,
                                                std::vector<FilterError> *errors)
{
    FilterConfig cfg;
    cfg.worldType = request.worldType;
    cfg.seedStart = request.seedStart;
    cfg.seedEnd = request.seedEnd;
    cfg.mixing = request.mixing;
    cfg.threads = std::max(0, request.threads);
    if (request.cpu.hasValue) {
        cfg.hasCpuSection = true;
        cfg.cpu.mode = request.cpu.mode;
        cfg.cpu.workers = std::max(0, request.cpu.workers);
        cfg.cpu.allowSmt = request.cpu.allowSmt;
        cfg.cpu.allowLowPerf = request.cpu.allowLowPerf;
        cfg.cpu.placement = request.cpu.placement;
        cfg.cpu.enableWarmup = request.cpu.enableWarmup;
        cfg.cpu.enableAdaptiveDown = request.cpu.enableAdaptiveDown;
        cfg.cpu.chunkSize = std::clamp(request.cpu.chunkSize, 1, 2048);
        cfg.cpu.progressInterval = std::clamp(request.cpu.progressInterval, 1, 1000000);
        cfg.cpu.sampleWindowMs = std::clamp(request.cpu.sampleWindowMs, 200, 10000);
        cfg.cpu.adaptiveMinWorkers = std::clamp(request.cpu.adaptiveMinWorkers, 1, 1024);
        cfg.cpu.adaptiveDropThreshold = std::clamp(request.cpu.adaptiveDropThreshold, 0.0, 0.5);
        cfg.cpu.adaptiveDropWindows = std::clamp(request.cpu.adaptiveDropWindows, 1, 10);
        cfg.cpu.adaptiveCooldownMs = std::clamp(request.cpu.adaptiveCooldownMs, 1000, 60000);
    }

    auto appendError = [&](FilterErrorCode code, std::string field, std::string detail) {
        if (errors == nullptr) {
            return;
        }
        errors->push_back(FilterError{
            .code = code,
            .field = std::move(field),
            .detail = std::move(detail),
        });
    };

    for (const auto &id : request.constraints.required) {
        const int index = GeyserIdToIndex(id);
        if (index < 0) {
            appendError(FilterErrorCode::UnknownRequiredGeyserId, "constraints.required", id);
            continue;
        }
        cfg.required.push_back(index);
    }

    for (const auto &id : request.constraints.forbidden) {
        const int index = GeyserIdToIndex(id);
        if (index < 0) {
            appendError(FilterErrorCode::UnknownForbiddenGeyserId, "constraints.forbidden", id);
            continue;
        }
        cfg.forbidden.push_back(index);
    }

    for (const auto &rule : request.constraints.distance) {
        const int index = GeyserIdToIndex(rule.geyserId);
        if (index < 0) {
            appendError(FilterErrorCode::UnknownDistanceGeyserId,
                        "constraints.distance.geyser",
                        rule.geyserId);
            continue;
        }
        cfg.distanceRules.push_back(FilterConfig::DistRule{
            .type = index,
            .minDist = rule.minDist,
            .maxDist = rule.maxDist,
        });
    }

    return cfg;
}

std::string SerializeStartedEvent(const std::string &jobId,
                                  const SearchStartedEvent &event)
{
    Json::Value root = BuildBaseEventJson("started", jobId);
    root["seedStart"] = event.seedStart;
    root["seedEnd"] = event.seedEnd;
    root["totalSeeds"] = event.totalSeeds;
    root["workerCount"] = event.workerCount;
    return WriteCompactJson(root);
}

std::string SerializeProgressEvent(const std::string &jobId,
                                   const SearchProgressEvent &event)
{
    Json::Value root = BuildBaseEventJson("progress", jobId);
    root["processedSeeds"] = event.processedSeeds;
    root["totalSeeds"] = event.totalSeeds;
    root["totalMatches"] = event.totalMatches;
    root["activeWorkers"] = event.activeWorkers;
    if (event.hasWindowSample) {
        root["windowSeedsPerSecond"] = event.windowSeedsPerSecond;
    }
    root["hasWindowSample"] = event.hasWindowSample;
    root["activeWorkersReduced"] = event.activeWorkersReduced;
    root["peakSeedsPerSecond"] = event.peakSeedsPerSecond;
    return WriteCompactJson(root);
}

std::string SerializeMatchEvent(const std::string &jobId,
                                const SearchMatchEvent &event)
{
    Json::Value root = BuildBaseEventJson("match", jobId);
    root["seed"] = event.seed;
    root["processedSeeds"] = event.processedSeeds;
    root["totalSeeds"] = event.totalSeeds;
    root["totalMatches"] = event.totalMatches;
    root["summary"] = BuildCaptureSummaryJson(event.capture);
    return WriteCompactJson(root);
}

std::string SerializeCompletedEvent(const std::string &jobId,
                                    const SearchCompletedEvent &event)
{
    Json::Value root = BuildBaseEventJson("completed", jobId);
    root["processedSeeds"] = event.processedSeeds;
    root["totalSeeds"] = event.totalSeeds;
    root["totalMatches"] = event.totalMatches;
    root["finalActiveWorkers"] = event.finalActiveWorkers;
    root["autoFallbackCount"] = event.autoFallbackCount;
    root["stoppedByBudget"] = event.stoppedByBudget;
    root["throughput"] = ToThroughputJson(event.throughput);
    return WriteCompactJson(root);
}

std::string SerializeFailedEvent(const std::string &jobId,
                                 const SearchFailedEvent &event)
{
    Json::Value root = BuildBaseEventJson("failed", jobId);
    root["message"] = event.message;
    root["processedSeeds"] = event.processedSeeds;
    root["totalSeeds"] = event.totalSeeds;
    return WriteCompactJson(root);
}

std::string SerializeFailedEvent(const std::string &jobId,
                                 const std::string &message)
{
    SearchFailedEvent event;
    event.message = message;
    return SerializeFailedEvent(jobId, event);
}

std::string SerializeCancelledEvent(const std::string &jobId,
                                    const SearchCancelledEvent &event)
{
    Json::Value root = BuildBaseEventJson("cancelled", jobId);
    root["processedSeeds"] = event.processedSeeds;
    root["totalSeeds"] = event.totalSeeds;
    root["totalMatches"] = event.totalMatches;
    root["finalActiveWorkers"] = event.finalActiveWorkers;
    root["throughput"] = ToThroughputJson(event.throughput);
    return WriteCompactJson(root);
}

std::string SerializePreviewEvent(const std::string &jobId,
                                  const SidecarPreviewRequest &request,
                                  const GeneratedWorldPreview &preview)
{
    Json::Value root = BuildBaseEventJson("preview", jobId);
    root["worldType"] = request.worldType;
    root["seed"] = request.seed;
    root["mixing"] = request.mixing;
    root["preview"] = BuildPreviewJson(preview);
    return WriteCompactJson(root);
}

} // namespace Batch
