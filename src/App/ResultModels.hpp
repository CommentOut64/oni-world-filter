#pragma once

#include <string>
#include <vector>
#include "Utils/Vector2f.hpp"

struct TraitSummary {
    int id{};
};

struct GeyserSummary {
    int type{};
    int x{};
    int y{};
};

struct GeyserNativeParameters {
    float averageActiveYieldKgPerCycle{};
    float eruptionPeriodSeconds{};
    float eruptionRatio{};
    float activePeriodSeconds{};
    float activeRatio{};
};

struct GeyserDerivedParameters {
    float eruptionRateKgPerSecond{};
    float averageOverallYieldGPerSecond{};
    float eruptionSeconds{};
    float activeSeconds{};
    float activeCycles{};
    float totalCycles{};
    float temperatureCelsius{};
};

struct GeyserDetail {
    int index{};
    GeyserSummary summary{};
    bool hasParameters{false};
    std::string parameterKind;
    GeyserNativeParameters native{};
    GeyserDerivedParameters derived{};
};

struct PolygonSummary {
    bool hasHole{};
    int zoneType{};
    std::vector<Vector2i> vertices;
};

struct GeneratedWorldSummary {
    int seed{};
    int worldType{}; // 兼容旧链路：0=主星，1=非主星
    int worldPlacementIndex{-1};
    bool isPrimary{};
    Vector2i worldSize{};
    Vector2i start{};
    std::vector<TraitSummary> traits;
    std::vector<GeyserSummary> geysers;
};

struct GeneratedWorldPreview {
    GeneratedWorldSummary summary;
    std::vector<PolygonSummary> polygons;
};

struct WorldReportData {
    GeneratedWorldPreview preview;
    std::vector<GeyserDetail> geyserDetails;
    int mixing{};
    std::string coord;
};

inline const GeneratedWorldPreview *FindPrimaryGeneratedWorldPreview(
    const std::vector<GeneratedWorldPreview> &previews)
{
    for (const auto &preview : previews) {
        if (preview.summary.isPrimary) {
            return &preview;
        }
    }
    return previews.empty() ? nullptr : &previews.front();
}
