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
    int worldType{};
    Vector2i worldSize{};
    Vector2i start{};
    std::vector<TraitSummary> traits;
    std::vector<GeyserSummary> geysers;
};

struct GeneratedWorldPreview {
    GeneratedWorldSummary summary;
    std::vector<PolygonSummary> polygons;
};
