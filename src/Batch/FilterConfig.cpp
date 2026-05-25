#include "Batch/FilterConfig.hpp"

#include <fstream>
#include <sstream>

#include "Geyser/GeyserCatalog.hpp"

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

} // namespace

const std::vector<std::string> &GetGeyserIds()
{
    return Geyser::GetCatalogKeys();
}

int GeyserIdToIndex(const std::string &id)
{
    return Geyser::FindIdByKey(id);
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
        cfg.cpu.allowSmt = cpu.get("allowSmt", cfg.cpu.allowSmt).asBool();
        cfg.cpu.allowLowPerf = cpu.get("allowLowPerf", cfg.cpu.allowLowPerf).asBool();
        cfg.cpu.placement = cpu.get("binding", cpu.get("placement", cfg.cpu.placement)).asString();
        cfg.cpu.printMatches = cpu.get("printMatches", cfg.cpu.printMatches).asBool();
        cfg.cpu.printProgress = cpu.get("printProgress", cfg.cpu.printProgress).asBool();
        cfg.cpu.benchmarkSilent = cpu.get("benchmarkSilent", cfg.cpu.benchmarkSilent).asBool();
        cfg.cpu.printDiagnostics = cpu.get("printDiagnostics", cfg.cpu.printDiagnostics).asBool();
    }

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
