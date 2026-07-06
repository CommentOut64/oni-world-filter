#include "WorldGen.hpp"

#include <cmath>
#include <array>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <map>
#include <memory>
#include <unordered_set>
#include <queue>
#include <numeric>

#include "Utils/Voronoi.hpp"
#include "Utils/Diagram.hpp"
#include "Utils/PointGenerator.hpp"

struct WeightedSubWorld {
    const SubWorld *subWorld;
    float weight;
    float overridePower;
    int minCount;
    int maxCount;
    int priority;

    WeightedSubWorld(const SubWorld *subworld,
                     const WeightedSubworldName &weightedWorld)
        : subWorld{subworld}
        , weight{weightedWorld.weight}
        , overridePower{weightedWorld.overridePower}
        , minCount{weightedWorld.minCount}
        , maxCount{weightedWorld.maxCount}
        , priority{weightedWorld.priority}
    {
    }
};

static void ApplySwapTags(std::vector<Site> &sites, KRandom &random);
extern void WriteToBinary(const std::vector<Site> &sites);
extern void WriteToBinary(const std::vector<Site *> &sites);

namespace {

std::string EscapeJsonString(std::string_view value)
{
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (const char ch : value) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += ch;
            break;
        }
    }
    return escaped;
}

struct GridCellPoint {
    int x = 0;
    int y = 0;
};

bool operator==(const GridCellPoint &lhs, const GridCellPoint &rhs)
{
    return lhs.x == rhs.x && lhs.y == rhs.y;
}

int64_t EncodeCell(int x, int y)
{
    return (static_cast<int64_t>(x) << 32) ^
           static_cast<uint32_t>(y);
}

struct GridCellPointHash {
    size_t operator()(const GridCellPoint &point) const noexcept
    {
        return static_cast<size_t>(EncodeCell(point.x, point.y));
    }
};

struct FeatureRoomCells {
    std::unordered_set<GridCellPoint, GridCellPointHash> centerCells;
    std::unordered_set<GridCellPoint, GridCellPointHash> allCells;
    int centerCount = 0;
    std::vector<int> borderCounts;
    std::vector<GridCellPoint> orderedCenterCells;
    std::vector<std::vector<GridCellPoint>> orderedBorderCells;
};

enum class WorldgenCellMaterial : uint8_t {
    Gas = 0,
    Solid = 1,
    Liquid = 2,
};

struct FeatureAmbientLayout {
    const FeatureSettings *feature = nullptr;
    std::unordered_set<GridCellPoint, GridCellPointHash> spawnCells;
};

struct SimulatedTerrainState {
    std::map<int, FeatureAmbientLayout> featureCellsBySite;
    std::vector<uint8_t> solidCells;
    std::vector<uint8_t> liquidCells;
    std::unordered_set<int64_t> cavityCells;
};

struct OilWellCandidateAnalysis {
    int featureSpawnCellCount = 0;
    int candidateCount = 0;
    int rejectedOutOfBounds = 0;
    int rejectedClaimedAnchor = 0;
    int rejectedAnchorNotCavity = 0;
    int rejectedOccupied = 0;
    int rejectedNonEmpty = 0;
    int rejectedUnsupportedFloor = 0;
    std::vector<GridCellPoint> candidates;
};

struct OilWellDebugRecord {
    int siteIndex = 0;
    int parentIndex = -1;
    int childIndex = -1;
    std::string subworld;
    int zoneType = -1;
    GridCellPoint centroid{};
    int featureSpawnCellCount = 0;
    int candidateCount = 0;
    int rejectedOutOfBounds = 0;
    int rejectedClaimedAnchor = 0;
    int rejectedAnchorNotCavity = 0;
    int rejectedOccupied = 0;
    int rejectedNonEmpty = 0;
    int rejectedUnsupportedFloor = 0;
    std::vector<GridCellPoint> candidatePreview;
    std::vector<GridCellPoint> spawned;
};

class NoiseModule2D
{
public:
    virtual ~NoiseModule2D() = default;
    virtual float Evaluate(float x, float y) const = 0;
};

class ImprovedPerlinNoise2D : public NoiseModule2D
{
public:
    enum class Quality {
        Fast,
        Standard,
        Best,
    };

    ImprovedPerlinNoise2D(int seed, Quality quality)
        : m_quality(quality)
    {
        Randomize(seed);
    }

    float Evaluate(float x, float y) const override
    {
        const int floorX = x > 0.0f ? static_cast<int>(x) : static_cast<int>(x) - 1;
        const int floorY = y > 0.0f ? static_cast<int>(y) : static_cast<int>(y) - 1;
        const int indexX = floorX & 0xFF;
        const int indexY = floorY & 0xFF;
        x -= static_cast<float>(floorX);
        y -= static_cast<float>(floorY);

        float curveX = 0.0f;
        float curveY = 0.0f;
        switch (m_quality) {
        case Quality::Fast:
            curveX = x;
            curveY = y;
            break;
        case Quality::Standard:
            curveX = SCurve3(x);
            curveY = SCurve3(y);
            break;
        case Quality::Best:
            curveX = SCurve5(x);
            curveY = SCurve5(y);
            break;
        }

        const int perm0 = m_random[static_cast<size_t>(indexX)] + indexY;
        const int perm1 = m_random[static_cast<size_t>(indexX + 1)] + indexY;
        return Lerp(Lerp(Grad(m_random[static_cast<size_t>(perm0)], x, y),
                         Grad(m_random[static_cast<size_t>(perm1)], x - 1.0f, y),
                         curveX),
                    Lerp(Grad(m_random[static_cast<size_t>(perm0 + 1)], x, y - 1.0f),
                         Grad(m_random[static_cast<size_t>(perm1 + 1)], x - 1.0f, y - 1.0f),
                         curveX),
                    curveY);
    }

protected:
    std::array<int, 512> m_random{};
    Quality m_quality = Quality::Best;

private:
    static constexpr std::array<int, 256> kSource = {
        151, 160, 137, 91, 90, 15, 131, 13, 201, 95, 96, 53, 194, 233, 7, 225,
        140, 36, 103, 30, 69, 142, 8, 99, 37, 240, 21, 10, 23, 190, 6, 148,
        247, 120, 234, 75, 0, 26, 197, 62, 94, 252, 219, 203, 117, 35, 11, 32,
        57, 177, 33, 88, 237, 149, 56, 87, 174, 20, 125, 136, 171, 168, 68, 175,
        74, 165, 71, 134, 139, 48, 27, 166, 77, 146, 158, 231, 83, 111, 229, 122,
        60, 211, 133, 230, 220, 105, 92, 41, 55, 46, 245, 40, 244, 102, 143, 54,
        65, 25, 63, 161, 1, 216, 80, 73, 209, 76, 132, 187, 208, 89, 18, 169,
        200, 196, 135, 130, 116, 188, 159, 86, 164, 100, 109, 198, 173, 186, 3, 64,
        52, 217, 226, 250, 124, 123, 5, 202, 38, 147, 118, 126, 255, 82, 85, 212,
        207, 206, 59, 227, 47, 16, 58, 17, 182, 189, 28, 42, 223, 183, 170, 213,
        119, 248, 152, 2, 44, 154, 163, 70, 221, 153, 101, 155, 167, 43, 172, 9,
        129, 22, 39, 253, 19, 98, 108, 110, 79, 113, 224, 232, 178, 185, 112, 104,
        218, 246, 97, 228, 251, 34, 242, 193, 238, 210, 144, 12, 191, 179, 162, 241,
        81, 51, 145, 235, 249, 14, 239, 107, 49, 192, 214, 31, 181, 199, 106, 157,
        184, 84, 204, 176, 115, 121, 50, 45, 127, 4, 150, 254, 138, 236, 205, 93,
        222, 114, 67, 29, 24, 72, 243, 141, 128, 195, 78, 66, 215, 61, 156, 180,
    };

    static float Lerp(float a, float b, float t)
    {
        return a + t * (b - a);
    }

    static float SCurve3(float value)
    {
        return value * value * (3.0f - 2.0f * value);
    }

    static float SCurve5(float value)
    {
        return value * value * value *
               (value * (value * 6.0f - 15.0f) + 10.0f);
    }

    static float Grad(int hash, float x, float y)
    {
        const int h = hash & 3;
        const float gx = (h & 2) == 0 ? x : -x;
        const float gy = (h & 1) == 0 ? y : -y;
        return gx + gy;
    }

    void Randomize(int seed)
    {
        if (seed == 0) {
            for (size_t index = 0; index < kSource.size(); ++index) {
                m_random[index] = kSource[index];
                m_random[index + kSource.size()] = kSource[index];
            }
            return;
        }

        const std::array<uint8_t, 4> bytes = {
            static_cast<uint8_t>(seed & 0xFF),
            static_cast<uint8_t>((seed >> 8) & 0xFF),
            static_cast<uint8_t>((seed >> 16) & 0xFF),
            static_cast<uint8_t>((seed >> 24) & 0xFF),
        };
        for (size_t index = 0; index < kSource.size(); ++index) {
            int value = kSource[index];
            value ^= bytes[0];
            value ^= bytes[1];
            value ^= bytes[2];
            value ^= bytes[3];
            m_random[index] = value;
            m_random[index + kSource.size()] = value;
        }
    }
};

class SimplexPerlinNoise2D final : public ImprovedPerlinNoise2D
{
public:
    SimplexPerlinNoise2D(int seed, Quality quality)
        : ImprovedPerlinNoise2D(seed, quality)
    {
    }

    float Evaluate(float x, float y) const override
    {
        constexpr float f2 = 0.3660254f;
        constexpr float g2 = 0.21132487f;
        constexpr float g22 = g2 * 2.0f - 1.0f;
        constexpr std::array<std::array<int, 3>, 12> grad3 = {{
            {{1, 1, 0}},   {{-1, 1, 0}},  {{1, -1, 0}},  {{-1, -1, 0}},
            {{1, 0, 1}},   {{-1, 0, 1}},  {{1, 0, -1}},  {{-1, 0, -1}},
            {{0, 1, 1}},   {{0, -1, 1}},  {{0, 1, -1}},  {{0, -1, -1}},
        }};

        float n0 = 0.0f;
        float n1 = 0.0f;
        float n2 = 0.0f;
        const float skew = (x + y) * f2;
        const int floorX = x >= 0.0f ? static_cast<int>(x + skew)
                                     : static_cast<int>(x + skew) - 1;
        const int floorY = y >= 0.0f ? static_cast<int>(y + skew)
                                     : static_cast<int>(y + skew) - 1;
        const float unskew = static_cast<float>(floorX + floorY) * g2;
        const float x0 = x - (static_cast<float>(floorX) - unskew);
        const float y0 = y - (static_cast<float>(floorY) - unskew);
        const int stepX = x0 > y0 ? 1 : 0;
        const int stepY = x0 > y0 ? 0 : 1;
        const float x1 = x0 - static_cast<float>(stepX) + g2;
        const float y1 = y0 - static_cast<float>(stepY) + g2;
        const float x2 = x0 + g22;
        const float y2 = y0 + g22;
        const int permX = floorX & 0xFF;
        const int permY = floorY & 0xFF;

        float t0 = 0.5f - x0 * x0 - y0 * y0;
        if (t0 > 0.0f) {
            t0 *= t0;
            const int grad = m_random[static_cast<size_t>(
                                 permX + m_random[static_cast<size_t>(permY)])] %
                             static_cast<int>(grad3.size());
            n0 = t0 * t0 *
                 (static_cast<float>(grad3[grad][0]) * x0 +
                  static_cast<float>(grad3[grad][1]) * y0);
        }

        float t1 = 0.5f - x1 * x1 - y1 * y1;
        if (t1 > 0.0f) {
            t1 *= t1;
            const int grad = m_random[static_cast<size_t>(
                                 permX + stepX +
                                 m_random[static_cast<size_t>(permY + stepY)])] %
                             static_cast<int>(grad3.size());
            n1 = t1 * t1 *
                 (static_cast<float>(grad3[grad][0]) * x1 +
                  static_cast<float>(grad3[grad][1]) * y1);
        }

        float t2 = 0.5f - x2 * x2 - y2 * y2;
        if (t2 > 0.0f) {
            t2 *= t2;
            const int grad = m_random[static_cast<size_t>(
                                 permX + 1 +
                                 m_random[static_cast<size_t>(permY + 1)])] %
                             static_cast<int>(grad3.size());
            n2 = t2 * t2 *
                 (static_cast<float>(grad3[grad][0]) * x2 +
                  static_cast<float>(grad3[grad][1]) * y2);
        }
        return 70.0f * (n0 + n1 + n2);
    }
};

class RidgedMultiFractalNoise2D final : public NoiseModule2D
{
public:
    RidgedMultiFractalNoise2D(std::unique_ptr<NoiseModule2D> source,
                              float frequency,
                              float lacunarity,
                              int octaves,
                              float gain,
                              float offset,
                              float exponent)
        : m_source(std::move(source))
        , m_frequency(frequency)
        , m_lacunarity(lacunarity)
        , m_octaves(std::clamp(octaves, 1, 30))
        , m_gain(gain)
        , m_offset(offset)
    {
        for (int index = 0; index < static_cast<int>(m_spectralWeights.size()); ++index) {
            m_spectralWeights[static_cast<size_t>(index)] =
                std::pow(m_lacunarity, static_cast<float>(-index) * exponent);
        }
    }

    float Evaluate(float x, float y) const override
    {
        x *= m_frequency;
        y *= m_frequency;
        float signal = std::abs(m_source->Evaluate(x, y));
        signal = m_offset - signal;
        signal *= signal;
        float result = signal;
        float weight = 1.0f;
        for (int octave = 1; octave < m_octaves && weight > 0.001f; ++octave) {
            x *= m_lacunarity;
            y *= m_lacunarity;
            weight = std::clamp(signal * m_gain, 0.0f, 1.0f);
            signal = std::abs(m_source->Evaluate(x, y));
            signal = m_offset - signal;
            signal *= signal;
            signal *= weight;
            result += signal * m_spectralWeights[static_cast<size_t>(octave)];
        }
        return result;
    }

private:
    std::unique_ptr<NoiseModule2D> m_source;
    float m_frequency = 1.0f;
    float m_lacunarity = 2.0f;
    int m_octaves = 1;
    float m_gain = 2.0f;
    float m_offset = 1.0f;
    std::array<float, 30> m_spectralWeights{};
};

class ScaleBiasNoise2D final : public NoiseModule2D
{
public:
    ScaleBiasNoise2D(std::unique_ptr<NoiseModule2D> source, float scale, float bias)
        : m_source(std::move(source))
        , m_scale(scale)
        , m_bias(bias)
    {
    }

    float Evaluate(float x, float y) const override
    {
        return m_source->Evaluate(x, y) * m_scale + m_bias;
    }

private:
    std::unique_ptr<NoiseModule2D> m_source;
    float m_scale = 1.0f;
    float m_bias = 0.0f;
};

class ScalePointNoise2D final : public NoiseModule2D
{
public:
    ScalePointNoise2D(std::unique_ptr<NoiseModule2D> source, float scaleX, float scaleY)
        : m_source(std::move(source))
        , m_scaleX(scaleX)
        , m_scaleY(scaleY)
    {
    }

    float Evaluate(float x, float y) const override
    {
        return m_source->Evaluate(x * m_scaleX, y * m_scaleY);
    }

private:
    std::unique_ptr<NoiseModule2D> m_source;
    float m_scaleX = 1.0f;
    float m_scaleY = 1.0f;
};

int MobWidthOffsetX(int widthIterator)
{
    return (widthIterator % 2 == 0) ? (-(widthIterator / 2))
                                    : (widthIterator / 2 + widthIterator % 2);
}

std::vector<GridCellPoint> BuildOrderedCellList(
    const std::unordered_set<GridCellPoint, GridCellPointHash> &cells);
std::vector<std::string_view> ResolveOrderedBiomeMobTags(const Site &site);

bool ContainsGridPoint(const Polygon &polygon, int x, int y)
{
    return polygon.Contains(static_cast<float>(x), static_cast<float>(y));
}

void AddFilledRectangleCells(std::unordered_set<GridCellPoint, GridCellPointHash> &cells,
                             const Vector2f &center,
                             float width,
                             float height,
                             KRandom &random,
                             std::vector<GridCellPoint> *orderedCells = nullptr)
{
    auto insertCell = [&](int x, int y) {
        const GridCellPoint point{x, y};
        if (cells.insert(point).second && orderedCells != nullptr) {
            orderedCells->push_back(point);
        }
    };

    if (width < 1.0f) {
        width = 1.0f;
    }
    if (height < 1.0f) {
        height = 1.0f;
    }

    float leftJitter = 0.0f;
    float rightJitter = 0.0f;
    const int minX = static_cast<int>(center.x - width / 2.0f);
    const int maxX = static_cast<int>(center.x + width / 2.0f);
    const int minY = static_cast<int>(center.y - height / 2.0f);
    const int maxY = static_cast<int>(center.y + height / 2.0f);

    for (int y = minY; y < maxY; ++y) {
        leftJitter =
            std::clamp(leftJitter + random.Next(-2.0f, 2.0f), -2.0f, 2.0f);
        rightJitter =
            std::clamp(rightJitter + random.Next(-2.0f, 2.0f), -2.0f, 2.0f);
        for (int x = static_cast<int>(static_cast<float>(minX) - leftJitter);
             static_cast<float>(x) < static_cast<float>(maxX) + rightJitter;
             ++x) {
            insertCell(x, y);
        }
    }

    float bottomJitter = 0.0f;
    float topJitter = 0.0f;
    for (int x = minX; x < maxX; ++x) {
        bottomJitter =
            std::clamp(bottomJitter + random.Next(-2.0f, 2.0f), -2.0f, 2.0f);
        topJitter =
            std::clamp(topJitter + random.Next(-2.0f, 2.0f), -2.0f, 2.0f);
        for (int y = static_cast<int>(static_cast<float>(minY) - bottomJitter);
             y < minY;
             ++y) {
            insertCell(x, y);
        }
        for (int y = maxY;
             static_cast<float>(y) < static_cast<float>(maxY) + topJitter;
             ++y) {
            insertCell(x, y);
        }
    }
}

std::unordered_set<GridCellPoint, GridCellPointHash> BuildBorderCells(
    const std::unordered_set<GridCellPoint, GridCellPointHash> &sourceCells,
    int radius)
{
    std::unordered_set<GridCellPoint, GridCellPointHash> borderCells;
    for (const auto &cell : sourceCells) {
        for (int x = cell.x - radius; x <= cell.x + radius; ++x) {
            for (int y = cell.y - radius; y <= cell.y + radius; ++y) {
                if (x == cell.x && y == cell.y) {
                    continue;
                }
                const GridCellPoint candidate{x, y};
                if (sourceCells.contains(candidate)) {
                    continue;
                }
                borderCells.insert(candidate);
            }
        }
    }
    return borderCells;
}

std::pair<std::unordered_set<GridCellPoint, GridCellPointHash>, std::vector<GridCellPoint>>
BuildBorderCellsOrdered(
    const std::vector<GridCellPoint> &sourceCells,
    const std::unordered_set<GridCellPoint, GridCellPointHash> &sourceCellSet,
    int radius)
{
    std::unordered_set<GridCellPoint, GridCellPointHash> borderCells;
    std::vector<GridCellPoint> orderedBorderCells;
    for (const auto &cell : sourceCells) {
        for (int x = cell.x - radius; x <= cell.x + radius; ++x) {
            for (int y = cell.y - radius; y <= cell.y + radius; ++y) {
                if (x == cell.x && y == cell.y) {
                    continue;
                }
                const GridCellPoint candidate{x, y};
                if (sourceCellSet.contains(candidate)) {
                    continue;
                }
                if (borderCells.insert(candidate).second) {
                    orderedBorderCells.push_back(candidate);
                }
            }
        }
    }
    return {std::move(borderCells), std::move(orderedBorderCells)};
}

std::unordered_set<GridCellPoint, GridCellPointHash> BuildTerrainCellPoints(const Site &site)
{
    std::unordered_set<GridCellPoint, GridCellPointHash> terrainCells;
    const Rect bounds = site.polygon.Bounds();
    const int minX = static_cast<int>(bounds.x);
    const int maxX = static_cast<int>(bounds.x + bounds.width);
    const int minY = static_cast<int>(bounds.y);
    const int maxY = static_cast<int>(bounds.y + bounds.height);
    for (int y = minY; y < maxY; ++y) {
        for (int x = minX; x < maxX; ++x) {
            if (!ContainsGridPoint(site.polygon, x, y)) {
                continue;
            }
            terrainCells.insert({x, y});
        }
    }
    return terrainCells;
}

std::vector<GridCellPoint> BuildTerrainCellPointList(const Site &site)
{
    std::vector<GridCellPoint> terrainCells;
    const Rect bounds = site.polygon.Bounds();
    const int minX = static_cast<int>(bounds.x);
    const int maxX = static_cast<int>(bounds.x + bounds.width);
    const int minY = static_cast<int>(bounds.y);
    const int maxY = static_cast<int>(bounds.y + bounds.height);
    for (int y = minY; y < maxY; ++y) {
        for (int x = minX; x < maxX; ++x) {
            if (!ContainsGridPoint(site.polygon, x, y)) {
                continue;
            }
            terrainCells.push_back({x, y});
        }
    }
    return terrainCells;
}

std::unordered_set<GridCellPoint, GridCellPointHash> BuildOilWellFeatureCells(
    const FeatureSettings &featureSettings,
    const Vector2f &center,
    KRandom &random)
{
    std::unordered_set<GridCellPoint, GridCellPointHash> featureCells;
    float finalSize = featureSettings.blobSize.GetRandomValue(random);
    if (finalSize < 1.0f) {
        return featureCells;
    }

    switch (featureSettings.shape) {
    case Shape::ShortWide:
        AddFilledRectangleCells(featureCells, center, finalSize, finalSize / 4.0f, random);
        break;
    case Shape::Square:
        AddFilledRectangleCells(featureCells, center, finalSize, finalSize, random);
        break;
    case Shape::TallThin:
        AddFilledRectangleCells(featureCells, center, finalSize / 4.0f, finalSize, random);
        break;
    default:
        AddFilledRectangleCells(featureCells, center, finalSize, finalSize / 4.0f, random);
        break;
    }

    for (const int borderWidth : featureSettings.borders) {
        if (borderWidth <= 0 || featureCells.empty()) {
            continue;
        }
        auto borderCells = BuildBorderCells(featureCells, borderWidth);
        featureCells.insert(borderCells.begin(), borderCells.end());
    }

    return featureCells;
}

FeatureRoomCells BuildFeatureRoomCells(const FeatureSettings &featureSettings,
                                       const Vector2f &center,
                                       KRandom &random)
{
    FeatureRoomCells roomCells;
    float finalSize = featureSettings.blobSize.GetRandomValue(random);
    if (finalSize < 1.0f) {
        return roomCells;
    }

    std::unordered_set<GridCellPoint, GridCellPointHash> centerCells;
    switch (featureSettings.shape) {
    case Shape::ShortWide:
        AddFilledRectangleCells(centerCells,
                                center,
                                finalSize,
                                finalSize / 4.0f,
                                random,
                                &roomCells.orderedCenterCells);
        break;
    case Shape::Square:
        AddFilledRectangleCells(centerCells,
                                center,
                                finalSize,
                                finalSize,
                                random,
                                &roomCells.orderedCenterCells);
        break;
    case Shape::TallThin:
        AddFilledRectangleCells(centerCells,
                                center,
                                finalSize / 4.0f,
                                finalSize,
                                random,
                                &roomCells.orderedCenterCells);
        break;
    default:
        AddFilledRectangleCells(centerCells,
                                center,
                                finalSize,
                                finalSize / 4.0f,
                                random,
                                &roomCells.orderedCenterCells);
        break;
    }

    roomCells.centerCells = centerCells;
    roomCells.centerCount = static_cast<int>(centerCells.size());
    roomCells.allCells = centerCells;
    std::vector<GridCellPoint> orderedAllCells = roomCells.orderedCenterCells;
    for (const int borderWidth : featureSettings.borders) {
        if (borderWidth <= 0 || roomCells.allCells.empty()) {
            break;
        }
        auto [borderCells, orderedBorderCells] =
            BuildBorderCellsOrdered(orderedAllCells, roomCells.allCells, borderWidth);
        roomCells.borderCounts.push_back(static_cast<int>(borderCells.size()));
        roomCells.orderedBorderCells.push_back(orderedBorderCells);
        roomCells.allCells.insert(borderCells.begin(), borderCells.end());
        orderedAllCells.insert(orderedAllCells.end(),
                               orderedBorderCells.begin(),
                               orderedBorderCells.end());
    }

    return roomCells;
}

std::unordered_set<GridCellPoint, GridCellPointHash> IntersectFeatureSpawnCells(
    const std::unordered_set<GridCellPoint, GridCellPointHash> &terrainCells,
    const std::unordered_set<GridCellPoint, GridCellPointHash> &featureCells)
{
    std::unordered_set<GridCellPoint, GridCellPointHash> result;
    result.reserve(std::min(terrainCells.size(), featureCells.size()));
    for (const auto &cell : featureCells) {
        if (terrainCells.contains(cell)) {
            result.insert(cell);
        }
    }
    return result;
}

void ConsumeWeightedChoices(const std::vector<WeightedSimHash> &choices,
                            int drawCount,
                            KRandom &random)
{
    if (choices.empty() || drawCount <= 0) {
        return;
    }
    std::vector<const WeightedSimHash *> weightedChoices;
    weightedChoices.reserve(choices.size());
    for (const auto &choice : choices) {
        weightedChoices.push_back(&choice);
    }
    for (int index = 0; index < drawCount; ++index) {
        (void)WeightedRandom_Choose(weightedChoices, random);
    }
}

void ConsumeElementChoiceGroup(const FeatureSettings &featureSettings,
                               std::string_view groupName,
                               int cellCount,
                               KRandom &random)
{
    if (cellCount <= 0) {
        return;
    }
    const auto itr = featureSettings.ElementChoiceGroups.find(std::string(groupName));
    if (itr == featureSettings.ElementChoiceGroups.end()) {
        return;
    }
    const auto &group = itr->second;
    switch (group.selectionMethod) {
    case Selection::Weighted:
    case Selection::WeightedResample:
        ConsumeWeightedChoices(group.choices, cellCount, random);
        break;
    case Selection::HorizontalSlice:
        break;
    default:
        ConsumeWeightedChoices(group.choices, 1, random);
        break;
    }
}

const FeatureSettings *SelectForegroundFeatureForSite(const Site &site,
                                                      const SettingsCache &settings,
                                                      KRandom *random)
{
    std::vector<const FeatureSettings *> candidates;
    candidates.reserve(site.tags.size());
    for (const auto &tag : site.tags) {
        const auto itr = settings.features.find(tag);
        if (itr != settings.features.end()) {
            candidates.push_back(&itr->second);
        }
    }
    if (candidates.empty()) {
        return nullptr;
    }
    if (random == nullptr) {
        return candidates.front();
    }
    return candidates[static_cast<size_t>(random->Next(static_cast<int>(candidates.size())))];
}

struct ActionSampleView {
    PointSelectionMethod selectMethod = PointSelectionMethod::RandomPoints;
    MinMax density;
    SampleBehaviour sampleBehaviour = SampleBehaviour::UniformSquare;
    bool isRoom = false;
    Selection roomMobSelection = Selection::None;
};

std::optional<ActionSampleView> FindActionSampleForTag(const std::string &tag,
                                                       const SettingsCache &settings)
{
    const auto roomItr = settings.rooms.add.find(tag);
    if (roomItr != settings.rooms.add.end()) {
        return ActionSampleView{
            .selectMethod = roomItr->second.selectMethod,
            .density = roomItr->second.density,
            .sampleBehaviour = roomItr->second.sampleBehaviour,
            .isRoom = true,
            .roomMobSelection = roomItr->second.mobselection,
        };
    }

    const auto mobItr = settings.mobs.MobLookupTable.add.find(tag);
    if (mobItr != settings.mobs.MobLookupTable.add.end()) {
        return ActionSampleView{
            .selectMethod = mobItr->second.selectMethod,
            .density = mobItr->second.density,
            .sampleBehaviour = mobItr->second.sampleBehaviour,
            .isRoom = false,
            .roomMobSelection = Selection::None,
        };
    }

    return std::nullopt;
}

std::unordered_set<int64_t> ConvertGridCellsToEncodedSet(
    const std::unordered_set<GridCellPoint, GridCellPointHash> &cells)
{
    std::unordered_set<int64_t> encoded;
    encoded.reserve(cells.size());
    for (const auto &cell : cells) {
        encoded.insert(EncodeCell(cell.x, cell.y));
    }
    return encoded;
}

std::unordered_set<GridCellPoint, GridCellPointHash> ConvertEncodedSetToGridCells(
    const std::unordered_set<int64_t> &cells)
{
    std::unordered_set<GridCellPoint, GridCellPointHash> decoded;
    decoded.reserve(cells.size());
    for (const int64_t key : cells) {
        decoded.insert({static_cast<int>(key >> 32), static_cast<int>(key)});
    }
    return decoded;
}

ImprovedPerlinNoise2D::Quality ParseNoiseQuality(std::string_view quality)
{
    if (quality == "Fast") {
        return ImprovedPerlinNoise2D::Quality::Fast;
    }
    if (quality == "Standard") {
        return ImprovedPerlinNoise2D::Quality::Standard;
    }
    return ImprovedPerlinNoise2D::Quality::Best;
}

std::unique_ptr<NoiseModule2D> BuildNoiseModule2D(
    const NoiseTree &tree,
    int globalNoiseSeed)
{
    struct LinkView {
        std::string type;
        const NoiseLink *link = nullptr;
    };

    std::map<std::pair<std::string, std::string>, const NoiseLink *> linkByTarget;
    for (const auto &link : tree.links) {
        linkByTarget[{link.target.type, link.target.name}] = &link;
    }

    std::function<std::unique_ptr<NoiseModule2D>(const NoiseNodeRef &)> buildRef;
    buildRef = [&](const NoiseNodeRef &node) -> std::unique_ptr<NoiseModule2D> {
        if (node.type == "Primitive") {
            const auto primitiveItr = tree.primitives.find(node.name);
            if (primitiveItr == tree.primitives.end()) {
                return nullptr;
            }
            const auto &primitive = primitiveItr->second;
            const auto quality = ParseNoiseQuality(primitive.quality);
            if (primitive.primative == "ImprovedPerlin") {
                return std::make_unique<ImprovedPerlinNoise2D>(
                    globalNoiseSeed + primitive.seed, quality);
            }
            if (primitive.primative == "SimplexPerlin") {
                return std::make_unique<SimplexPerlinNoise2D>(
                    globalNoiseSeed + primitive.seed, quality);
            }
            return nullptr;
        }

        const auto linkItr = linkByTarget.find({node.type, node.name});
        if (linkItr == linkByTarget.end() || linkItr->second == nullptr ||
            !linkItr->second->source0.has_value()) {
            return nullptr;
        }
        auto source = buildRef(*linkItr->second->source0);
        if (!source) {
            return nullptr;
        }

        if (node.type == "Filter") {
            const auto filterItr = tree.filters.find(node.name);
            if (filterItr == tree.filters.end()) {
                return nullptr;
            }
            const auto &filter = filterItr->second;
            if (filter.filter == "RidgedMultiFractal") {
                return std::make_unique<RidgedMultiFractalNoise2D>(
                    std::move(source),
                    filter.frequency,
                    filter.lacunarity,
                    filter.octaves,
                    filter.gain,
                    filter.offset,
                    filter.exponent);
            }
            return nullptr;
        }

        if (node.type == "Modifier") {
            const auto modifierItr = tree.modifiers.find(node.name);
            if (modifierItr == tree.modifiers.end()) {
                return nullptr;
            }
            const auto &modifier = modifierItr->second;
            if (modifier.modifyType == "ScaleBias") {
                return std::make_unique<ScaleBiasNoise2D>(
                    std::move(source), modifier.scale, modifier.bias);
            }
            if (modifier.modifyType == "Scale2d") {
                return std::make_unique<ScalePointNoise2D>(
                    std::move(source), modifier.scale2d.x, modifier.scale2d.y);
            }
            return nullptr;
        }

        return nullptr;
    };

    for (const auto &link : tree.links) {
        if (link.target.type != "Terminator" || !link.source0.has_value()) {
            continue;
        }
        return buildRef(*link.source0);
    }
    return nullptr;
}

int WorldCellIndex(const World &world, int x, int y)
{
    return x + static_cast<int>(world.worldsize.x) * y;
}

bool IsWorldCellValid(const World &world, int x, int y)
{
    return x >= 0 && y >= 0 &&
           x < static_cast<int>(world.worldsize.x) &&
           y < static_cast<int>(world.worldsize.y);
}

bool IsElementExplicitlyNonSolid(std::string_view element)
{
    static const std::unordered_set<std::string_view> kNonSolidElements = {
        "Vacuum",          "Void",             "CarbonDioxide",   "ChlorineGas",
        "ContaminatedOxygen",                  "Hydrogen",        "Methane",
        "Oxygen",          "Propane",          "SourGas",         "Steam",
        "Syngas",          "Fallout",          "Brine",           "Chlorine",
        "CrudeOil",        "DirtyWater",       "Ethanol",         "LiquidGunk",
        "Magma",           "Mercury",          "Milk",            "MoltenAluminum",
        "MoltenCarbon",    "MoltenCobalt",     "MoltenCopper",    "MoltenGlass",
        "MoltenGold",      "MoltenIron",       "MoltenIridium",   "MoltenLead",
        "MoltenNickel",    "MoltenNiobium",    "MoltenSalt",      "MoltenSteel",
        "MoltenSucrose",   "MoltenSyngas",     "MoltenTungsten",  "MoltenUranium",
        "MurkyBrine",      "Naphtha",          "NaturalResin",    "NuclearWaste",
        "Petroleum",       "PhytoOil",         "RefinedLipid",    "Resin",
        "SaltWater",       "SugarWater",       "SuperCoolant",    "ToxicMud",
        "ViscoGel",        "Water",            "Mucus",           "Gunk",
    };
    if (kNonSolidElements.contains(element)) {
        return true;
    }
    return element.starts_with("Liquid") || element.starts_with("Molten") ||
           element.ends_with("Gas");
}

bool IsElementExplicitlyGasLike(std::string_view element)
{
    static const std::unordered_set<std::string_view> kGasLikeElements = {
        "Vacuum",          "Void",             "CarbonDioxide", "ChlorineGas",
        "ContaminatedOxygen",                  "Hydrogen",      "Methane",
        "Oxygen",          "Propane",          "SourGas",       "Steam",
        "Syngas",          "Fallout",
    };
    return kGasLikeElements.contains(element) || element.ends_with("Gas");
}

bool IsElementSolidForWorldgen(std::string_view element)
{
    return !IsElementExplicitlyNonSolid(element);
}

bool IsElementLiquidForWorldgen(std::string_view element)
{
    if (IsElementSolidForWorldgen(element) || IsElementExplicitlyGasLike(element)) {
        return false;
    }

    static const std::unordered_set<std::string_view> kLiquidElements = {
        "Brine",        "Chlorine",     "CrudeOil",     "DirtyWater",
        "Ethanol",      "LiquidGunk",   "Magma",        "Mercury",
        "Milk",         "MurkyBrine",   "Naphtha",      "NaturalResin",
        "NuclearWaste", "Petroleum",    "PhytoOil",     "RefinedLipid",
        "Resin",        "SaltWater",    "SugarWater",   "SuperCoolant",
        "ToxicMud",     "ViscoGel",     "Water",        "Mucus",
        "Gunk",
    };
    if (kLiquidElements.contains(element)) {
        return true;
    }
    return element.starts_with("Liquid") || element.starts_with("Molten");
}

WorldgenCellMaterial ClassifyElementForWorldgen(std::string_view element)
{
    if (IsElementSolidForWorldgen(element)) {
        return WorldgenCellMaterial::Solid;
    }
    if (IsElementLiquidForWorldgen(element)) {
        return WorldgenCellMaterial::Liquid;
    }
    return WorldgenCellMaterial::Gas;
}

std::vector<ElementGradient> BuildElementBandWithMaxValues(
    const std::vector<ElementGradient> &source)
{
    std::vector<ElementGradient> result = source;
    float cumulative = 0.0f;
    for (auto &gradient : result) {
        if (gradient.maxValue > 0.0f) {
            cumulative = gradient.maxValue;
            continue;
        }
        cumulative += gradient.bandSize;
        gradient.maxValue = cumulative;
    }
    return result;
}

const std::vector<ElementGradient> *SelectBackgroundBiomeForSite(const Site &site,
                                                                 const SettingsCache &settings,
                                                                 KRandom &random)
{
    std::vector<const std::vector<ElementGradient> *> biomeCandidates;
    biomeCandidates.reserve(site.tags.size());
    for (const auto &tag : site.tags) {
        const auto itr = settings.biomes.find(tag);
        if (itr != settings.biomes.end()) {
            biomeCandidates.push_back(&itr->second);
        }
    }
    if (biomeCandidates.empty()) {
        return nullptr;
    }
    if (biomeCandidates.size() == 1) {
        return biomeCandidates.front();
    }
    return biomeCandidates[static_cast<size_t>(random.Next(static_cast<int>(biomeCandidates.size())))];
}

WorldgenCellMaterial ChooseGroupElementMaterial(const ElementChoiceGroup &group,
                                                KRandom &random)
{
    if (group.choices.empty()) {
        return WorldgenCellMaterial::Solid;
    }
    std::vector<const WeightedSimHash *> weightedChoices;
    weightedChoices.reserve(group.choices.size());
    for (const auto &choice : group.choices) {
        weightedChoices.push_back(&choice);
    }
    const WeightedSimHash *choice = WeightedRandom_Choose(weightedChoices, random);
    if (choice == nullptr) {
        return WorldgenCellMaterial::Solid;
    }
    return ClassifyElementForWorldgen(choice->element);
}

bool TerrainSitesContainPoint(const std::vector<const Site *> &sites, float x, float y)
{
    for (const Site *site : sites) {
        if (site != nullptr && site->polygon.Contains(x, y)) {
            return true;
        }
    }
    return false;
}

bool IsFeaturePointContainedInBorderForWorldgen(const Site &currentSite,
                                                const std::vector<const Site *> &terrainSites,
                                                int x,
                                                int y)
{
    if (!currentSite.tags.contains("AllowExceedNodeBorders")) {
        return true;
    }
    if (currentSite.polygon.Contains(static_cast<float>(x), static_cast<float>(y))) {
        return true;
    }

    for (const Site *site : terrainSites) {
        if (site == nullptr || !site->polygon.Contains(static_cast<float>(x),
                                                       static_cast<float>(y))) {
            continue;
        }
        if (currentSite.subworld != nullptr && site->subworld != nullptr &&
            currentSite.subworld->zoneType != site->subworld->zoneType) {
            return false;
        }
        break;
    }
    return true;
}

void ApplyChoiceGroupToSolidCells(const FeatureSettings &featureSettings,
                                  const Site &currentSite,
                                  const std::vector<const Site *> &terrainSites,
                                  std::string_view groupName,
                                  const std::vector<GridCellPoint> &orderedCells,
                                  KRandom &random,
                                  std::vector<uint8_t> &solidCells,
                                  std::vector<uint8_t> &liquidCells,
                                  const World &world)
{
    if (orderedCells.empty()) {
        return;
    }
    const auto itr = featureSettings.ElementChoiceGroups.find(std::string(groupName));
    if (itr == featureSettings.ElementChoiceGroups.end()) {
        return;
    }
    const auto &group = itr->second;
    if (group.selectionMethod == Selection::Weighted ||
        group.selectionMethod == Selection::WeightedResample) {
        for (const auto &cell : orderedCells) {
            if (!IsWorldCellValid(world, cell.x, cell.y) ||
                !IsFeaturePointContainedInBorderForWorldgen(
                    currentSite, terrainSites, cell.x, cell.y)) {
                continue;
            }
            const auto material = ChooseGroupElementMaterial(group, random);
            const size_t index =
                static_cast<size_t>(WorldCellIndex(world, cell.x, cell.y));
            solidCells[index] = material == WorldgenCellMaterial::Solid ? 1 : 0;
            liquidCells[index] = material == WorldgenCellMaterial::Liquid ? 1 : 0;
        }
        return;
    }

    const auto material = ChooseGroupElementMaterial(group, random);
    for (const auto &cell : orderedCells) {
        if (!IsWorldCellValid(world, cell.x, cell.y) ||
            !IsFeaturePointContainedInBorderForWorldgen(
                currentSite, terrainSites, cell.x, cell.y)) {
            continue;
        }
        const size_t index = static_cast<size_t>(WorldCellIndex(world, cell.x, cell.y));
        solidCells[index] = material == WorldgenCellMaterial::Solid ? 1 : 0;
        liquidCells[index] = material == WorldgenCellMaterial::Liquid ? 1 : 0;
    }
}

void AddClaimedRectCells(const Rect &rect,
                         const World &world,
                         std::unordered_set<int64_t> &claimedCells)
{
    const int width = static_cast<int>(world.worldsize.x);
    const int height = static_cast<int>(world.worldsize.y);
    const int minX = std::max(0, static_cast<int>(std::floor(rect.x)));
    const int maxX = std::min(width, static_cast<int>(std::ceil(rect.x + rect.width)));
    const int minY = std::max(0, static_cast<int>(std::floor(rect.y)));
    const int maxY = std::min(height, static_cast<int>(std::ceil(rect.y + rect.height)));
    for (int y = minY; y < maxY; ++y) {
        for (int x = minX; x < maxX; ++x) {
            claimedCells.insert(EncodeCell(x, y));
        }
    }
}

void AddWorldBorderClaimedCells(const std::vector<Site> &generatedSites,
                                const World &world,
                                const SettingsCache &settings,
                                std::unordered_set<int64_t> &claimedCells)
{
    if (!settings.GetDefaultData<bool>(world, "DrawWorldBorder")) {
        return;
    }

    const int thickness =
        static_cast<int>(settings.GetDefaultData<float>(world, "WorldBorderThickness"));
    const float range = settings.GetDefaultData<float>(world, "WorldBorderRange");
    const float mapWidth = world.worldsize.x;
    const float mapHeight = world.worldsize.y;
    float delta1 = 0.0f;
    float delta2 = 0.0f;
    float border1 = 0.0f;
    float border2 = mapWidth - 1.0f;
    KRandom random(0);

    std::vector<const Site *> atLeft;
    std::vector<const Site *> atRight;
    std::vector<const Site *> atSurface;
    std::vector<const Site *> atDepths;
    for (const auto &site : generatedSites) {
        if (!site.tags.contains("RemoveWorldBorderOverVacuum")) {
            continue;
        }
        if (site.tags.contains("AtLeft")) {
            atLeft.push_back(&site);
        }
        if (site.tags.contains("AtRight")) {
            atRight.push_back(&site);
        }
        if (site.tags.contains("AtSurface")) {
            atSurface.push_back(&site);
        }
        if (site.tags.contains("AtDepths")) {
            atDepths.push_back(&site);
        }
    }

    for (int y = static_cast<int>(mapHeight) - 1; y >= 0; --y) {
        delta1 = std::max(-range, std::min(delta1 + random.Next(-2.0f, 2.0f), range));
        if (!TerrainSitesContainPoint(atLeft, 1.0f, static_cast<float>(y))) {
            border1 = std::max(border1, thickness + delta1 - 1.0f);
        }
        delta2 = std::max(-range, std::min(delta2 + random.Next(-2.0f, 2.0f), range));
        if (!TerrainSitesContainPoint(atRight, mapWidth - 1.0f, static_cast<float>(y))) {
            border2 = std::min(border2, mapWidth - thickness - delta2);
        }
    }
    AddClaimedRectCells({0.0f, 0.0f, border1 + 1.0f, mapHeight}, world, claimedCells);
    AddClaimedRectCells({border2, 0.0f, mapWidth - border2, mapHeight}, world, claimedCells);

    delta1 = 0.0f;
    delta2 = 0.0f;
    border1 = 0.0f;
    border2 = mapHeight - 1.0f;
    for (int x = 0; x < static_cast<int>(mapWidth); ++x) {
        delta1 = std::max(-range, std::min(delta1 + random.Next(-2.0f, 2.0f), range));
        if (!TerrainSitesContainPoint(atDepths, static_cast<float>(x), 1.0f)) {
            border1 = std::max(border1, thickness + delta1 - 1.0f);
        }
        delta2 = std::max(-range, std::min(delta2 + random.Next(-2.0f, 2.0f), range));
        if (!TerrainSitesContainPoint(atSurface, static_cast<float>(x), mapHeight - 1.0f)) {
            border2 = std::min(border2, mapHeight - thickness - delta2);
        }
    }
    AddClaimedRectCells({0.0f, 0.0f, mapWidth, border1 + 1.0f}, world, claimedCells);
    AddClaimedRectCells({0.0f, border2, mapWidth, mapHeight - border2}, world, claimedCells);
}

void DetectNaturalCavityCells(const World &world,
                              const std::vector<uint8_t> &solidCells,
                              std::unordered_set<int64_t> &cavityCells)
{
    const int width = static_cast<int>(world.worldsize.x);
    const int height = static_cast<int>(world.worldsize.y);
    std::vector<uint8_t> visited(static_cast<size_t>(width * height), 0);
    std::queue<int> pending;
    std::vector<int> component;
    component.reserve(320);

    constexpr std::array<GridCellPoint, 4> kNeighbors = {
        GridCellPoint{1, 0},
        GridCellPoint{-1, 0},
        GridCellPoint{0, 1},
        GridCellPoint{0, -1},
    };

    for (int cellIndex = 0; cellIndex < width * height; ++cellIndex) {
        if (visited[static_cast<size_t>(cellIndex)] != 0 ||
            solidCells[static_cast<size_t>(cellIndex)] != 0) {
            continue;
        }
        visited[static_cast<size_t>(cellIndex)] = 1;
        pending.push(cellIndex);
        component.clear();
        while (!pending.empty()) {
            const int current = pending.front();
            pending.pop();
            component.push_back(current);
            const int x = current % width;
            const int y = current / width;
            for (const auto &neighbor : kNeighbors) {
                const int nextX = x + neighbor.x;
                const int nextY = y + neighbor.y;
                if (nextX < 0 || nextY < 0 || nextX >= width || nextY >= height) {
                    continue;
                }
                const int nextIndex = nextX + nextY * width;
                if (visited[static_cast<size_t>(nextIndex)] != 0 ||
                    solidCells[static_cast<size_t>(nextIndex)] != 0) {
                    continue;
                }
                visited[static_cast<size_t>(nextIndex)] = 1;
                pending.push(nextIndex);
            }
        }
        if (component.empty() || component.size() > 300) {
            continue;
        }
        for (const int openIndex : component) {
            const int x = openIndex % width;
            const int y = openIndex / width;
            cavityCells.insert(EncodeCell(x, y));
        }
    }
}

void ConsumeGenerateActionCells(const Site &site,
                                const SettingsCache &settings,
                                const std::unordered_set<int64_t> &possiblePoints,
                                KRandom &random,
                                std::map<std::string, int> &terrainPositionCounts)
{
    const auto biomeMobTags = ResolveOrderedBiomeMobTags(site);
    for (const auto &tag : site.tags) {
        if (std::ranges::find(biomeMobTags, std::string_view(tag)) != biomeMobTags.end()) {
            continue;
        }
        const auto sample = FindActionSampleForTag(tag, settings);
        if (!sample.has_value()) {
            continue;
        }

        const float density = sample->density.GetRandomValue(random);
        std::vector<Vector2f> points;
        if (sample->selectMethod == PointSelectionMethod::RandomPoints) {
            points = GetRandomPoints(site.polygon,
                                     density,
                                     0.0f,
                                     {},
                                     sample->sampleBehaviour,
                                     true,
                                     random);
        } else {
            points.push_back({site.x, site.y});
        }

        if (!sample->isRoom || sample->roomMobSelection != Selection::None) {
            continue;
        }

        std::unordered_set<int64_t> pickedPoints;
        for (const auto &point : points) {
            const int x = static_cast<int>(point.x);
            const int y = static_cast<int>(point.y);
            const int64_t cell = EncodeCell(x, y);
            if (possiblePoints.contains(cell)) {
                pickedPoints.insert(cell);
            }
        }
        if (!pickedPoints.empty()) {
            terrainPositionCounts[tag] += static_cast<int>(pickedPoints.size());
        }
    }
}

void ConsumeSprinkleRandom(const std::string &tag,
                           int terrainPositionCount,
                           const SettingsCache &settings,
                           KRandom &random)
{
    if (terrainPositionCount <= 0) {
        return;
    }
    const auto featureItr = settings.features.find(tag);
    const auto roomItr = settings.rooms.add.find(tag);
    if (featureItr == settings.features.end() || roomItr == settings.rooms.add.end()) {
        return;
    }

    ConsumeElementChoiceGroup(
        featureItr->second, "SprinkleOfElementChoices", 1, random);
    for (int index = 0; index < terrainPositionCount; ++index) {
        (void)roomItr->second.blobSize.GetRandomValue(random);
    }
}

void ConsumeApplyBackgroundRandom(const Site &,
                                  const SettingsCache &settings,
                                  KRandom &random,
                                  const std::map<std::string, int> &terrainPositionCounts)
{
    if (const auto itr = terrainPositionCounts.find("SprinkleOfOxyRock");
        itr != terrainPositionCounts.end()) {
        ConsumeSprinkleRandom("SprinkleOfOxyRock", itr->second, settings, random);
    }
    if (const auto itr = terrainPositionCounts.find("SprinkleOfMetal");
        itr != terrainPositionCounts.end()) {
        ConsumeSprinkleRandom("SprinkleOfMetal", itr->second, settings, random);
    }
}

SimulatedTerrainState SimulateTerrainStateForOilWell(const std::vector<Site> &generatedSites,
                                                     const SettingsCache &settings,
                                                     const World &world,
                                                     int terrainSeed)
{
    SimulatedTerrainState result;
    result.solidCells.assign(
        static_cast<size_t>(world.worldsize.x * world.worldsize.y), 1);
    result.liquidCells.assign(
        static_cast<size_t>(world.worldsize.x * world.worldsize.y), 0);

    std::vector<const Site *> leafSites;
    struct ParentSiteView {
        const Site *parent = nullptr;
        std::string noiseKey = "noise/Default";
        const NoiseTree *noiseTree = nullptr;
        std::unique_ptr<NoiseModule2D> module;
        std::vector<int> cellIndices;
        float minValue = std::numeric_limits<float>::max();
        float maxValue = std::numeric_limits<float>::lowest();
    };
    std::vector<ParentSiteView> parents;
    parents.reserve(generatedSites.size());

    for (const auto &parent : generatedSites) {
        ParentSiteView parentView;
        parentView.parent = &parent;
        if (parent.children) {
            for (const auto &child : *parent.children) {
                leafSites.push_back(&child);
            }
        }

        const std::string_view requestedNoise =
            (parent.subworld != nullptr && !parent.subworld->biomeNoise.empty())
                ? std::string_view(parent.subworld->biomeNoise)
                : std::string_view("noise/Default");
        parentView.noiseTree = settings.FindNoise(
            requestedNoise, parent.subworld != nullptr ? parent.subworld->name : "");
        if (parentView.noiseTree == nullptr) {
            parentView.noiseTree = settings.FindNoise("noise/Default");
            parentView.noiseKey = "noise/Default";
        } else {
            parentView.noiseKey = std::string(requestedNoise);
        }
        if (parentView.noiseTree != nullptr) {
            parentView.module = BuildNoiseModule2D(*parentView.noiseTree, terrainSeed);
        }

        if (parentView.module != nullptr) {
            auto parentCells = BuildTerrainCellPointList(parent);
            parentView.cellIndices.reserve(parentCells.size());
            for (const auto &cell : parentCells) {
                if (!IsWorldCellValid(world, cell.x, cell.y)) {
                    continue;
                }
                const int cellIndex = WorldCellIndex(world, cell.x, cell.y);
                const float sample = parentView.module->Evaluate(
                    static_cast<float>(cell.x) * parentView.noiseTree->settings.zoom,
                    static_cast<float>(cell.y) * parentView.noiseTree->settings.zoom);
                result.solidCells[static_cast<size_t>(cellIndex)] = 0;
                parentView.cellIndices.push_back(cellIndex);
                parentView.minValue = std::min(parentView.minValue, sample);
                parentView.maxValue = std::max(parentView.maxValue, sample);
                if (!std::isfinite(sample)) {
                    continue;
                }
            }
        }
        parents.push_back(std::move(parentView));
    }
    std::ranges::sort(leafSites, [](const Site *lhs, const Site *rhs) {
        return lhs->idx < rhs->idx;
    });

    std::map<std::string, std::pair<float, float>> noiseMinMax;
    std::map<std::string, std::vector<int>> noiseCells;
    std::vector<float> normalizedNoise(
        static_cast<size_t>(world.worldsize.x * world.worldsize.y), 0.0f);
    for (const auto &parent : parents) {
        if (parent.module == nullptr || parent.cellIndices.empty()) {
            continue;
        }
        auto &stats = noiseMinMax[parent.noiseKey];
        if (noiseCells[parent.noiseKey].empty()) {
            stats.first = parent.minValue;
            stats.second = parent.maxValue;
        } else {
            stats.first = std::min(stats.first, parent.minValue);
            stats.second = std::max(stats.second, parent.maxValue);
        }
        auto &cells = noiseCells[parent.noiseKey];
        cells.insert(cells.end(), parent.cellIndices.begin(), parent.cellIndices.end());
        for (const int cellIndex : parent.cellIndices) {
            const int x = cellIndex % static_cast<int>(world.worldsize.x);
            const int y = cellIndex / static_cast<int>(world.worldsize.x);
            normalizedNoise[static_cast<size_t>(cellIndex)] = parent.module->Evaluate(
                static_cast<float>(x) * parent.noiseTree->settings.zoom,
                static_cast<float>(y) * parent.noiseTree->settings.zoom);
        }
    }
    for (auto &[noiseKey, cells] : noiseCells) {
        const auto statsItr = noiseMinMax.find(noiseKey);
        if (statsItr == noiseMinMax.end()) {
            continue;
        }
        const float minValue = statsItr->second.first;
        const float maxValue = statsItr->second.second;
        const float range = maxValue - minValue;
        if (range <= 0.0f) {
            for (const int cellIndex : cells) {
                normalizedNoise[static_cast<size_t>(cellIndex)] = 0.0f;
            }
            continue;
        }
        for (const int cellIndex : cells) {
            normalizedNoise[static_cast<size_t>(cellIndex)] =
                (normalizedNoise[static_cast<size_t>(cellIndex)] - minValue) / range;
        }
    }

    KRandom terrainRandom(terrainSeed);
    for (const Site *site : leafSites) {
        auto availableTerrainPoints = ConvertGridCellsToEncodedSet(BuildTerrainCellPoints(*site));
        std::map<std::string, int> terrainPositionCounts;

        if (const FeatureSettings *feature =
                SelectForegroundFeatureForSite(*site, settings, &terrainRandom);
            feature != nullptr) {
            FeatureRoomCells roomCells =
                BuildFeatureRoomCells(*feature, site->polygon.Centroid(), terrainRandom);
            auto encodedFeatureCells =
                ConvertGridCellsToEncodedSet(roomCells.allCells);
            for (const int64_t cell : encodedFeatureCells) {
                availableTerrainPoints.erase(cell);
            }

            ApplyChoiceGroupToSolidCells(*feature,
                                         *site,
                                         leafSites,
                                         "RoomCenterElements",
                                         roomCells.orderedCenterCells,
                                         terrainRandom,
                                         result.solidCells,
                                         result.liquidCells,
                                         world);
            for (size_t borderIndex = 0; borderIndex < roomCells.borderCounts.size();
                 ++borderIndex) {
                const std::string groupName =
                    "RoomBorderChoices" + std::to_string(borderIndex);
                ApplyChoiceGroupToSolidCells(*feature,
                                             *site,
                                             leafSites,
                                             groupName,
                                             roomCells.orderedBorderCells[borderIndex],
                                             terrainRandom,
                                             result.solidCells,
                                             result.liquidCells,
                                             world);
            }
            auto featureSpawnCells =
                IntersectFeatureSpawnCells(BuildTerrainCellPoints(*site), roomCells.allCells);
            FeatureAmbientLayout layout;
            layout.feature = feature;
            layout.spawnCells = std::move(featureSpawnCells);
            result.featureCellsBySite.emplace(site->idx, std::move(layout));
        }

        ConsumeGenerateActionCells(
            *site, settings, availableTerrainPoints, terrainRandom, terrainPositionCounts);
        const auto biome = SelectBackgroundBiomeForSite(*site, settings, terrainRandom);
        if (biome != nullptr) {
            auto gradients = BuildElementBandWithMaxValues(*biome);
            for (const int64_t encodedCell : availableTerrainPoints) {
                const int x = static_cast<int>(encodedCell >> 32);
                const int y = static_cast<int>(encodedCell);
                if (!IsWorldCellValid(world, x, y)) {
                    continue;
                }
                const int cellIndex = WorldCellIndex(world, x, y);
                const float value = normalizedNoise[static_cast<size_t>(cellIndex)];
                bool solid = true;
                bool liquid = false;
                for (const auto &gradient : gradients) {
                    if (value < gradient.maxValue) {
                        const auto material = ClassifyElementForWorldgen(gradient.content);
                        solid = material == WorldgenCellMaterial::Solid;
                        liquid = material == WorldgenCellMaterial::Liquid;
                        break;
                    }
                }
                result.solidCells[static_cast<size_t>(cellIndex)] = solid ? 1 : 0;
                result.liquidCells[static_cast<size_t>(cellIndex)] = liquid ? 1 : 0;
            }
        }
        ConsumeApplyBackgroundRandom(*site, settings, terrainRandom, terrainPositionCounts);
    }

    DetectNaturalCavityCells(world, result.solidCells, result.cavityCells);
    return result;
}

void AddOccupiedMobCells(const Mob &mob,
                         int anchorX,
                         int anchorY,
                         std::unordered_set<int64_t> &occupiedCells)
{
    for (int ix = 0; ix < mob.width + mob.paddingX * 2; ++ix) {
        const int x = anchorX + MobWidthOffsetX(ix);
        for (int iy = 0; iy < mob.height; ++iy) {
            occupiedCells.insert(EncodeCell(x, anchorY + iy));
        }
    }
}

void AddTemplateClaimedCells(const TemplateSpawner &spawner,
                             std::unordered_set<int64_t> &claimedCells)
{
    const Vector2<int> templatePos{
        static_cast<int>(spawner.position.x),
        static_cast<int>(spawner.position.y),
    };
    for (const auto &cell : spawner.container->cells) {
        claimedCells.insert(
            EncodeCell(templatePos.x + cell.location_x, templatePos.y + cell.location_y));
    }
}

std::vector<std::string_view> ResolveOrderedBiomeMobTags(const Site &site)
{
    std::vector<std::string_view> tags;
    if (site.subworld == nullptr) {
        return tags;
    }
    for (const auto &biome : site.subworld->biomes) {
        if (!site.tags.contains(biome.name)) {
            continue;
        }
        tags.reserve(biome.tags.size());
        for (const auto &tag : biome.tags) {
            tags.push_back(tag);
        }
        break;
    }
    return tags;
}

bool IsAmbientMobPlacementBoundsValid(const World &world,
                                      const Mob &mob,
                                      int anchorX,
                                      int anchorY)
{
    const int direction =
        (mob.location != Location::Ceiling && mob.location != Location::LiquidCeiling) ? 1 : -1;
    const int paddedWidth = mob.width + mob.paddingX * 2;
    const int widthOffset = paddedWidth / 2 - mob.width - mob.paddingX + 1;
    const int minOffsetX = widthOffset - 1;
    const int minOffsetY = direction < 0 ? 1 : mob.height;
    const int maxOffsetX = minOffsetX + mob.width + 1;
    const int maxOffsetY = minOffsetY - (mob.height + 1);
    return IsWorldCellValid(world, anchorX + minOffsetX, anchorY + minOffsetY) &&
           IsWorldCellValid(world, anchorX + maxOffsetX, anchorY + maxOffsetY);
}

bool IsAmbientMobLocationSupported(Location location)
{
    return location == Location::Solid || location == Location::Floor ||
           location == Location::AnyFloor;
}

bool CanPlaceAmbientMobAtCell(const World &world,
                              const Mob &mob,
                              int anchorX,
                              int anchorY,
                              const std::unordered_set<int64_t> &cavityCells,
                              const std::vector<uint8_t> &solidCells,
                              const std::vector<uint8_t> &liquidCells,
                              const std::unordered_set<int64_t> *claimedCells,
                              const std::unordered_set<int64_t> &occupiedCells)
{
    if (!IsAmbientMobLocationSupported(mob.location) ||
        !IsAmbientMobPlacementBoundsValid(world, mob, anchorX, anchorY)) {
        return false;
    }
    if (claimedCells != nullptr && claimedCells->contains(EncodeCell(anchorX, anchorY))) {
        return false;
    }

    const int direction =
        (mob.location != Location::Ceiling && mob.location != Location::LiquidCeiling) ? 1 : -1;
    const int paddedWidth = mob.width + mob.paddingX * 2;
    for (int ix = 0; ix < paddedWidth; ++ix) {
        const int x = anchorX + MobWidthOffsetX(ix);
        for (int iy = 0; iy < mob.height; ++iy) {
            const int y = anchorY + iy * direction;
            if (!IsWorldCellValid(world, x, y) ||
                occupiedCells.contains(EncodeCell(x, y))) {
                return false;
            }
        }
    }

    if (mob.location == Location::Solid) {
        for (int ix = 0; ix < mob.width; ++ix) {
            const int x = anchorX + MobWidthOffsetX(ix);
            for (int iy = 0; iy < mob.height; ++iy) {
                const int y = anchorY + iy * direction;
                const size_t index = static_cast<size_t>(WorldCellIndex(world, x, y));
                if (cavityCells.contains(EncodeCell(x, y)) || solidCells[index] == 0) {
                    return false;
                }
            }
        }
        return true;
    }

    if (mob.location == Location::Floor) {
        for (int iy = 0; iy < mob.height; ++iy) {
            for (int ix = 0; ix < paddedWidth; ++ix) {
                const int x = anchorX + MobWidthOffsetX(ix);
                const int y = anchorY + iy;
                const size_t index = static_cast<size_t>(WorldCellIndex(world, x, y));
                if (!cavityCells.contains(EncodeCell(x, y)) || solidCells[index] != 0 ||
                    liquidCells[index] != 0) {
                    return false;
                }
                if (iy == 0 && ix < mob.width &&
                    solidCells[static_cast<size_t>(WorldCellIndex(world, x, y - 1))] == 0) {
                    return false;
                }
            }
        }
        return true;
    }

    if (!cavityCells.contains(EncodeCell(anchorX, anchorY))) {
        return false;
    }
    for (int iy = 0; iy < mob.height; ++iy) {
        for (int ix = 0; ix < mob.width; ++ix) {
            const int x = anchorX + MobWidthOffsetX(ix);
            const int y = anchorY + iy;
            const size_t index = static_cast<size_t>(WorldCellIndex(world, x, y));
            if (solidCells[index] != 0) {
                return false;
            }
            if (iy == 0 &&
                solidCells[static_cast<size_t>(WorldCellIndex(world, x, y - 1))] == 0) {
                return false;
            }
        }
    }
    return true;
}

OilWellCandidateAnalysis AnalyzeOilWellCandidateCells(
    const Mob &mob,
    const World &world,
    const std::vector<GridCellPoint> &possibleSpawnPoints,
    const std::unordered_set<int64_t> &cavityCells,
    const std::vector<uint8_t> &solidCells,
    const std::vector<uint8_t> &liquidCells,
    const std::unordered_set<int64_t> &claimedCells,
    const std::unordered_set<int64_t> &occupiedCells)
{
    OilWellCandidateAnalysis analysis;
    analysis.featureSpawnCellCount = static_cast<int>(possibleSpawnPoints.size());
    analysis.candidates.reserve(possibleSpawnPoints.size());
    for (const auto &point : possibleSpawnPoints) {
        if (!IsAmbientMobPlacementBoundsValid(world, mob, point.x, point.y)) {
            ++analysis.rejectedOutOfBounds;
            continue;
        }
        const int64_t anchorKey = EncodeCell(point.x, point.y);
        if (claimedCells.contains(anchorKey)) {
            ++analysis.rejectedClaimedAnchor;
            continue;
        }
        if (!cavityCells.contains(anchorKey)) {
            ++analysis.rejectedAnchorNotCavity;
            continue;
        }

        bool rejected = false;
        const int paddedWidth = mob.width + mob.paddingX * 2;
        for (int ix = 0; ix < paddedWidth && !rejected; ++ix) {
            const int x = point.x + MobWidthOffsetX(ix);
            for (int iy = 0; iy < mob.height; ++iy) {
                const int y = point.y + iy;
                const int64_t key = EncodeCell(x, y);
                if (!IsWorldCellValid(world, x, y)) {
                    ++analysis.rejectedOutOfBounds;
                    rejected = true;
                    break;
                }
                if (occupiedCells.contains(key)) {
                    ++analysis.rejectedOccupied;
                    rejected = true;
                    break;
                }
            }
        }
        if (rejected) {
            continue;
        }

        for (int iy = 0; iy < mob.height && !rejected; ++iy) {
            for (int ix = 0; ix < mob.width; ++ix) {
                const int x = point.x + MobWidthOffsetX(ix);
                const int y = point.y + iy;
                if (!IsWorldCellValid(world, x, y) || !IsWorldCellValid(world, x, y - 1)) {
                    ++analysis.rejectedOutOfBounds;
                    rejected = true;
                    break;
                }
                const size_t index = static_cast<size_t>(WorldCellIndex(world, x, y));
                if (solidCells[index] != 0 || liquidCells[index] != 0) {
                    ++analysis.rejectedNonEmpty;
                    rejected = true;
                    break;
                }
                if (iy == 0 &&
                    solidCells[static_cast<size_t>(WorldCellIndex(world, x, y - 1))] == 0) {
                    ++analysis.rejectedUnsupportedFloor;
                    rejected = true;
                    break;
                }
            }
        }
        if (rejected) {
            continue;
        }

        analysis.candidates.push_back(point);
    }
    analysis.candidateCount = static_cast<int>(analysis.candidates.size());
    return analysis;
}

std::vector<GridCellPoint> CollectAmbientMobCandidateCells(
    const Mob &mob,
    const World &world,
    const std::vector<GridCellPoint> &possibleSpawnPoints,
    const std::unordered_set<int64_t> &cavityCells,
    const std::vector<uint8_t> &solidCells,
    const std::vector<uint8_t> &liquidCells,
    const std::unordered_set<int64_t> *claimedCells,
    const std::unordered_set<int64_t> &occupiedCells)
{
    std::vector<GridCellPoint> candidates;
    candidates.reserve(possibleSpawnPoints.size());
    for (const auto &point : possibleSpawnPoints) {
        if (!CanPlaceAmbientMobAtCell(world,
                                      mob,
                                      point.x,
                                      point.y,
                                      cavityCells,
                                      solidCells,
                                      liquidCells,
                                      claimedCells,
                                      occupiedCells)) {
            continue;
        }
        candidates.push_back(point);
    }
    return candidates;
}

void SimulateFeatureAmbientMobOccupancy(
    const World &world,
    const FeatureAmbientLayout &layout,
    const SettingsCache &settings,
    const std::unordered_set<int64_t> &cavityCells,
    const std::vector<uint8_t> &solidCells,
    const std::vector<uint8_t> &liquidCells,
    const std::unordered_set<int64_t> &claimedCells,
    KRandom &spawnRandom,
    std::unordered_set<int64_t> &occupiedCells,
    std::vector<GridCellPoint> &spawnedOilWells)
{
    if (layout.feature == nullptr || layout.feature->internalMobs.empty()) {
        return;
    }

    std::vector<GridCellPoint> possiblePoints;
    possiblePoints.reserve(layout.spawnCells.size());
    for (const auto &cell : layout.spawnCells) {
        possiblePoints.push_back(cell);
    }

    for (const auto &internalMob : layout.feature->internalMobs) {
        const auto mobItr = settings.mobs.MobLookupTable.add.find(internalMob.type);
        if (mobItr == settings.mobs.MobLookupTable.add.end()) {
            continue;
        }
        const auto &mob = mobItr->second;
        if (!IsAmbientMobLocationSupported(mob.location)) {
            continue;
        }

        auto candidates = CollectAmbientMobCandidateCells(mob,
                                                          world,
                                                          possiblePoints,
                                                          cavityCells,
                                                          solidCells,
                                                          liquidCells,
                                                          &claimedCells,
                                                          occupiedCells);
        if (candidates.empty()) {
            continue;
        }

        ShuffleSeeded(candidates, spawnRandom);
        const int requestedCount = std::max(
            0, static_cast<int>(std::floor(internalMob.count.GetRandomValue(spawnRandom) + 0.5f)));
        int skippedCandidates = 0;
        for (int candidateIndex = 0;
             candidateIndex - skippedCandidates < requestedCount &&
             candidateIndex < static_cast<int>(candidates.size());
             ++candidateIndex) {
            const auto &selected = candidates[static_cast<size_t>(candidateIndex)];
            if (!CanPlaceAmbientMobAtCell(world,
                                          mob,
                                          selected.x,
                                          selected.y,
                                          cavityCells,
                                          solidCells,
                                          liquidCells,
                                          &claimedCells,
                                          occupiedCells)) {
                ++skippedCandidates;
                continue;
            }
            AddOccupiedMobCells(mob, selected.x, selected.y, occupiedCells);
            const std::string_view prefab =
                mob.prefabName.empty() ? std::string_view(internalMob.type)
                                       : std::string_view(mob.prefabName);
            if (prefab == "OilWell") {
                spawnedOilWells.push_back(selected);
            }
        }
    }
}

void SimulateBiomeMobOccupancy(
    const World &world,
    const Site &site,
    const SettingsCache &settings,
    const std::unordered_set<GridCellPoint, GridCellPointHash> &terrainCells,
    const std::unordered_set<int64_t> &cavityCells,
    const std::vector<uint8_t> &solidCells,
    const std::vector<uint8_t> &liquidCells,
    const std::unordered_set<int64_t> &claimedCells,
    KRandom &spawnRandom,
    std::unordered_set<int64_t> &occupiedCells)
{
    const auto biomeTags = ResolveOrderedBiomeMobTags(site);
    if (biomeTags.empty()) {
        return;
    }

    std::vector<GridCellPoint> possiblePoints;
    possiblePoints.reserve(terrainCells.size());
    for (const auto &cell : terrainCells) {
        if (claimedCells.contains(EncodeCell(cell.x, cell.y))) {
            continue;
        }
        possiblePoints.push_back(cell);
    }
    if (possiblePoints.empty()) {
        return;
    }

    for (const std::string_view tag : biomeTags) {
        const auto mobItr = settings.mobs.MobLookupTable.add.find(std::string(tag));
        if (mobItr == settings.mobs.MobLookupTable.add.end()) {
            continue;
        }
        const auto &mob = mobItr->second;
        if (!IsAmbientMobLocationSupported(mob.location)) {
            continue;
        }

        auto candidates = CollectAmbientMobCandidateCells(mob,
                                                          world,
                                                          possiblePoints,
                                                          cavityCells,
                                                          solidCells,
                                                          liquidCells,
                                                          nullptr,
                                                          occupiedCells);
        if (candidates.empty()) {
            continue;
        }

        ShuffleSeeded(candidates, spawnRandom);
        float density = mob.density.GetRandomValue(spawnRandom);
        if (density > 1.0f) {
            density = 1.0f;
        }
        const int requestedCount = std::max(
            0,
            static_cast<int>(
                std::floor(static_cast<float>(candidates.size()) * density + 0.5f)));
        int skippedCandidates = 0;
        for (int candidateIndex = 0;
             candidateIndex - skippedCandidates < requestedCount &&
             candidateIndex < static_cast<int>(candidates.size());
             ++candidateIndex) {
            const auto &selected = candidates[static_cast<size_t>(candidateIndex)];
            if (!CanPlaceAmbientMobAtCell(world,
                                          mob,
                                          selected.x,
                                          selected.y,
                                          cavityCells,
                                          solidCells,
                                          liquidCells,
                                          nullptr,
                                          occupiedCells)) {
                ++skippedCandidates;
                continue;
            }
            AddOccupiedMobCells(mob, selected.x, selected.y, occupiedCells);
        }
    }
}

std::vector<OilWellDebugRecord> BuildOilWellDebugRecords(
    const std::vector<Site> &generatedSites,
    const SettingsCache &settings,
    const World &world,
    int seed,
    const std::vector<TemplateSpawner> &templates)
{
    std::vector<OilWellDebugRecord> records;
    const auto mobItr = settings.mobs.MobLookupTable.add.find("OilWell");
    const auto featureItr = settings.features.find("features/oilpockets/OilWell");
    if (mobItr == settings.mobs.MobLookupTable.add.end() ||
        featureItr == settings.features.end()) {
        return records;
    }

    std::unordered_set<int64_t> claimedCells;
    AddWorldBorderClaimedCells(generatedSites, world, settings, claimedCells);
    for (const auto &templt : templates) {
        AddTemplateClaimedCells(templt, claimedCells);
    }

    std::unordered_set<int64_t> occupiedAmbientCells;
    KRandom spawnRandom(seed);
    const auto simulatedTerrain =
        SimulateTerrainStateForOilWell(generatedSites, settings, world, seed);

    struct LeafSiteRef {
        const Site *site = nullptr;
        int parentIndex = -1;
        int childIndex = -1;
    };
    std::vector<LeafSiteRef> leafSites;
    for (size_t parentIndex = 0; parentIndex < generatedSites.size(); ++parentIndex) {
        const auto &parent = generatedSites[parentIndex];
        if (!parent.children) {
            continue;
        }
        for (size_t childIndex = 0; childIndex < parent.children->size(); ++childIndex) {
            leafSites.push_back(LeafSiteRef{
                .site = &parent.children->at(childIndex),
                .parentIndex = static_cast<int>(parentIndex),
                .childIndex = static_cast<int>(childIndex),
            });
        }
    }
    std::ranges::sort(leafSites, [](const LeafSiteRef &lhs, const LeafSiteRef &rhs) {
        return lhs.site->idx < rhs.site->idx;
    });

    for (const auto &leaf : leafSites) {
        const auto terrainCells = BuildTerrainCellPoints(*leaf.site);
        if (terrainCells.empty()) {
            continue;
        }

        const auto featureCellsItr = simulatedTerrain.featureCellsBySite.find(leaf.site->idx);
        if (featureCellsItr != simulatedTerrain.featureCellsBySite.end()) {
            if (featureCellsItr->second.feature == &featureItr->second) {
                std::vector<GridCellPoint> possiblePoints;
                possiblePoints.reserve(featureCellsItr->second.spawnCells.size());
                for (const auto &cell : featureCellsItr->second.spawnCells) {
                    possiblePoints.push_back(cell);
                }

                auto analysis = AnalyzeOilWellCandidateCells(mobItr->second,
                                                             world,
                                                             possiblePoints,
                                                             simulatedTerrain.cavityCells,
                                                             simulatedTerrain.solidCells,
                                                             simulatedTerrain.liquidCells,
                                                             claimedCells,
                                                             occupiedAmbientCells);
                std::vector<GridCellPoint> spawnedOilWells;
                SimulateFeatureAmbientMobOccupancy(world,
                                                  featureCellsItr->second,
                                                  settings,
                                                  simulatedTerrain.cavityCells,
                                                  simulatedTerrain.solidCells,
                                                  simulatedTerrain.liquidCells,
                                                  claimedCells,
                                                  spawnRandom,
                                                  occupiedAmbientCells,
                                                  spawnedOilWells);

                OilWellDebugRecord record;
                record.siteIndex = leaf.site->idx;
                record.parentIndex = leaf.parentIndex;
                record.childIndex = leaf.childIndex;
                record.subworld =
                    leaf.site->subworld != nullptr ? leaf.site->subworld->name : "";
                record.zoneType =
                    leaf.site->subworld != nullptr ? static_cast<int>(leaf.site->subworld->zoneType)
                                                   : -1;
                const auto centroid = leaf.site->polygon.Centroid();
                record.centroid = {
                    static_cast<int>(std::floor(centroid.x + 0.5f)),
                    static_cast<int>(std::floor(centroid.y + 0.5f)),
                };
                record.featureSpawnCellCount = analysis.featureSpawnCellCount;
                record.candidateCount = analysis.candidateCount;
                record.rejectedOutOfBounds = analysis.rejectedOutOfBounds;
                record.rejectedClaimedAnchor = analysis.rejectedClaimedAnchor;
                record.rejectedAnchorNotCavity = analysis.rejectedAnchorNotCavity;
                record.rejectedOccupied = analysis.rejectedOccupied;
                record.rejectedNonEmpty = analysis.rejectedNonEmpty;
                record.rejectedUnsupportedFloor = analysis.rejectedUnsupportedFloor;
                const size_t previewCount = std::min<size_t>(analysis.candidates.size(), 8);
                record.candidatePreview.assign(analysis.candidates.begin(),
                                               analysis.candidates.begin() + previewCount);
                record.spawned = std::move(spawnedOilWells);
                records.push_back(std::move(record));
            } else {
                std::vector<GridCellPoint> ignored;
                SimulateFeatureAmbientMobOccupancy(world,
                                                  featureCellsItr->second,
                                                  settings,
                                                  simulatedTerrain.cavityCells,
                                                  simulatedTerrain.solidCells,
                                                  simulatedTerrain.liquidCells,
                                                  claimedCells,
                                                  spawnRandom,
                                                  occupiedAmbientCells,
                                                  ignored);
            }
        }

        SimulateBiomeMobOccupancy(world,
                                  *leaf.site,
                                  settings,
                                  terrainCells,
                                  simulatedTerrain.cavityCells,
                                  simulatedTerrain.solidCells,
                                  simulatedTerrain.liquidCells,
                                  claimedCells,
                                  spawnRandom,
                                  occupiedAmbientCells);
    }

    return records;
}

} // namespace

bool WorldGen::GenerateOverworld(std::vector<Site> &sites)
{
    KRandom random(m_seed);
    if (!GenerateSeedPoints(random, sites)) {
        return false;
    }
    bool usePD = m_world.layoutMethod == LayoutMethod::PowerTree;
    Polygon bounds(Rect(0.0f, 0.0f, m_world.worldsize.x, m_world.worldsize.y));
    Diagram diagram(bounds, sites);
    if (usePD) {
        diagram.ComputeNode();
        diagram.ComputeNodePD();
    } else {
        diagram.ComputeNode();
    }
    PropagateDistanceTags(sites);
    ConvertUnknownCells(sites, random);
    if (usePD) {
        diagram.ComputeNodePD();
    }
    int count = 0;
    for (int i = 0; i < (int)sites.size(); ++i) {
        count += GenerateChildren(sites[i], random, m_seed + i, usePD);
    }
    std::vector<Site *> allSites;
    allSites.reserve(count);
    if (!ForceLowestToLeaf(sites, allSites)) {
        return false;
    }
    random = KRandom(m_seed);
    ApplySwapTags(sites, random);
    DetermineTemplates(allSites, random);
    return true;
}

bool WorldGen::ForceLowestToLeaf(std::vector<Site> &sites,
                                 std::vector<Site *> &allSites)
{
    int index = 1;
    Site *startSite = nullptr;
    for (auto &site : sites) {
        for (auto &child : *site.children) {
            child.idx = index++;
            allSites.push_back(&child);
            if (startSite == nullptr && child.tags.contains("StartLocation")) {
                startSite = &child;
            }
        }
    }
    if (startSite == nullptr) {
        return false;
    }
    for (auto &site : *startSite->parent->children) {
        site.tags.insert("IgnoreCaveOverride");
    }
    for (auto neighbour : startSite->neighbours) {
        const_cast<Site *>(neighbour)->tags.insert("NearStartLocation");
    }
    return true;
}

static inline void SwitchNodes(Site &lhs, Site &rhs)
{
    std::swap(lhs.idx, rhs.idx);
    std::swap(lhs.x, rhs.x);
    std::swap(lhs.y, rhs.y);
    lhs.polygon.Swap(rhs.polygon);
}

static void ApplySwapTags(std::vector<Site> &sites, KRandom &random)
{
    std::vector<Site *> nodes;
    for (auto &site : sites) {
        if (site.tags.contains("CenteralFeature") ||
            !site.tags.contains("SwapLakesToBelow")) {
            continue;
        }
        nodes.clear();
        for (auto &child : *site.children) {
            if (!child.tags.contains("CenteralFeature")) {
                nodes.push_back(&child);
            }
        }
        ShuffleSeeded(nodes, random);
        std::queue<Site *> above;
        std::queue<Site *> below;
        for (auto node : nodes) {
            bool isWet = node->tags.contains("Wet");
            bool isAbove = node->y > site.y;
            if (isWet && isAbove) {
                above.push(node);
            } else if (!isWet && !isAbove) {
                below.push(node);
            }
        }
        while (!above.empty() && !below.empty()) {
            SwitchNodes(*above.front(), *below.front());
            above.pop();
            below.pop();
        }
    }
}

static bool RunFilterTagCommand(Site &site, const AllowedCellsFilter &filter)
{
    switch (filter.tagcommand) {
    case TagCommand::Default:
        return true;
    case TagCommand::AtTag:
        return std::ranges::contains(site.tags, filter.tag);
    case TagCommand::NotAtTag:
        return !std::ranges::contains(site.tags, filter.tag);
    case TagCommand::DistanceFromTag: {
        auto distance = site.minDistanceToTag[filter.tag];
        if (distance < filter.minDistance || filter.maxDistance < distance) {
            return false;
        } else {
            return true;
        }
    }
    default:
        return false;
    }
}

static HashSet<WeightedSubWorld *>
GetNameFilterSet(const AllowedCellsFilter &filter,
                 std::vector<WeightedSubWorld> &subworlds)
{
    HashSet<WeightedSubWorld *> hashSet;
    for (auto &name : filter.subworldNames) {
        for (auto &subworld : subworlds) {
            if (subworld.subWorld == nullptr)
                continue;
            if (subworld.subWorld->name == name) {
                hashSet.Append(&subworld);
            }
        }
    }
    return hashSet;
}

static HashSet<WeightedSubWorld *> GetZoneTypeFilterSet(
    const AllowedCellsFilter &filter,
    std::map<int, std::vector<WeightedSubWorld *>> &subworldsByZoneType)
{
    HashSet<WeightedSubWorld *> hashSet;
    for (auto &zoneType : filter.zoneTypes) {
        auto &subWorlds = subworldsByZoneType[(int)zoneType];
        for (auto &subworld : subWorlds) {
            if (subworld->subWorld == nullptr)
                continue;
            hashSet.Append(subworld);
        }
    }
    return hashSet;
}

static HashSet<WeightedSubWorld *> GetTemperatureFilterSet(
    const AllowedCellsFilter &filter,
    std::map<int, std::vector<WeightedSubWorld *>> subworldsByTemperature)
{
    HashSet<WeightedSubWorld *> hashSet;
    for (auto &tempRange : filter.temperatureRanges) {
        auto &subWorlds = subworldsByTemperature[(int)tempRange];
        for (auto &subworld : subWorlds) {
            if (subworld->subWorld == nullptr)
                continue;
            hashSet.Append(subworld);
        }
    }
    return hashSet;
}

static std::vector<WeightedSubWorld *>
Filter(Site &site, const World &world,
       std::vector<WeightedSubWorld> &allSubWorlds,
       std::map<int, std::vector<WeightedSubWorld *>> &subworldsByTemperature,
       std::map<int, std::vector<WeightedSubWorld *>> &subworldsByZoneType)
{
    HashSet<WeightedSubWorld *> hashSet;
    HashSet<WeightedSubWorld *> hashSet2;
    for (auto filter : world.unknownCellsAllowedSubworlds2) {
        hashSet2.Clear();
        bool useFilter = RunFilterTagCommand(site, *filter);
        if (useFilter && !filter->subworldNames.empty()) {
            hashSet2.UnionWith(GetNameFilterSet(*filter, allSubWorlds));
        }
        if (useFilter && !filter->temperatureRanges.empty()) {
            hashSet2.UnionWith(
                GetTemperatureFilterSet(*filter, subworldsByTemperature));
        }
        if (useFilter && !filter->zoneTypes.empty()) {
            hashSet2.UnionWith(
                GetZoneTypeFilterSet(*filter, subworldsByZoneType));
        }
        switch (filter->command) {
        case Command::Clear:
            if (useFilter) {
                hashSet.Clear();
            }
            break;
        case Command::Replace:
            if (hashSet2.Size() > 0) {
                hashSet.Clear();
                hashSet.UnionWith(hashSet2);
            }
            break;
        case Command::UnionWith:
            hashSet.UnionWith(hashSet2);
            break;
        case Command::ExceptWith:
            hashSet.ExceptWith(hashSet2);
            break;
        case Command::IntersectWith: {
            hashSet.IntersectWith(hashSet2);
            break;
        }
        case Command::SymmetricExceptWith: {
            hashSet.SymmetricExceptWith(hashSet2);
            break;
        }
        case Command::All:
        default:
            break;
        }
    }
    return hashSet.ToList();
}

static void ApplySubworldToNode(Site &site, const SubWorld &subWorld,
                                float overridePower = -1.0f)
{
    site.subworld = &subWorld;
    site.weight = ((overridePower > 0.0f) ? overridePower : subWorld.pdWeight);
    for (auto &tag : subWorld.tags) {
        site.tags.insert(tag);
    }
    site.tags.insert(subWorld.name);
    site.tags.insert(ZoneTypeToString(subWorld.zoneType));
    site.tags.insert(TempRangeToString(subWorld.temperatureRange));
}

void WorldGen::ConvertUnknownCells(std::vector<Site> &sites, KRandom &random)
{
    std::vector<int> indices(sites.size() - 1);
    std::iota(indices.begin(), indices.end(), 1);
    ShuffleSeeded(indices, random);
    std::vector<WeightedSubWorld> subworldsForWorld;
    const std::vector<SubWorld *> *subworldList = nullptr;
    if (m_settings.IsSpaceOutEnabled()) {
        auto itr = m_settings.orderedSubworlds.find("SPACEOUT");
        if (itr != m_settings.orderedSubworlds.end()) {
            subworldList = &itr->second;
        }
    } else {
        auto itr = m_settings.orderedSubworlds.find("VANILLA");
        if (itr != m_settings.orderedSubworlds.end()) {
            subworldList = &itr->second;
        }
    }
    if (subworldList == nullptr) {
        return;
    }
    for (auto item : *subworldList) {
        for (auto subworld : m_world.subworldFiles2) {
            if (item->name == subworld->name) {
                subworldsForWorld.emplace_back(item, *subworld);
            }
        }
    }
    std::map<int, std::vector<WeightedSubWorld *>> dict1;
    for (int i = 0; i <= (int)Range::ExtremelyHot; ++i) {
        auto &list = dict1[i];
        for (auto &subworld : subworldsForWorld) {
            if (subworld.subWorld->temperatureRange == (Range)i) {
                list.push_back(&subworld);
            }
        }
    }
    std::map<int, std::vector<WeightedSubWorld *>> dict2;
    for (int i = 0; i <= (int)ZoneType::SugarWoods; ++i) {
        auto &list = dict2[i];
        for (auto &subworld : subworldsForWorld) {
            if (subworld.subWorld->zoneType == (ZoneType)i) {
                list.push_back(&subworld);
            }
        }
    }
    for (auto index : indices) {
        auto &site = sites[index];
        auto list2 = Filter(site, m_world, subworldsForWorld, dict1, dict2);
        std::vector<WeightedSubWorld *> list3;
        for (auto &item : list2) {
            if (item->minCount > 0) {
                list3.push_back(item);
            }
        }
        WeightedSubWorld *weightedSubWorld;
        if (!list3.empty()) {
            weightedSubWorld = list3[0];
            int priority = weightedSubWorld->priority;
            for (auto &item2 : list3) {
                if (item2->priority > priority ||
                    (item2->priority == priority &&
                     item2->minCount > weightedSubWorld->minCount)) {
                    weightedSubWorld = item2;
                    priority = item2->priority;
                }
            }
            weightedSubWorld->minCount--;
        } else {
            weightedSubWorld = WeightedRandom_Choose(list2, random);
        }
        if (weightedSubWorld != nullptr &&
            weightedSubWorld->subWorld != nullptr) {
            ApplySubworldToNode(site, *weightedSubWorld->subWorld,
                                weightedSubWorld->overridePower);
            weightedSubWorld->maxCount--;
            if (weightedSubWorld->maxCount <= 0) {
                weightedSubWorld->subWorld = nullptr;
            }
        }
    }
    auto &globalFeatures = m_world.globalFeatures2;
    std::vector<Site *> list2;
    list2.reserve(sites.size());
    for (auto &site : sites) {
        if (!site.tags.contains("NoGlobalFeatureSpawning")) {
            list2.push_back(&site);
        }
    }
    ShuffleSeeded(list2, random);
    for (size_t i = 0; i < list2.size() && i < globalFeatures.size(); ++i) {
        list2[i]->globalFeature = globalFeatures[i];
    }
}

static void
TagTopAndBottomSites(float mapHeight, std::vector<Site> &sites,
                     std::map<std::string, std::vector<Site *>> &sitesWithTags)
{
    auto minY = 5.0f;
    auto maxY = mapHeight - 5.0f;
    auto &surfaceSites = sitesWithTags["AtSurface"];
    auto &depthsSites = sitesWithTags["AtDepths"];
    for (auto &site : sites) {
        auto &bounds = site.polygon.Bounds();
        if (bounds.y < maxY && maxY < bounds.y + bounds.height) {
            site.tags.emplace("AtSurface");
            surfaceSites.emplace_back(&site);
        }
        if (bounds.y < minY && minY < bounds.y + bounds.height) {
            site.tags.emplace("AtDepths");
            depthsSites.emplace_back(&site);
        }
    }
}

static void
TagEdgeSites(float mapWidth, std::vector<Site> &sites,
             std::map<std::string, std::vector<Site *>> &sitesWithTags)
{
    auto minX = 5.0f;
    auto maxX = mapWidth - 5.0f;
    auto &edgeSites = sitesWithTags["AtEdge"];
    auto &leftSites = sitesWithTags["AtLeft"];
    auto &rightSites = sitesWithTags["AtRight"];
    for (auto &site : sites) {
        auto &bounds = site.polygon.Bounds();
        if (bounds.x < minX && minX < bounds.x + bounds.width) {
            site.tags.emplace("AtEdge");
            site.tags.emplace("AtLeft");
            edgeSites.emplace_back(&site);
            leftSites.emplace_back(&site);
        }
        if (bounds.x < maxX && maxX < bounds.x + bounds.width) {
            site.tags.emplace("AtEdge");
            site.tags.emplace("AtRight");
            edgeSites.emplace_back(&site);
            rightSites.emplace_back(&site);
        }
    }
}

void WorldGen::PropagateDistanceTags(std::vector<Site> &sites) const
{
    std::map<std::string, std::vector<Site *>> sitesWithTags;

    sites[0].tags.emplace("AtStart");
    sitesWithTags["AtStart"].emplace_back(&sites[0]);

    TagTopAndBottomSites(m_world.worldsize.y, sites, sitesWithTags);
    TagEdgeSites(m_world.worldsize.x, sites, sitesWithTags);

    const char *tags[] = {"AtSurface", "AtDepths", "AtEdge",
                          "AtStart",   "AtLeft",   "AtRight"};
    for (auto &tag : tags) {
        for (auto &site : sites) {
            site.visited = false;
        }
        auto &sitesWithTag = sitesWithTags[tag];
        std::queue<const Site *> neighbours;
        for (auto *site : sitesWithTag) {
            site->visited = true;
            site->minDistanceToTag.emplace(tag, 0);
            neighbours.push(site);
        }
        int distance = 0;
        const Site *site = nullptr;
        const Site *end = nullptr;
        while (!neighbours.empty()) {
            if (site == end) {
                distance++;
                end = neighbours.back();
            }
            site = neighbours.front();
            neighbours.pop();
            for (const auto *constNeighbour : site->neighbours) {
                Site *neighbour = const_cast<Site *>(constNeighbour);
                if (!neighbour->visited) {
                    neighbour->visited = true;
                    neighbour->minDistanceToTag.emplace(tag, distance);
                    neighbours.push(neighbour);
                }
            }
        }
    }
}

bool WorldGen::GenerateSeedPoints(KRandom &random, std::vector<Site> &sites)
{
    auto mapWidth = m_world.worldsize.x;
    auto mapHeight = m_world.worldsize.y;
    Polygon poly(Rect(0.0f, 0.0f, mapWidth, mapHeight));
    auto densityMin = GetDefaultData<float>("OverworldDensityMin");
    auto densityMax = GetDefaultData<float>("OverworldDensityMax");
    auto density = random.Next(densityMin, densityMax);
    auto avoidRadius = GetDefaultData<float>("OverworldAvoidRadius");
    auto startX = m_world.startingPositionHorizontal2.GetRandomValue(random);
    auto startY = m_world.startingPositionVertical2.GetRandomValue(random);
    std::vector<Vector2f> position;
    position.emplace_back(startX * mapWidth, startY * mapHeight);
    auto &sampler = GetDefaultData<std::string>("OverworldSampleBehaviour");
    auto enumSampler = sampler == "UniformHex" ? SampleBehaviour::UniformHex
                                               : SampleBehaviour::PoissonDisk;
    auto points = GetRandomPoints(poly, density, avoidRadius, position,
                                  enumSampler, false, random, false, true);
    auto subworldFile = std::ranges::find_if(
        m_world.subworldFiles2, [this](const WeightedSubworldName *x) {
            return x->name == m_world.startSubworldName;
        });
    auto subworld = m_settings.subworlds.find(m_world.startSubworldName);
    if (subworld == m_settings.subworlds.end()) {
        LogE("start subworld %s wrong.", m_world.startSubworldName.c_str());
        return false;
    }
    float overridePower = -1.0f;
    if (subworldFile != m_world.subworldFiles2.end() &&
        (*subworldFile)->overridePower > 0.0f) {
        overridePower = (*subworldFile)->overridePower;
    }
    int index = 1;
    sites.reserve(points.size() + 10); // reserve with dummy sites;
    sites.emplace_back(index++, position[0]);
    ApplySubworldToNode(sites.back(), subworld->second, overridePower);
    for (auto &point : points) {
        sites.emplace_back(index++, point);
    }
    return true;
}

void WorldGen::SetFeatureBiome(Site &site, KRandom &random,
                               const Feature *feature)
{
    bool flag = false;
    if (feature != nullptr) {
        auto itr = m_settings.features.find(feature->type);
        if (itr != m_settings.features.end()) {
            auto &feature2 = itr->second;
            auto &biomes = itr->second.biomeTags;
            site.tags.insert(feature2.tags.begin(), feature2.tags.end());
            site.tags.insert(feature->tags.begin(), feature->tags.end());
            for (auto &tag : feature->excludesTags) {
                site.tags.erase(tag);
            }
            site.tags.insert("Feature");
            site.tags.insert(feature->type);
            if (!feature2.forceBiome.empty()) {
                site.tags.insert(feature2.forceBiome);
                flag = true;
            }
            site.tags.insert(biomes.begin(), biomes.end());
        } else {
            LogE("unknown feature: %s", feature->type.c_str());
        }
    }
    if (!flag && !site.subworld->biomes.empty()) {
        std::vector<const WeightedBiome *> biomes;
        for (auto &biome : site.subworld->biomes) {
            biomes.push_back(&biome);
        }
        auto biome = WeightedRandom_Choose(biomes, random);
        site.tags.insert(biome->name);
        site.tags.insert(biome->tags.begin(), biome->tags.end());
        flag = true;
    }
    if (!flag) {
        site.tags.insert("UNKNOWN");
    }
}

size_t WorldGen::GenerateChildren(Site &site, KRandom &externRrandom, int seed,
                                  bool usePD)
{
    KRandom random(seed);
    auto &subworld = *site.subworld;
    int index = 1;
    site.children = std::make_unique<std::vector<Site>>();

    float density = subworld.density.GetRandomValue(random);
    int minPointCount = subworld.features.size() + subworld.extraBiomeChildren;
    if (site.globalFeature != nullptr) {
        minPointCount++;
    }
    if (minPointCount < subworld.minChildCount) {
        minPointCount = subworld.minChildCount;
    }
    int maxPointCount = std::numeric_limits<int>::max();
    if (subworld.singleChildCount) {
        minPointCount = 1;
        maxPointCount = 1;
    }
    auto &boundingArea = site.polygon;
    float avoidRadius = subworld.avoidRadius;
    std::vector<Vector2f> position;
    if (subworld.centralFeature.has_value()) {
        position.push_back(site.polygon.Centroid());
        site.children->emplace_back(index++, site.polygon.Centroid());
        auto &child = site.children->back();
        auto &feature = subworld.centralFeature.value();
        child.subworld = site.subworld;
        child.tags = site.tags;
        child.tags.insert("CenteralFeature");
        child.parent = &site;
        SetFeatureBiome(child, externRrandom, &feature);
    }
    std::vector<Vector2f> points;
    for (int i = 0; i < 10; ++i) {
        points = GetRandomPoints(boundingArea, density, avoidRadius, position,
                                 subworld.sampleBehaviour, true, random, true,
                                 subworld.doAvoidPoints);
        if (minPointCount <= (int)points.size()) {
            break;
        } else if (maxPointCount < (int)points.size()) {
            points.resize(maxPointCount);
            break;
        } else {
            density *= 0.8f;
            avoidRadius *= 0.8f;
        }
    }
    for (auto &sampler : subworld.samplers) {
        position.insert(position.end(), points.begin(), points.end());
        density = sampler.density.GetRandomValue(random);
        auto rndPoints = GetRandomPoints(
            boundingArea, density, sampler.avoidRadius, position,
            sampler.sampleBehaviour, true, random, true, sampler.doAvoidPoints);
        points.insert(points.end(), rndPoints.begin(), rndPoints.end());
    }
    if (points.size() > 200) {
        points.resize(200);
    }
    for (size_t i = 0; i < points.size(); ++i) {
        const Feature *feature = nullptr;
        if (i < subworld.features.size()) {
            feature = &subworld.features[i];
        }
        if (i == subworld.features.size()) {
            feature = site.globalFeature;
        }
        site.children->emplace_back(index++, points[i]);
        auto &child = site.children->back();
        child.subworld = site.subworld;
        child.tags = site.tags;
        child.parent = &site;
        SetFeatureBiome(child, externRrandom, feature);
    }
    Diagram diagram(site.polygon, *site.children);
    diagram.ComputeNode();
    if (!subworld.dontRelaxChildren) {
        if (usePD) {
            diagram.ComputeNodePD();
        } else {
            diagram.ComputeNode();
        }
    }
    return (int)site.children->size();
}

static std::map<std::string_view, int> GenerateGeysersDict()
{
    const char *configs[] = {
        "steam", "hot_steam", "hot_water", "slush_water", "filthy_water",
        "slush_salt_water", "salt_water", "small_volcano", "big_volcano",
        "liquid_co2", "hot_co2", "hot_hydrogen", "hot_po2", "slimy_po2",
        "chlorine_gas", "methane", "molten_copper", "molten_iron",
        "molten_gold", "molten_aluminum", "molten_cobalt", "oil_drip",
        "liquid_sulfur", "chlorine_gas_cool", "molten_tungsten",
        "molten_niobium", "murky_brine",
        // special geyser
        "OilWell", "SmallReefGeyser", "UnderwaterVent",
        // important buildings
        "receiver", "sender", "teleporter", "cryopod", "printpod"};
    std::map<std::string_view, int> result;
    for (int i = 0; i < (int)std::size(configs); ++i) {
        result.emplace(configs[i], i);
    }
    return result;
}

std::vector<Vector3i> WorldGen::GetGeysers(int globalWorldSeed,
                                           const std::vector<Site> *generatedSites) const
{
    static std::map<std::string_view, int> configs = GenerateGeysersDict();
    std::vector<Vector3i> result;
    result.reserve(m_templates.size());
    int count = m_settings.IsSpaceOutEnabled() ? 23 : 20;
    for (auto &templt : m_templates) {
        const std::string &name = templt.container->name;
        Vector2<int> pos{templt.position};
        pos.y = (int)m_world.worldsize.y - pos.y;
        if (name == "geysers/generic") {
            int seed = globalWorldSeed + pos.x + (int)templt.position.y;
            int index = KRandom(seed).Next(0, count);
            if (!m_settings.IsSpaceOutEnabled() && index == 19) {
                index = 21;
            }
            result.emplace_back(pos.x, pos.y, index);
        } else if (name.starts_with("expansion1::poi/warp/receiver")) {
            bool found = false;
            for (const auto &entity : ExpandTemplateEntities(templt)) {
                if (entity.entityId != "WarpConduitReceiver") {
                    continue;
                }
                result.emplace_back(entity.position.x,
                                    (int)m_world.worldsize.y - entity.position.y,
                                    configs["receiver"]);
                found = true;
                break;
            }
            if (!found) {
                result.emplace_back(pos.x, pos.y, configs["receiver"]);
            }
        } else if (name.starts_with("expansion1::poi/warp/sender")) {
            bool found = false;
            for (const auto &entity : ExpandTemplateEntities(templt)) {
                if (entity.entityId != "WarpConduitSender") {
                    continue;
                }
                result.emplace_back(entity.position.x,
                                    (int)m_world.worldsize.y - entity.position.y,
                                    configs["sender"]);
                found = true;
                break;
            }
            if (!found) {
                result.emplace_back(pos.x, pos.y, configs["sender"]);
            }
        } else if (name.starts_with("expansion1::poi/warp/teleporter")) {
            bool found = false;
            for (const auto &entity : ExpandTemplateEntities(templt)) {
                if (entity.entityId != "WarpPortal") {
                    continue;
                }
                result.emplace_back(entity.position.x,
                                    (int)m_world.worldsize.y - entity.position.y,
                                    configs["teleporter"]);
                found = true;
                break;
            }
            if (!found) {
                result.emplace_back(pos.x, pos.y, configs["teleporter"]);
            }
        } else if (name.contains("::bases/warpworld")) {
            for (const auto &entity : ExpandTemplateEntities(templt)) {
                if (entity.entityId != "WarpPortal") {
                    continue;
                }
                result.emplace_back(entity.position.x,
                                    (int)m_world.worldsize.y - entity.position.y,
                                    configs["teleporter"]);
                break;
            }
        } else if (name.starts_with("expansion1::poi/traits/cryopod")) {
            result.emplace_back(pos.x, pos.y, configs["cryopod"]);
        } else if (!templt.container->otherEntities.empty()) {
            for (auto &item : templt.container->otherEntities) {
                if (item.id == "OilWell" || item.id == "SmallReefGeyser" ||
                    item.id == "UnderwaterVent") {
                    pos.x += item.location_x;
                    pos.y -= item.location_y;
                    result.emplace_back(pos.x, pos.y, configs[item.id]);
                } else if (item.id.find("GeyserGeneric_") == 0) {
                    auto itr = configs.find(item.id.substr(14));
                    if (itr != configs.end()) {
                        pos.x += item.location_x;
                        pos.y -= item.location_y;
                        result.emplace_back(pos.x, pos.y, itr->second);
                    }
                }
            }
        }
    }

    if (generatedSites != nullptr) {
        std::unordered_set<int64_t> claimedCells;
        AddWorldBorderClaimedCells(*generatedSites, m_world, m_settings, claimedCells);
        for (const auto &templt : m_templates) {
            AddTemplateClaimedCells(templt, claimedCells);
        }

        std::unordered_set<int64_t> occupiedAmbientCells;
        KRandom spawnRandom(m_seed);
        const auto simulatedTerrain =
            SimulateTerrainStateForOilWell(*generatedSites, m_settings, m_world, m_seed);

        std::vector<const Site *> leafSites;
        for (const auto &parent : *generatedSites) {
            if (!parent.children) {
                continue;
            }
            for (const auto &child : *parent.children) {
                leafSites.push_back(&child);
            }
        }
        std::ranges::sort(leafSites, [](const Site *lhs, const Site *rhs) {
            return lhs->idx < rhs->idx;
        });

        for (const Site *child : leafSites) {
            const auto terrainCells = BuildTerrainCellPoints(*child);
            if (terrainCells.empty()) {
                continue;
            }

            const auto featureCellsItr = simulatedTerrain.featureCellsBySite.find(child->idx);
            if (featureCellsItr != simulatedTerrain.featureCellsBySite.end()) {
                std::vector<GridCellPoint> spawnedOilWells;
                SimulateFeatureAmbientMobOccupancy(m_world,
                                                  featureCellsItr->second,
                                                  m_settings,
                                                  simulatedTerrain.cavityCells,
                                                  simulatedTerrain.solidCells,
                                                  simulatedTerrain.liquidCells,
                                                  claimedCells,
                                                  spawnRandom,
                                                  occupiedAmbientCells,
                                                  spawnedOilWells);
                for (const auto &selected : spawnedOilWells) {
                    result.emplace_back(selected.x,
                                        static_cast<int>(m_world.worldsize.y) - selected.y,
                                        configs["OilWell"]);
                }
            }

            SimulateBiomeMobOccupancy(m_world,
                                      *child,
                                      m_settings,
                                      terrainCells,
                                      simulatedTerrain.cavityCells,
                                      simulatedTerrain.solidCells,
                                      simulatedTerrain.liquidCells,
                                      claimedCells,
                                      spawnRandom,
                                      occupiedAmbientCells);
        }
    }
    return result;
}

std::vector<WorldGen::SpawnedTemplateEntity> WorldGen::ExpandTemplateEntities(
    const TemplateSpawner &spawner) const
{
    std::vector<SpawnedTemplateEntity> result;
    const TemplateContainer &container = *spawner.container;
    const Vector2<int> templatePos{
        spawner.position.x,
        spawner.position.y,
    };

    if (container.name == "geysers/generic") {
        result.push_back(SpawnedTemplateEntity{
            .entityId = "GeyserGeneric",
            .position = templatePos,
        });
        return result;
    }

    result.reserve(container.otherEntities.size() + container.buildings.size());

    for (const auto &item : container.otherEntities) {
        result.push_back(SpawnedTemplateEntity{
            .entityId = item.id,
            .position = Vector2<int>{
                templatePos.x + item.location_x,
                templatePos.y + item.location_y,
            },
        });
    }
    for (const auto &item : container.buildings) {
        result.push_back(SpawnedTemplateEntity{
            .entityId = item.id,
            .position = Vector2<int>{
                templatePos.x + item.location_x,
                templatePos.y + item.location_y,
            },
        });
    }
    return result;
}

void WorldGen::DumpDebugWorldState(const std::vector<Site> &sites,
                                   const std::string &path) const
{
    const auto oilWellDebug = BuildOilWellDebugRecords(
        sites, m_settings, m_world, m_seed, m_templates);
    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) {
        return;
    }

    file << "{\n";
    file << "  \"world\": \"" << EscapeJsonString(m_world.name) << "\",\n";
    file << "  \"worldSize\": {\"x\": " << m_world.worldsize.x
         << ", \"y\": " << m_world.worldsize.y << "},\n";
    file << "  \"sites\": [\n";
    for (size_t index = 0; index < sites.size(); ++index) {
        const auto &site = sites[index];
        const auto centroid = site.polygon.Centroid();
        file << "    {\n";
        file << "      \"index\": " << index << ",\n";
        file << "      \"centroid\": {\"x\": " << centroid.x << ", \"y\": " << centroid.y
             << "},\n";
        file << "      \"subworld\": \""
             << EscapeJsonString(site.subworld != nullptr ? site.subworld->name : "") << "\",\n";
        file << "      \"zoneType\": "
             << (site.subworld != nullptr ? static_cast<int>(site.subworld->zoneType) : -1)
             << ",\n";
        file << "      \"templateTag\": \"" << EscapeJsonString(site.templateTag) << "\",\n";
        file << "      \"tags\": [";
        bool firstTag = true;
        for (const auto &tag : site.tags) {
            if (!firstTag) {
                file << ", ";
            }
            firstTag = false;
            file << "\"" << EscapeJsonString(tag) << "\"";
        }
        file << "],\n";
        file << "      \"minDistanceToTag\": {";
        bool firstDistance = true;
        for (const auto &[tag, distance] : site.minDistanceToTag) {
            if (!firstDistance) {
                file << ", ";
            }
            firstDistance = false;
            file << "\"" << EscapeJsonString(tag) << "\": " << distance;
        }
        file << "}\n";
        file << "    }";
        if (index + 1 != sites.size()) {
            file << ",";
        }
        file << "\n";
    }
    file << "  ],\n";
    file << "  \"leafSites\": [\n";
    bool firstLeafSite = true;
    for (size_t parentIndex = 0; parentIndex < sites.size(); ++parentIndex) {
        const auto &parent = sites[parentIndex];
        if (parent.children == nullptr) {
            continue;
        }
        for (size_t childIndex = 0; childIndex < parent.children->size(); ++childIndex) {
            const auto &child = parent.children->at(childIndex);
            const auto centroid = child.polygon.Centroid();
            if (!firstLeafSite) {
                file << ",\n";
            }
            firstLeafSite = false;
            file << "    {\n";
            file << "      \"parentIndex\": " << parentIndex << ",\n";
            file << "      \"childIndex\": " << childIndex << ",\n";
            file << "      \"centroid\": {\"x\": " << centroid.x << ", \"y\": " << centroid.y
                 << "},\n";
            file << "      \"subworld\": \""
                 << EscapeJsonString(child.subworld != nullptr ? child.subworld->name : "")
                 << "\",\n";
            file << "      \"zoneType\": "
                 << (child.subworld != nullptr ? static_cast<int>(child.subworld->zoneType) : -1)
                 << ",\n";
            file << "      \"templateTag\": \"" << EscapeJsonString(child.templateTag) << "\",\n";
            file << "      \"tags\": [";
            bool firstTag = true;
            for (const auto &tag : child.tags) {
                if (!firstTag) {
                    file << ", ";
                }
                firstTag = false;
                file << "\"" << EscapeJsonString(tag) << "\"";
            }
            file << "],\n";
            file << "      \"minDistanceToTag\": {";
            bool firstDistance = true;
            for (const auto &[tag, distance] : child.minDistanceToTag) {
                if (!firstDistance) {
                    file << ", ";
                }
                firstDistance = false;
                file << "\"" << EscapeJsonString(tag) << "\": " << distance;
            }
            file << "}\n";
            file << "    }";
        }
    }
    if (!firstLeafSite) {
        file << "\n";
    }
    file << "  ],\n";
    file << "  \"templates\": [\n";
    for (size_t index = 0; index < m_templates.size(); ++index) {
        const auto &spawner = m_templates[index];
        file << "    {\n";
        file << "      \"name\": \"" << EscapeJsonString(spawner.container->name) << "\",\n";
        file << "      \"root\": {\"x\": " << spawner.position.x << ", \"y\": "
             << spawner.position.y << "},\n";
        file << "      \"entities\": [\n";
        const auto entities = ExpandTemplateEntities(spawner);
        for (size_t entityIndex = 0; entityIndex < entities.size(); ++entityIndex) {
            const auto &entity = entities[entityIndex];
            file << "        {\"id\": \"" << EscapeJsonString(entity.entityId)
                 << "\", \"x\": " << entity.position.x << ", \"y\": " << entity.position.y
                 << "}";
            if (entityIndex + 1 != entities.size()) {
                file << ",";
            }
            file << "\n";
        }
        file << "      ]\n";
        file << "    }";
        if (index + 1 != m_templates.size()) {
            file << ",";
        }
        file << "\n";
    }
    file << "  ],\n";
    file << "  \"oilWellDiagnostics\": [\n";
    for (size_t index = 0; index < oilWellDebug.size(); ++index) {
        const auto &record = oilWellDebug[index];
        file << "    {\n";
        file << "      \"siteIndex\": " << record.siteIndex << ",\n";
        file << "      \"parentIndex\": " << record.parentIndex << ",\n";
        file << "      \"childIndex\": " << record.childIndex << ",\n";
        file << "      \"subworld\": \"" << EscapeJsonString(record.subworld) << "\",\n";
        file << "      \"zoneType\": " << record.zoneType << ",\n";
        file << "      \"centroid\": {\"x\": " << record.centroid.x << ", \"y\": "
             << record.centroid.y << "},\n";
        file << "      \"featureSpawnCellCount\": " << record.featureSpawnCellCount << ",\n";
        file << "      \"candidateCount\": " << record.candidateCount << ",\n";
        file << "      \"rejectedOutOfBounds\": " << record.rejectedOutOfBounds << ",\n";
        file << "      \"rejectedClaimedAnchor\": " << record.rejectedClaimedAnchor << ",\n";
        file << "      \"rejectedAnchorNotCavity\": " << record.rejectedAnchorNotCavity
             << ",\n";
        file << "      \"rejectedOccupied\": " << record.rejectedOccupied << ",\n";
        file << "      \"rejectedNonEmpty\": " << record.rejectedNonEmpty << ",\n";
        file << "      \"rejectedUnsupportedFloor\": " << record.rejectedUnsupportedFloor
             << ",\n";
        file << "      \"candidatePreview\": [";
        for (size_t candidateIndex = 0; candidateIndex < record.candidatePreview.size();
             ++candidateIndex) {
            const auto &candidate = record.candidatePreview[candidateIndex];
            if (candidateIndex != 0) {
                file << ", ";
            }
            file << "{\"x\": " << candidate.x << ", \"y\": " << candidate.y << "}";
        }
        file << "],\n";
        file << "      \"spawned\": [";
        for (size_t spawnedIndex = 0; spawnedIndex < record.spawned.size(); ++spawnedIndex) {
            const auto &spawned = record.spawned[spawnedIndex];
            if (spawnedIndex != 0) {
                file << ", ";
            }
            file << "{\"x\": " << spawned.x << ", \"y\": " << spawned.y << "}";
        }
        file << "]\n";
        file << "    }";
        if (index + 1 != oilWellDebug.size()) {
            file << ",";
        }
        file << "\n";
    }
    file << "  ]\n";
    file << "}\n";
}
