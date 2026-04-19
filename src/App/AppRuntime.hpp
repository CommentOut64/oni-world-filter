#pragma once

#include <string>
#include <vector>

#include "App/ResultSink.hpp"
#include "Setting/SettingsCache.hpp"
#include "WorldGen.hpp"

class AppRuntime
{
public:
    static AppRuntime *Instance();

    void SetResultSink(ResultSink *sink);
    ResultSink *GetResultSink() const;

    void SetSkipPolygons(bool skip);
    bool IsSkippingPolygons() const;

    void Initialize(int seed);
    bool Generate(const std::string &code, int traitsFlag);

private:
    AppRuntime() = default;

    void SetSeedWithTraits(const std::vector<World *> &worlds, int traitsFlag);
    GeneratedWorldSummary BuildSummary(int seed,
                                       World *world,
                                       std::vector<Site> &sites,
                                       const std::vector<const WorldTrait *> &traits,
                                       const WorldGen &worldGen);
    GeneratedWorldPreview BuildPreview(World *world,
                                       std::vector<Site> &sites,
                                       const GeneratedWorldSummary &summary);
    static bool GetZonePolygon(Site &site, Polygon &polygon);

private:
    SettingsCache m_settings;
    KRandom m_random{0};
    ResultSink *m_sink = nullptr;
    bool m_skipPolygons = false;
};
