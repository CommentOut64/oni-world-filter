#include "Batch/FilterConfig.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#include <json/json.h>

namespace Batch {

namespace {

template<typename T>
T ClampValue(T value, T minValue, T maxValue)
{
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

void AddError(FilterConfigLoadResult &result,
              FilterErrorCode code,
              std::string field,
              std::string detail)
{
    result.errors.push_back(FilterError{
        .code = code,
        .field = std::move(field),
        .detail = std::move(detail),
    });
}

const std::vector<std::string> kGeyserIds = {
    "steam", "hot_steam", "hot_water", "slush_water", "filthy_water",
    "slush_salt_water", "salt_water", "small_volcano", "big_volcano",
    "liquid_co2", "hot_co2", "hot_hydrogen", "hot_po2", "slimy_po2",
    "chlorine_gas", "methane", "molten_copper", "molten_iron",
    "molten_gold", "molten_aluminum", "molten_cobalt",
    "oil_drip", "liquid_sulfur", "chlorine_gas_cool",
    "molten_tungsten", "molten_niobium",
    "printing_pod", "oil_reservoir", "warp_sender", "warp_receiver",
    "warp_portal", "cryo_tank",
};

} // namespace

const std::vector<std::string> &GetGeyserIds()
{
    return kGeyserIds;
}

int GeyserIdToIndex(const std::string &id)
{
    auto itr = std::find(kGeyserIds.begin(), kGeyserIds.end(), id);
    if (itr == kGeyserIds.end()) {
        return -1;
    }
    return static_cast<int>(std::distance(kGeyserIds.begin(), itr));
}

std::string FormatFilterError(const FilterError &error)
{
    std::ostringstream stream;
    stream << error.field;
    if (!error.detail.empty()) {
        stream << ": " << error.detail;
    }
    return stream.str();
}

FilterConfigLoadResult LoadFilterConfig(const std::string &path)
{
    FilterConfigLoadResult result;

    std::ifstream file(path);
    if (!file.is_open()) {
        AddError(result, FilterErrorCode::FileOpenFailed, "path", "cannot open filter file");
        return result;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;
    if (!Json::parseFromStream(builder, file, &root, &errs)) {
        AddError(result, FilterErrorCode::JsonParseFailed, "json", errs);
        return result;
    }

    auto &cfg = result.config;
    cfg.worldType = root.get("worldType", cfg.worldType).asInt();
    cfg.seedStart = root.get("seedStart", cfg.seedStart).asInt();
    cfg.seedEnd = root.get("seedEnd", cfg.seedEnd).asInt();
    cfg.mixing = root.get("mixing", cfg.mixing).asInt();
    cfg.threads = std::max(0, root.get("threads", cfg.threads).asInt());
    if (cfg.seedStart > cfg.seedEnd) {
        AddError(result,
                 FilterErrorCode::InvalidSeedRange,
                 "seedStart/seedEnd",
                 "seedStart must be <= seedEnd");
    }

    const Json::Value cpu = root["cpu"];
    if (cpu.isObject()) {
        cfg.hasCpuSection = true;
        cfg.cpu.mode = cpu.get("mode", cfg.cpu.mode).asString();
        cfg.cpu.workers = std::max(0, cpu.get("workers", cfg.cpu.workers).asInt());
        cfg.cpu.allowSmt = cpu.get("allowSmt", cfg.cpu.allowSmt).asBool();
        cfg.cpu.allowLowPerf = cpu.get("allowLowPerf", cfg.cpu.allowLowPerf).asBool();
        cfg.cpu.placement = cpu.get("placement", cfg.cpu.placement).asString();
        cfg.cpu.enableWarmup = cpu.get("enableWarmup", cfg.cpu.enableWarmup).asBool();
        cfg.cpu.warmupTotalMs = cpu.get("warmupTotalMs", cfg.cpu.warmupTotalMs).asInt();
        cfg.cpu.warmupPerCandidateMs = cpu.get("warmupPerCandidateMs", cfg.cpu.warmupPerCandidateMs).asInt();
        cfg.cpu.warmupSeedCount = cpu.get("warmupSeedCount", cfg.cpu.warmupSeedCount).asInt();
        cfg.cpu.warmupTieTolerance = cpu.get("warmupTieTolerance", cfg.cpu.warmupTieTolerance).asDouble();
        cfg.cpu.warmupMinSampledSeeds = cpu.get("warmupMinSampledSeeds", cfg.cpu.warmupMinSampledSeeds).asInt();
        cfg.cpu.warmupMaxRetry = cpu.get("warmupMaxRetry", cfg.cpu.warmupMaxRetry).asInt();
        cfg.cpu.enableAdaptiveDown = cpu.get("enableAdaptiveDown", cfg.cpu.enableAdaptiveDown).asBool();
        cfg.cpu.adaptiveMinWorkers = cpu.get("adaptiveMinWorkers", cfg.cpu.adaptiveMinWorkers).asInt();
        cfg.cpu.adaptiveDropThreshold = cpu.get("adaptiveDropThreshold", cfg.cpu.adaptiveDropThreshold).asDouble();
        cfg.cpu.adaptiveDropWindows = cpu.get("adaptiveDropWindows", cfg.cpu.adaptiveDropWindows).asInt();
        cfg.cpu.adaptiveCooldownMs = cpu.get("adaptiveCooldownMs", cfg.cpu.adaptiveCooldownMs).asInt();
        cfg.cpu.sampleWindowMs = cpu.get("sampleWindowMs", cfg.cpu.sampleWindowMs).asInt();
        cfg.cpu.chunkSize = cpu.get("chunkSize", cfg.cpu.chunkSize).asInt();
        cfg.cpu.progressInterval = cpu.get("progressInterval", cfg.cpu.progressInterval).asInt();
        cfg.cpu.printMatches = cpu.get("printMatches", cfg.cpu.printMatches).asBool();
        cfg.cpu.printProgress = cpu.get("printProgress", cfg.cpu.printProgress).asBool();
        cfg.cpu.benchmarkSilent = cpu.get("benchmarkSilent", cfg.cpu.benchmarkSilent).asBool();
        cfg.cpu.printDiagnostics = cpu.get("printDiagnostics", cfg.cpu.printDiagnostics).asBool();
    }

    cfg.cpu.warmupTotalMs = ClampValue(cfg.cpu.warmupTotalMs, 1000, 30000);
    cfg.cpu.warmupPerCandidateMs = ClampValue(cfg.cpu.warmupPerCandidateMs, 500, 8000);
    cfg.cpu.warmupSeedCount = ClampValue(cfg.cpu.warmupSeedCount, 256, 200000);
    cfg.cpu.warmupTieTolerance = ClampValue(cfg.cpu.warmupTieTolerance, 0.0, 0.2);
    cfg.cpu.warmupMinSampledSeeds = ClampValue(cfg.cpu.warmupMinSampledSeeds, 32, 200000);
    cfg.cpu.warmupMaxRetry = ClampValue(cfg.cpu.warmupMaxRetry, 0, 5);
    cfg.cpu.adaptiveMinWorkers = ClampValue(cfg.cpu.adaptiveMinWorkers, 1, 1024);
    cfg.cpu.adaptiveDropThreshold = ClampValue(cfg.cpu.adaptiveDropThreshold, 0.0, 0.5);
    cfg.cpu.adaptiveDropWindows = ClampValue(cfg.cpu.adaptiveDropWindows, 1, 10);
    cfg.cpu.adaptiveCooldownMs = ClampValue(cfg.cpu.adaptiveCooldownMs, 1000, 60000);
    cfg.cpu.sampleWindowMs = ClampValue(cfg.cpu.sampleWindowMs, 200, 10000);
    cfg.cpu.chunkSize = ClampValue(cfg.cpu.chunkSize, 1, 2048);
    cfg.cpu.progressInterval = ClampValue(cfg.cpu.progressInterval, 1, 1000000);

    for (const auto &value : root["required"]) {
        const std::string geyserId = value.asString();
        const int index = GeyserIdToIndex(geyserId);
        if (index < 0) {
            AddError(result,
                     FilterErrorCode::UnknownRequiredGeyserId,
                     "required",
                     geyserId);
            continue;
        }
        cfg.required.push_back(index);
    }

    for (const auto &value : root["forbidden"]) {
        const std::string geyserId = value.asString();
        const int index = GeyserIdToIndex(geyserId);
        if (index < 0) {
            AddError(result,
                     FilterErrorCode::UnknownForbiddenGeyserId,
                     "forbidden",
                     geyserId);
            continue;
        }
        cfg.forbidden.push_back(index);
    }

    int distanceIndex = 0;
    for (const auto &value : root["distance"]) {
        const std::string baseField = "distance[" + std::to_string(distanceIndex) + "]";
        ++distanceIndex;
        if (!value.isObject()) {
            AddError(result,
                     FilterErrorCode::MissingDistanceField,
                     baseField,
                     "distance rule must be object");
            continue;
        }
        if (!value.isMember("geyser")) {
            AddError(result,
                     FilterErrorCode::MissingDistanceField,
                     baseField + ".geyser",
                     "required field is missing");
            continue;
        }
        if (!value.isMember("minDist")) {
            AddError(result,
                     FilterErrorCode::MissingDistanceField,
                     baseField + ".minDist",
                     "required field is missing");
            continue;
        }
        if (!value.isMember("maxDist")) {
            AddError(result,
                     FilterErrorCode::MissingDistanceField,
                     baseField + ".maxDist",
                     "required field is missing");
            continue;
        }

        const std::string geyserId = value["geyser"].asString();
        const int geyserType = GeyserIdToIndex(geyserId);
        if (geyserType < 0) {
            AddError(result,
                     FilterErrorCode::UnknownDistanceGeyserId,
                     baseField + ".geyser",
                     geyserId);
            continue;
        }

        FilterConfig::DistRule rule;
        rule.type = geyserType;
        rule.minDist = value["minDist"].asFloat();
        rule.maxDist = value["maxDist"].asFloat();
        cfg.distanceRules.push_back(rule);
    }

    int countIndex = 0;
    for (const auto &value : root["count"]) {
        const std::string baseField = "count[" + std::to_string(countIndex) + "]";
        ++countIndex;
        if (!value.isObject()) {
            AddError(result,
                     FilterErrorCode::MissingCountField,
                     baseField,
                     "count rule must be object");
            continue;
        }
        if (!value.isMember("geyser")) {
            AddError(result,
                     FilterErrorCode::MissingCountField,
                     baseField + ".geyser",
                     "required field is missing");
            continue;
        }
        if (!value.isMember("minCount")) {
            AddError(result,
                     FilterErrorCode::MissingCountField,
                     baseField + ".minCount",
                     "required field is missing");
            continue;
        }
        if (!value.isMember("maxCount")) {
            AddError(result,
                     FilterErrorCode::MissingCountField,
                     baseField + ".maxCount",
                     "required field is missing");
            continue;
        }

        const std::string geyserId = value["geyser"].asString();
        const int geyserType = GeyserIdToIndex(geyserId);
        if (geyserType < 0) {
            AddError(result,
                     FilterErrorCode::UnknownCountGeyserId,
                     baseField + ".geyser",
                     geyserId);
            continue;
        }

        const int minCount = value["minCount"].asInt();
        const int maxCount = value["maxCount"].asInt();
        if (minCount < 0 || maxCount < 0 || minCount > maxCount) {
            AddError(result,
                     FilterErrorCode::InvalidCountRange,
                     baseField + ".minCount/maxCount",
                     "count range is invalid");
            continue;
        }

        cfg.countRules.push_back(FilterConfig::CountRule{
            .type = geyserType,
            .minCount = minCount,
            .maxCount = maxCount,
        });
    }

    return result;
}

} // namespace Batch
