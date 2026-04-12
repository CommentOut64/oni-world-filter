#include "Diagram.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include "Voronoi.hpp"
#include "ConvexHull.hpp"

constexpr float EPSILON = std::numeric_limits<float>::denorm_min();
constexpr double kDeterminantEpsilon = 1e-10;
constexpr float kCoordinateAbsLimit = 1000000.0f;

static bool IsFinitePoint(const Vector2f &point)
{
    return std::isfinite(point.x) && std::isfinite(point.y) &&
           std::abs(point.x) <= kCoordinateAbsLimit &&
           std::abs(point.y) <= kCoordinateAbsLimit;
}

static bool TryGetDualPoint(ConvexFace<Site> &face, Vector2f *outPoint)
{
    if (outPoint == nullptr) {
        return false;
    }
    if (!face.dualPoint.has_value()) {
        auto &vertices = face.Vertices;
        Vector3d vec1(vertices[0]->x, vertices[0]->y, vertices[0]->z);
        Vector3d vec2(vertices[1]->x, vertices[1]->y, vertices[1]->z);
        Vector3d vec3(vertices[2]->x, vertices[2]->y, vertices[2]->z);
        const double num1 =
            vec1.y * (vec2.z - vec3.z) + vec2.y * (vec3.z - vec1.z) +
            vec3.y * (vec1.z - vec2.z);
        const double num2 =
            vec1.z * (vec2.x - vec3.x) + vec2.z * (vec3.x - vec1.x) +
            vec3.z * (vec1.x - vec2.x);
        const double denominator =
            vec1.x * (vec2.y - vec3.y) + vec2.x * (vec3.y - vec1.y) +
            vec3.x * (vec1.y - vec2.y);
        if (!std::isfinite(denominator) ||
            std::abs(denominator) <= kDeterminantEpsilon) {
            return false;
        }

        const double ratio = -0.5 / denominator;
        Vector2f point((float)(num1 * ratio), (float)(num2 * ratio));
        if (!IsFinitePoint(point)) {
            return false;
        }
        face.dualPoint.emplace(point);
    }
    *outPoint = face.dualPoint.value();
    return true;
}

inline static double Det(Vector3d *m)
{
    return m[0].x * (m[1].y * m[2].z - m[2].y * m[1].z) -
           m[0].y * (m[1].x * m[2].z - m[2].x * m[1].z) +
           m[0].z * (m[1].x * m[2].y - m[2].x * m[1].y);
}

inline static double LengthSquared(double x, double y) { return x * x + y * y; }

static bool TryGetCircumcenter(ConvexFace<Site> &face, Vector2f *outPoint)
{
    if (outPoint == nullptr) {
        return false;
    }
    if (!face.circumcenter.has_value()) {
        auto &vertices = face.Vertices;
        Vector3d data[3];
        for (int i = 0; i < 3; i++) {
            data[i].x = vertices[i]->x;
            data[i].y = vertices[i]->y;
            data[i].z = 1.0;
        }
        const double determinant = Det(data);
        if (!std::isfinite(determinant) ||
            std::abs(determinant) <= kDeterminantEpsilon) {
            return false;
        }
        const double ratio = -1.0 / (2.0 * determinant);
        for (int j = 0; j < 3; j++) {
            data[j].x = LengthSquared(vertices[j]->x, vertices[j]->y);
        }
        const double num3 = 0.0 - Det(data);
        for (int k = 0; k < 3; k++) {
            data[k].y = vertices[k]->x;
        }
        const double num4 = Det(data);
        Vector2f point((float)(ratio * num3), (float)(ratio * num4));
        if (!IsFinitePoint(point)) {
            return false;
        }
        face.circumcenter.emplace(point);
    }
    *outPoint = face.circumcenter.value();
    return true;
}

static bool PolyForRandomPoints(std::vector<Vector2f> &verts)
{
    if (verts.size() < 3) {
        return false;
    }
    if (!std::ranges::all_of(verts, [](const Vector2f &point) {
            return IsFinitePoint(point);
        })) {
        return false;
    }
    ConvexHull hull;
    auto hullResult = hull.Create2D(verts);
    auto &Points = hullResult.Points;
    if (Points.size() < 3) {
        return false;
    }
    double area = 0.0;
    int count = Points.size();
    for (int i = 0; i < count; i++) {
        int index = (i + 1) % count;
        auto &vector1 = *Points[i];
        auto &vector2 = *Points[index];
        area += vector1.x * vector2.y - vector2.x * vector1.y;
    }
    if (!std::isfinite(area) || std::abs(area) <= kDeterminantEpsilon) {
        return false;
    }

    std::vector<Vector2f> result;
    if (area < 0.0) {
        for (auto itr = Points.rbegin(); itr != Points.rend(); ++itr) {
            result.emplace_back(**itr);
        }
    } else {
        for (auto itr = Points.begin(); itr != Points.end(); ++itr) {
            result.emplace_back(**itr);
        }
    }
    if (result.size() < 3 || !std::ranges::all_of(result, [](const Vector2f &point) {
            return IsFinitePoint(point);
        })) {
        return false;
    }
    verts.swap(result);
    return true;
}

static bool ContainsVert(const ConvexFace<Site> *face, const Site *target)
{
    if (face == nullptr || target == nullptr) {
        return false;
    }
    for (int i = 0; i < 3; i++) {
        if (face->Vertices[i] == target) {
            return true;
        }
    }
    return false;
}

static Edge GetEdge(ConvexFace<Site> &face0, ConvexFace<Site> &face1)
{
    Edge edge;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            if (face0.Vertices[i] == face1.Vertices[j]) {
                if (edge.leftSite == nullptr) {
                    edge.leftSite = face0.Vertices[i];
                } else {
                    edge.rightSite = face0.Vertices[i];
                }
            }
        }
    }
    return edge;
}

static void TouchingFaces(Site *site, ConvexFace<Site> &startingFace,
                          std::vector<ConvexFace<Site> *> &result)
{
    result.clear();
    std::stack<ConvexFace<Site> *> stack;
    stack.push(&startingFace);
    while (!stack.empty()) {
        auto convexFaceExt = stack.top();
        stack.pop();
        if (!ContainsVert(convexFaceExt, site) ||
            std::ranges::find(result, convexFaceExt) != result.end()) {
            continue;
        }
        result.push_back(convexFaceExt);
        for (auto face : convexFaceExt->Adjacency) {
            if (ContainsVert(face, site)) {
                stack.push(face);
            }
        }
    }
}

static std::vector<Site *> GenerateNeighbors(Site *site, ConvexFace<Site> &startFace)
{
    std::vector<Site *> list;
    std::vector<ConvexFace<Site> *> list2;
    std::stack<ConvexFace<Site> *> stack;
    stack.push(&startFace);
    while (!stack.empty()) {
        auto convexFaceExt = stack.top();
        stack.pop();
        list2.push_back(convexFaceExt);
        for (auto face : convexFaceExt->Adjacency) {
            if (ContainsVert(face, site) &&
                std::ranges::find(list2, face) == list2.end()) {
                auto edge = GetEdge(*convexFaceExt, *face);
                auto dualSite3d =
                    ((edge.leftSite == site) ? edge.rightSite : edge.leftSite);
                list.push_back(dualSite3d);
                stack.push(face);
            }
        }
    }
    return list;
}

static void FilterNeighbours(Site &home)
{
    auto itr = home.neighbours.begin();
    while (itr != home.neighbours.end()) {
        auto site = *itr;
        if (site->dummy || !site->polygon.SimpleSharesEdge(home.polygon)) {
            itr = home.neighbours.erase(itr);
        } else {
            ++itr;
        }
    }
}

Diagram::Diagram(Polygon &bounds, std::vector<Site> &sites)
    : m_bounds{bounds}
    , m_sites{sites}
{
    for (auto &site : m_sites) {
        m_weightSum += site.weight;
    }
}

bool Diagram::MakeVD(const Rect &bounds)
{
    Voronoi voronoi;
    voronoi.Create(m_sites, bounds);
    for (auto &site : m_sites) {
        if (site.dummy) {
            continue;
        }
        voronoi.BuildRegion(site);
        voronoi.FindNeighborSites(site);
        site.polygon.Intersect(m_bounds);
        if (site.polygon.Vertices.size() < 3 ||
            !std::ranges::all_of(site.polygon.Vertices, [](const Vector2f &point) {
                return IsFinitePoint(point);
            })) {
            return false;
        }
    }
    for (auto &site : m_sites) {
        site.UpdatePosition();
    }
    return true;
}

bool Diagram::ComputeNode()
{
    if (m_sites.size() == 1) {
        m_sites[0].polygon = m_bounds;
        m_sites[0].UpdatePosition();
        return true;
    }
    size_t originSize = m_sites.size();
    Rect bounds = m_bounds.Bounds();
    auto minCorner = bounds.MinCorner();
    auto maxCorner = bounds.MaxCorner();
    auto center = bounds.Center();
    auto index = (uint16_t)(originSize + 1);
    m_sites.emplace_back(index++, minCorner.x - 500.0f, center.y);
    m_sites.back().dummy = true;
    m_sites.emplace_back(index++, maxCorner.x + 500.0f, center.y);
    m_sites.back().dummy = true;
    m_sites.emplace_back(index++, center.x, minCorner.y - 500.0f);
    m_sites.back().dummy = true;
    m_sites.emplace_back(index++, center.x, maxCorner.y + 500.0f);
    m_sites.back().dummy = true;
    bounds.x -= 500.0f;
    bounds.y -= 500.0f;
    bounds.width += 500.0f;
    bounds.height += 500.0f;
    if (!MakeVD(bounds)) {
        m_sites.resize(originSize);
        return false;
    }
    m_sites.resize(originSize);
    for (auto &site : m_sites) {
        FilterNeighbours(site);
    }
    return true;
}

bool Diagram::ComputeVD()
{
    ConvexHull hull;
    auto hullResult = hull.CreateDelaunay(m_sites);
    if (hullResult.Faces.empty()) {
        return false;
    }
    std::vector<ConvexFace<Site> *> roundFaces;
    for (auto &cell : hullResult.Faces) {
        Vector2f cellCenter;
        if (!TryGetCircumcenter(cell, &cellCenter)) {
            return false;
        }
        for (auto site : cell.Vertices) {
            if (site->dummy || site->visited) {
                continue;
            }
            site->visited = true;
            site->polygon.Clear();
            TouchingFaces(site, cell, roundFaces);
            for (auto item : roundFaces) {
                Vector2f center;
                if (!TryGetCircumcenter(*item, &center)) {
                    return false;
                }
                site->polygon.Vertices.emplace_back(center);
            }
            if (!PolyForRandomPoints(site->polygon.Vertices)) {
                return false;
            }
            site->polygon.Intersect(m_bounds);
            if (site->polygon.Vertices.size() < 3 ||
                !std::ranges::all_of(site->polygon.Vertices,
                                     [](const Vector2f &point) {
                                         return IsFinitePoint(point);
                                     })) {
                return false;
            }
        }
    }
    for (auto &site : m_sites) {
        site.neighbours.clear();
        site.UpdatePosition();
    }
    return true;
}

bool Diagram::ComputePD()
{
    ConvexHull hull;
    auto hullResult = hull.Create(m_sites);
    if (hullResult.Faces.empty()) {
        return false;
    }
    std::vector<ConvexFace<Site> *> roundFaces;
    for (auto &face : hullResult.Faces) {
        if (face.Normal[2] >= -EPSILON) {
            continue;
        }
        for (auto site : face.Vertices) {
            if (site->dummy || site->visited) {
                continue;
            }
            site->visited = true;
            site->neighbours.clear();
            site->polygon.Clear();
            TouchingFaces(site, face, roundFaces);
            site->neighbours = GenerateNeighbors(site, face);
            for (auto item : roundFaces) {
                Vector2f center;
                if (!TryGetDualPoint(*item, &center)) {
                    return false;
                }
                site->polygon.Vertices.emplace_back(center);
            }
            if (!PolyForRandomPoints(site->polygon.Vertices)) {
                return false;
            }
            site->polygon.Intersect(m_bounds);
            if (site->polygon.Vertices.size() < 3 ||
                !std::ranges::all_of(site->polygon.Vertices,
                                     [](const Vector2f &point) {
                                         return IsFinitePoint(point);
                                     })) {
                return false;
            }
        }
    }
    for (auto &site : m_sites) {
        site.UpdatePosition();
    }
    return true;
}

bool Diagram::ComputePowerDiagram()
{
    for (int i = 0; i <= 500; i++) {
        if (!UpdateWeights()) {
            return false;
        }
        if (!ComputePD()) {
            return false;
        }
        float max = 0.0f;
        for (auto &site : m_sites) {
            if (!site.dummy) {
                float area1 = site.polygon.Area();
                float area2 = site.weight / m_weightSum * m_bounds.Area();
                if (!std::isfinite(area1) || !std::isfinite(area2) ||
                    area1 <= EPSILON || area2 <= EPSILON) {
                    return false;
                }
                float ratio = std::abs(area1 - area2) / area2;
                if (!std::isfinite(ratio)) {
                    return false;
                }
                if (max < ratio) {
                    max = ratio;
                }
            }
        }
        if (max < 0.2f) {
            break;
        }
    }
    return true;
}

bool Diagram::UpdateWeights()
{
    float min = 0.0f;
    int externCount = m_bounds.Vertices.size() * 2;
    auto sites = m_sites | std::views::take(m_sites.size() - externCount);
    for (auto &site : sites) {
        site.visited = false;
        if (site.currentWeight < 1.0f) {
            site.currentWeight = 1.0f;
        }
        float area1 = site.polygon.Area();
        float area2 = site.weight / (double)m_weightSum * m_bounds.Area();
        if (!std::isfinite(area1) || !std::isfinite(area2) || area1 <= EPSILON ||
            area2 <= EPSILON) {
            return false;
        }
        float ratio = area2 / area1;
        if (!std::isfinite(ratio) || ratio <= 0.0f) {
            return false;
        }
        if ((ratio > 1.1f && site.previousWeightAdaption < 0.9f) ||
            (ratio < 0.9f && site.previousWeightAdaption > 1.1f)) {
            ratio = std::sqrt(ratio);
        }
        if (ratio < 1.1f && ratio > 0.9f && site.currentWeight != 1.0f) {
            ratio = std::sqrt(ratio);
        }
        if (site.currentWeight < 10.0f) {
            ratio *= ratio;
        }
        if (site.currentWeight > 10.0f) {
            ratio = std::sqrt(ratio);
        }
        site.previousWeightAdaption = ratio;
        site.currentWeight *= ratio;
        if (site.currentWeight < 1.0f) {
            float radius1 = std::sqrt(area1 / std::numbers::pi_v<float>);
            float radius2 = std::sqrt(area2 / std::numbers::pi_v<float>);
            float diff = std::sqrt(site.currentWeight) - (radius1 - radius2);
            if (diff < 0.0f) {
                site.currentWeight = 0.0f - diff * diff;
                if (site.currentWeight < min) {
                    min = site.currentWeight;
                }
            }
        }
    }
    if (min < 0.0f) {
        min = -min;
        for (auto &site : sites) {
            site.currentWeight += min + 1.0;
        }
    }
    min = 1.0f;
    for (auto &site : sites) {
        if (site.neighbours.empty()) {
            float dist = site.x * site.x + site.y * site.y;
            float factor = dist / (std::abs(site.currentWeight) + 1.0f);
            if (!std::isfinite(factor)) {
                return false;
            }
            if (factor < min) {
                min = factor;
            }
        }
        for (auto &neighbour : site.neighbours) {
            float diff = site.currentWeight - neighbour->currentWeight;
            float dist = site.DistanceSquared(*neighbour);
            float factor = dist / (std::abs(diff) + 1.0f);
            if (!std::isfinite(factor)) {
                return false;
            }
            if (factor < min) {
                min = factor;
            }
        }
    }
    for (auto &site : sites) {
        site.currentWeight *= min;
        site.UpdatePosition();
    }
    return true;
}

bool Diagram::ComputeNodePD()
{
    size_t originSize = m_sites.size();
    auto cleanup = [&]() {
        m_sites.resize(originSize);
    };
    auto &centroid = m_bounds.Centroid();
    auto index = (uint16_t)(originSize + 1);
    m_weightSum = 0.0f;
    for (auto &site : m_sites) {
        site.visited = false;
        site.currentWeight = site.weight;
        site.previousWeightAdaption = 0.0f;
        m_weightSum += site.weight;
    }
    int j = 1;
    for (int i = 0; i < (int)m_bounds.Vertices.size(); j++, i++) {
        if (j == (int)m_bounds.Vertices.size())
            j = 0;
        auto &point1 = m_bounds.Vertices[i];
        auto &point2 = m_bounds.Vertices[j];
        auto point3 = (point2 - point1) * 0.5f + point2;
        auto extPoint1 = point1 + (point1 - centroid).Normalized() * 1000.0f;
        auto extPoint2 = point2 + (point3 - centroid).Normalized() * 1000.0f;
        m_sites.emplace_back(index++, extPoint1, EPSILON);
        m_sites.back().dummy = true;
        m_sites.emplace_back(index++, extPoint2, EPSILON);
        m_sites.back().dummy = true;
    }
    if (!ComputeVD()) {
        cleanup();
        return false;
    }
    if (!ComputePowerDiagram()) {
        cleanup();
        return false;
    }
    cleanup();
    for (auto &site : m_sites) {
        FilterNeighbours(site);
    }
    return true;
}
