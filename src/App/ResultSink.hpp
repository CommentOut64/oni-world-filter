#pragma once

#include <cstdint>
#include <vector>

#include "App/ResultModels.hpp"

enum class ResultType : uint32_t {
    Starting = 0,
    Trait = 1,
    Geyser = 2,
    Polygon = 3,
    WorldSize = 4,
    Resource = 5
};

class ResultSink
{
public:
    virtual ~ResultSink() = default;

    virtual bool RequestResource(uint32_t expectedSize, std::vector<char> &data) = 0;
    virtual void OnGeneratedWorldSummary(const GeneratedWorldSummary &summary) = 0;
    virtual void OnGeneratedWorldPreview(const GeneratedWorldPreview &preview) = 0;
};

struct BatchCaptureRecord {
    bool active = false;
    int startX = 0;
    int startY = 0;
    int worldW = 0;
    int worldH = 0;

    struct Geyser {
        int type{};
        int x{};
        int y{};
    };

    std::vector<Geyser> geysers;
    std::vector<int> traits;

    void Reset()
    {
        geysers.clear();
        traits.clear();
        startX = 0;
        startY = 0;
        worldW = 0;
        worldH = 0;
    }
};

class WasmResultSink final : public ResultSink
{
public:
    bool RequestResource(uint32_t expectedSize, std::vector<char> &data) override;
    void OnGeneratedWorldSummary(const GeneratedWorldSummary &summary) override;
    void OnGeneratedWorldPreview(const GeneratedWorldPreview &preview) override;
};

class BatchCaptureSink final : public ResultSink
{
public:
    void SetActive(bool active);
    void Reset();
    const BatchCaptureRecord &Data() const;

    bool RequestResource(uint32_t expectedSize, std::vector<char> &data) override;
    void OnGeneratedWorldSummary(const GeneratedWorldSummary &summary) override;
    void OnGeneratedWorldPreview(const GeneratedWorldPreview &preview) override;

private:
    BatchCaptureRecord m_data;
};
