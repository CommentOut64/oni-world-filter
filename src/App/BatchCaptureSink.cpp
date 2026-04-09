#include "App/ResultSink.hpp"

#ifndef EMSCRIPTEN
#include <fstream>

#include "config.h"
#endif

void BatchCaptureSink::SetActive(bool active)
{
    m_data.active = active;
}

void BatchCaptureSink::Reset()
{
    m_data.Reset();
}

const BatchCaptureRecord &BatchCaptureSink::Data() const
{
    return m_data;
}

bool BatchCaptureSink::RequestResource(uint32_t expectedSize, std::vector<char> &data)
{
#ifndef EMSCRIPTEN
    data.assign(expectedSize, 0);
    std::ifstream fstm(SETTING_ASSET_FILEPATH, std::ios::binary);
    if (!fstm.is_open()) {
        LogE("can not open file.");
        return false;
    }
    auto size = fstm.seekg(0, std::ios::end).tellg();
    if (size != expectedSize) {
        LogE("wrong count.");
        return false;
    }
    fstm.seekg(0).read(data.data(), expectedSize);
    return fstm.good();
#else
    (void)expectedSize;
    data.clear();
    LogE("BatchCaptureSink::RequestResource is not supported in emscripten");
    return false;
#endif
}

void BatchCaptureSink::OnGeneratedWorldSummary(const GeneratedWorldSummary &summary)
{
    if (summary.worldType == 0) {
        m_data.startX = summary.start.x;
        m_data.startY = summary.start.y;
    }
    if (m_data.worldW == 0) {
        m_data.worldW = summary.worldSize.x;
        m_data.worldH = summary.worldSize.y;
    }

    m_data.traits.clear();
    m_data.traits.reserve(summary.traits.size());
    for (const auto &item : summary.traits) {
        m_data.traits.push_back(item.id);
    }

    m_data.geysers.clear();
    m_data.geysers.reserve(summary.geysers.size());
    for (const auto &item : summary.geysers) {
        m_data.geysers.push_back({item.type, item.x, item.y});
    }
}

void BatchCaptureSink::OnGeneratedWorldPreview(const GeneratedWorldPreview &preview)
{
    (void)preview;
}
