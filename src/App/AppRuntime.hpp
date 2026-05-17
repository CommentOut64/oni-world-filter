#pragma once

#include <string>
#include <vector>

#include "Cluster/ClusterWorldOffsetSolver.hpp"
#include "App/ResultSink.hpp"
#include "Setting/SettingsCache.hpp"
#include "Setting/WorldEffectiveState.hpp"
#include "WorldGen.hpp"

class AppRuntime
{
public:
#if 0
    // 临时 timing 检测，先停用。
    struct SearchPhaseTiming {
        uint64_t restoreMicros = 0;
        uint64_t coordinateMicros = 0;
        uint64_t buildWorldListMicros = 0;
        uint64_t subworldMixingMicros = 0;
        uint64_t traitsMicros = 0;
        uint64_t worldGenMicros = 0;
        uint64_t worldGenSeedPointsMicros = 0;
        uint64_t worldGenDiagramMicros = 0;
        uint64_t worldGenCellProcessingMicros = 0;
        uint64_t worldGenChildrenMicros = 0;
        uint64_t worldGenTemplatesMicros = 0;
        uint64_t summaryMicros = 0;
        uint64_t totalMicros = 0;
    };
#endif

    static AppRuntime *Instance();

    void SetResultSink(ResultSink *sink);
    ResultSink *GetResultSink() const;

    void SetSkipPolygons(bool skip);
    bool IsSkippingPolygons() const;

    void Initialize(int seed);
    bool PrepareSearchWorker(const std::string &code);
    bool ResetSearchSeed(const std::string &code);
    bool GeneratePrepared(int traitsFlag);
    bool Generate(const std::string &code, int traitsFlag);
    bool GenerateSelectedPlacements(const std::string &code,
                                    int traitsFlag,
                                    const std::vector<int> &placementIndexes,
                                    int primaryPlacementIndex);
#if 0
    // 临时 timing 检测，先停用。
    const SearchPhaseTiming &LastSearchPhaseTiming() const;
#endif

private:
    AppRuntime() = default;

    bool BuildWorldList(std::vector<ResolvedWorldPlacement> &placements);
    bool GenerateCurrentState(int traitsFlag, bool genWarpWorld);
    static int FindPrimaryPlacementIndex(const std::vector<ResolvedWorldPlacement> &placements);
    static std::vector<int> CollectPreviewPlacementIndexes(
        const std::vector<ResolvedWorldPlacement> &placements,
        bool genWarpWorld);
    bool GenerateWorldsForPlacementIndexes(std::vector<ResolvedWorldPlacement> &placements,
                                           int traitsFlag,
                                           const std::vector<int> &placementIndexes,
                                           int primaryPlacementIndex);
    void SetSeedWithTraits(const std::vector<World *> &worlds, int traitsFlag);
    GeneratedWorldSummary BuildSummary(int seed,
                                       const WorldEffectiveState &state,
                                       const ClusterWorldOffset &worldOffset,
                                       std::vector<Site> &sites,
                                       const WorldGen &worldGen);
    GeneratedWorldPreview BuildPreview(World *world,
                                       std::vector<Site> &sites,
                                       const GeneratedWorldSummary &summary);
    static bool GetZonePolygon(Site &site, Polygon &polygon);

private:
    SettingsCache m_settings;
    SearchMutableStateSnapshot m_searchMutableBaseline;
    KRandom m_random{0};
    ResultSink *m_sink = nullptr;
    bool m_skipPolygons = false;
    bool m_searchWorkerPrepared = false;
    bool m_searchSeedPrepared = false;
    bool m_searchWarpWorld = false;
};
