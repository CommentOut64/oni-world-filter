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

bool ParseCpuConfig(const Json::Value &root,
                    SidecarCpuConfig *cpu,
                    SidecarParseResult *result)
{
    if (cpu == nullptr || result == nullptr) {
        return false;
    }
    const Json::Value cpuNode = root["cpu"];
    if (cpuNode.isNull()) {
        return true;
    }
    if (!cpuNode.isObject()) {
        SetParseError(*result, "cpu must be object");
        return false;
    }
    cpu->hasValue = true;
    cpu->mode = cpuNode.get("mode", cpu->mode).asString();
    cpu->workers = std::max(0, cpuNode.get("workers", cpu->workers).asInt());
    cpu->allowSmt = cpuNode.get("allowSmt", cpu->allowSmt).asBool();
    cpu->allowLowPerf = cpuNode.get("allowLowPerf", cpu->allowLowPerf).asBool();
    cpu->placement = cpuNode.get("placement", cpu->placement).asString();
    cpu->enableWarmup = cpuNode.get("enableWarmup", cpu->enableWarmup).asBool();
    cpu->enableAdaptiveDown = cpuNode.get("enableAdaptiveDown", cpu->enableAdaptiveDown).asBool();
    cpu->chunkSize = cpuNode.get("chunkSize", cpu->chunkSize).asInt();
    cpu->progressInterval = cpuNode.get("progressInterval", cpu->progressInterval).asInt();
    cpu->sampleWindowMs = cpuNode.get("sampleWindowMs", cpu->sampleWindowMs).asInt();
    cpu->adaptiveMinWorkers = cpuNode.get("adaptiveMinWorkers", cpu->adaptiveMinWorkers).asInt();
    cpu->adaptiveDropThreshold = cpuNode.get("adaptiveDropThreshold", cpu->adaptiveDropThreshold).asDouble();
    cpu->adaptiveDropWindows = cpuNode.get("adaptiveDropWindows", cpu->adaptiveDropWindows).asInt();
    cpu->adaptiveCooldownMs = cpuNode.get("adaptiveCooldownMs", cpu->adaptiveCooldownMs).asInt();
    return true;
}

bool ParseConstraints(const Json::Value &root,
                      SidecarConstraints *constraints,
                      SidecarParseResult *result,
                      bool validateDistanceGeyserId)
{
    if (constraints == nullptr || result == nullptr) {
        return false;
    }
    const Json::Value constraintsNode = root["constraints"];
    if (!constraintsNode.isNull() && !constraintsNode.isObject()) {
        SetParseError(*result, "constraints must be object");
        return false;
    }
    if (!ParseStringArray(constraintsNode["required"],
                          "constraints.required",
                          &constraints->required,
                          &result->error)) {
        return false;
    }
    if (!ParseStringArray(constraintsNode["forbidden"],
                          "constraints.forbidden",
                          &constraints->forbidden,
                          &result->error)) {
        return false;
    }

    const Json::Value distance = constraintsNode["distance"];
    if (!distance.isNull()) {
        if (!distance.isArray()) {
            SetParseError(*result, "constraints.distance must be array");
            return false;
        }
        int index = 0;
        for (const auto &item : distance) {
            if (!item.isObject()) {
                SetParseError(*result, "constraints.distance item must be object");
                return false;
            }
            SidecarDistanceRule rule;
            if (!RequireString(item, "geyser", &rule.geyserId, &result->error)) {
                result->error = "constraints.distance[" + std::to_string(index) +
                                "]: " + result->error;
                return false;
            }
            if (validateDistanceGeyserId && GeyserIdToIndex(rule.geyserId) < 0) {
                result->error = "constraints.distance[" + std::to_string(index) +
                                "]: unknown geyser id";
                return false;
            }
            if (!RequireNumber(item, "minDist", &rule.minDist, &result->error)) {
                result->error = "constraints.distance[" + std::to_string(index) +
                                "]: " + result->error;
                return false;
            }
            if (!RequireNumber(item, "maxDist", &rule.maxDist, &result->error)) {
                result->error = "constraints.distance[" + std::to_string(index) +
                                "]: " + result->error;
                return false;
            }
            constraints->distance.push_back(std::move(rule));
            ++index;
        }
    }

    const Json::Value count = constraintsNode["count"];
    if (!count.isNull()) {
        if (!count.isArray()) {
            SetParseError(*result, "constraints.count must be array");
            return false;
        }
        int index = 0;
        for (const auto &item : count) {
            if (!item.isObject()) {
                SetParseError(*result, "constraints.count item must be object");
                return false;
            }
            SidecarCountRule rule;
            if (!RequireString(item, "geyser", &rule.geyserId, &result->error)) {
                result->error = "constraints.count[" + std::to_string(index) +
                                "]: " + result->error;
                return false;
            }
            if (!RequireInt(item, "minCount", &rule.minCount, &result->error)) {
                result->error = "constraints.count[" + std::to_string(index) +
                                "]: " + result->error;
                return false;
            }
            if (!RequireInt(item, "maxCount", &rule.maxCount, &result->error)) {
                result->error = "constraints.count[" + std::to_string(index) +
                                "]: " + result->error;
                return false;
            }
            constraints->count.push_back(std::move(rule));
            ++index;
        }
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

Json::Value BuildSearchCatalogJson(const SearchAnalysis::SearchCatalog &catalog)
{
    Json::Value root(Json::objectValue);

    Json::Value worlds(Json::arrayValue);
    for (const auto &item : catalog.worlds) {
        Json::Value world(Json::objectValue);
        world["id"] = item.id;
        world["code"] = item.code;
        worlds.append(world);
    }
    root["worlds"] = worlds;

    Json::Value geysers(Json::arrayValue);
    for (const auto &item : catalog.geysers) {
        Json::Value geyser(Json::objectValue);
        geyser["id"] = item.id;
        geyser["key"] = item.key;
        geysers.append(geyser);
    }
    root["geysers"] = geysers;

    Json::Value traits(Json::arrayValue);
    for (const auto &item : catalog.traits) {
        Json::Value trait(Json::objectValue);
        trait["id"] = item.id;
        trait["name"] = item.name;
        trait["description"] = item.description;

        Json::Value traitTags(Json::arrayValue);
        for (const auto &tag : item.traitTags) {
            traitTags.append(tag);
        }
        trait["traitTags"] = traitTags;

        Json::Value exclusiveWith(Json::arrayValue);
        for (const auto &id : item.exclusiveWith) {
            exclusiveWith.append(id);
        }
        trait["exclusiveWith"] = exclusiveWith;

        Json::Value exclusiveWithTags(Json::arrayValue);
        for (const auto &tag : item.exclusiveWithTags) {
            exclusiveWithTags.append(tag);
        }
        trait["exclusiveWithTags"] = exclusiveWithTags;

        Json::Value forbiddenDLCIds(Json::arrayValue);
        for (const auto &id : item.forbiddenDLCIds) {
            forbiddenDLCIds.append(id);
        }
        trait["forbiddenDLCIds"] = forbiddenDLCIds;

        Json::Value effectSummary(Json::arrayValue);
        for (const auto &summary : item.effectSummary) {
            effectSummary.append(summary);
        }
        trait["effectSummary"] = effectSummary;
        trait["searchable"] = item.searchable;

        traits.append(trait);
    }
    root["traits"] = traits;

    Json::Value mixingSlots(Json::arrayValue);
    for (const auto &item : catalog.mixingSlots) {
        Json::Value slot(Json::objectValue);
        slot["slot"] = item.slot;
        slot["path"] = item.path;
        slot["type"] = item.type;
        slot["name"] = item.name;
        slot["description"] = item.description;
        mixingSlots.append(slot);
    }
    root["mixingSlots"] = mixingSlots;

    Json::Value parameterSpecs(Json::arrayValue);
    for (const auto &item : catalog.parameterSpecs) {
        Json::Value spec(Json::objectValue);
        spec["id"] = item.id;
        spec["valueType"] = item.valueType;
        spec["meaning"] = item.meaning;
        spec["staticRange"] = item.staticRange;
        spec["supportsDynamicRange"] = item.supportsDynamicRange;
        spec["source"] = item.source;
        parameterSpecs.append(spec);
    }
    root["parameterSpecs"] = parameterSpecs;
    return root;
}

Json::Value BuildValidationIssuesJson(const std::vector<SearchAnalysis::ValidationIssue> &issues)
{
    Json::Value array(Json::arrayValue);
    for (const auto &issue : issues) {
        Json::Value item(Json::objectValue);
        item["layer"] = issue.layer;
        item["code"] = issue.code;
        item["field"] = issue.field;
        item["message"] = issue.message;
        array.append(item);
    }
    return array;
}

Json::Value BuildNormalizedSearchRequestJson(const SearchAnalysis::NormalizedSearchRequest &request)
{
    Json::Value root(Json::objectValue);
    root["worldType"] = request.worldType;
    root["seedStart"] = request.seedStart;
    root["seedEnd"] = request.seedEnd;
    root["mixing"] = request.mixing;
    root["threads"] = request.threads;

    Json::Value groups(Json::arrayValue);
    for (const auto &group : request.groups) {
        Json::Value item(Json::objectValue);
        item["geyserId"] = group.geyserId;
        item["geyserIndex"] = group.geyserIndex;
        item["minCount"] = group.minCount;
        item["maxCount"] = group.maxCount;
        item["hasRequired"] = group.hasRequired;
        item["hasForbidden"] = group.hasForbidden;
        item["hasExplicitCount"] = group.hasExplicitCount;

        Json::Value distance(Json::arrayValue);
        for (const auto &rule : group.distanceRules) {
            Json::Value distanceItem(Json::objectValue);
            distanceItem["geyser"] = rule.geyserId;
            distanceItem["minDist"] = rule.minDist;
            distanceItem["maxDist"] = rule.maxDist;
            distance.append(distanceItem);
        }
        item["distance"] = distance;
        groups.append(item);
    }
    root["groups"] = groups;
    return root;
}

Json::Value BuildSearchAnalysisJson(const SearchAnalysis::SearchAnalysisResult &analysis)
{
    Json::Value root(Json::objectValue);
    root["normalizedRequest"] = BuildNormalizedSearchRequestJson(analysis.normalizedRequest);
    root["errors"] = BuildValidationIssuesJson(analysis.errors);
    root["warnings"] = BuildValidationIssuesJson(analysis.warnings);
    Json::Value bottlenecks(Json::arrayValue);
    for (const auto &item : analysis.bottlenecks) {
        bottlenecks.append(item);
    }
    root["bottlenecks"] = bottlenecks;
    root["predictedBottleneckProbability"] = analysis.predictedBottleneckProbability;
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
        if (!ParseCpuConfig(root, &request.cpu, &result)) {
            return result;
        }
        if (!ParseConstraints(root, &request.constraints, &result, true)) {
            return result;
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

    if (command == "get_search_catalog") {
        result.request.command = SidecarCommandType::GetSearchCatalog;
        auto &request = result.request.getSearchCatalog;
        const Json::Value jobId = root["jobId"];
        if (jobId.isNull()) {
            return result;
        }
        if (!jobId.isString()) {
            SetParseError(result, "field must be string: jobId");
            return result;
        }
        request.jobId = jobId.asString();
        if (request.jobId.empty()) {
            request.jobId = "search-catalog";
        }
        return result;
    }

    if (command == "analyze_search_request") {
        result.request.command = SidecarCommandType::AnalyzeSearchRequest;
        auto &request = result.request.analyze;
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
        if (!ParseCpuConfig(root, &request.cpu, &result)) {
            return result;
        }
        if (!ParseConstraints(root, &request.constraints, &result, false)) {
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

std::string SerializeSearchCatalogEvent(const std::string &jobId,
                                        const SearchAnalysis::SearchCatalog &catalog)
{
    Json::Value root = BuildBaseEventJson("search_catalog", jobId);
    root["catalog"] = BuildSearchCatalogJson(catalog);
    return WriteCompactJson(root);
}

std::string SerializeSearchAnalysisEvent(const std::string &jobId,
                                         const SearchAnalysis::SearchAnalysisResult &analysis)
{
    Json::Value root = BuildBaseEventJson("search_analysis", jobId);
    root["analysis"] = BuildSearchAnalysisJson(analysis);
    return WriteCompactJson(root);
}

} // namespace Batch
