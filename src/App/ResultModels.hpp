#pragma once

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
