#include "App/ResultSink.hpp"

#include <vector>

extern "C" void jsExchangeData(uint32_t type, uint32_t count, size_t data);

bool WasmResultSink::RequestResource(uint32_t expectedSize, std::vector<char> &data)
{
    data.assign(expectedSize, 0);
    jsExchangeData((uint32_t)ResultType::Resource, expectedSize, (size_t)data.data());
    return true;
}

void WasmResultSink::OnGeneratedWorldSummary(const GeneratedWorldSummary &summary)
{
    auto starting = summary.start;
    auto worldSize = summary.worldSize;
    jsExchangeData((uint32_t)ResultType::Starting,
                   (uint32_t)summary.worldType,
                   (size_t)&starting);
    jsExchangeData((uint32_t)ResultType::WorldSize,
                   (uint32_t)summary.seed,
                   (size_t)&worldSize);

    std::vector<int> traits;
    traits.reserve(summary.traits.size());
    for (const auto &item : summary.traits) {
        traits.push_back(item.id);
    }
    jsExchangeData((uint32_t)ResultType::Trait,
                   (uint32_t)traits.size(),
                   (size_t)traits.data());

    std::vector<int> geysers;
    geysers.reserve(summary.geysers.size() * 3);
    for (const auto &item : summary.geysers) {
        geysers.push_back(item.type);
        geysers.push_back(item.x);
        geysers.push_back(item.y);
    }
    jsExchangeData((uint32_t)ResultType::Geyser,
                   (uint32_t)geysers.size(),
                   (size_t)geysers.data());
}

void WasmResultSink::OnGeneratedWorldPreview(const GeneratedWorldPreview &preview)
{
    std::vector<int> encoded;
    for (const auto &polygon : preview.polygons) {
        encoded.push_back(polygon.hasHole ? 1 : 0);
        encoded.push_back(polygon.zoneType);
        encoded.push_back((int)polygon.vertices.size());
        for (const auto &vertex : polygon.vertices) {
            encoded.push_back(vertex.x);
            encoded.push_back(vertex.y);
        }
    }
    jsExchangeData((uint32_t)ResultType::Polygon,
                   (uint32_t)encoded.size(),
                   (size_t)encoded.data());
}
