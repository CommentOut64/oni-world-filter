#pragma once

#include <memory>

#include "Setting/SettingsCache.hpp"
#include "Utils/KRandom.hpp"
#include "Utils/Diagram.hpp"

struct TemplateSpawner {
    Vector2f position;
    const TemplateContainer *container;
};

class WorldGen
{
public:
#if 0
    // 临时 timing 检测，先停用。
    struct GenerateTiming {
        uint64_t seedPointsMicros = 0;
        uint64_t diagramMicros = 0;
        uint64_t cellProcessingMicros = 0;
        uint64_t childrenMicros = 0;
        uint64_t templatesMicros = 0;
        uint64_t totalMicros = 0;
    };
#endif

private:
    int m_seed;
    const SettingsCache &m_settings;
    const World &m_world;
    WorldPlacement *m_placement = nullptr;
    std::vector<TemplateSpawner> m_templates;

public:
    WorldGen(World &world, SettingsCache &settings)
        : m_seed{settings.seed}
        , m_settings{settings}
        , m_world{world}
    {
    }

    bool GenerateOverworld(std::vector<Site> &sites);

    std::vector<Vector3i> GetGeysers(int seed) const;
#if 0
    // 临时 timing 检测，先停用。
    const GenerateTiming &LastGenerateTiming() const;
#endif

private:
    template<typename T>
    const T &GetDefaultData(const std::string &key)
    {
        return m_settings.GetDefaultData<T>(m_world, key);
    }
    bool GenerateSeedPoints(KRandom &random, std::vector<Site> &sites);
    void PropagateDistanceTags(std::vector<Site> &sites) const;
    void ConvertUnknownCells(std::vector<Site> &allSites, KRandom &random);
    bool GenerateChildren(Site &site, KRandom &random, int seed, bool usePD);
    void SetFeatureBiome(Site &site, KRandom &random, const Feature *feature);
    bool DetermineTemplates(std::vector<Site *> &sites, KRandom &random);
};

// clang-format off
inline std::string ZoneTypeToString(ZoneType zone)
{
    const char *dict[] = {
        "FrozenWastes", "CrystalCaverns",    "BoggyMarsh",     "Sandstone",
        "ToxicJungle",  "MagmaCore",         "OilField",       "Space",
        "Ocean",        "Rust",              "Forest",         "Radioactive",
        "Swamp",        "Wasteland",         "RocketInterior", "Metallic",
        "Barren",       "Moo",               "IceCaves",       "CarrotQuarry",
        "SugarWoods",   "PrehistoricGarden", "PrehistoricRaptor",
        "PrehistoricWetlands"};
    return dict[(int)zone];
}
// clang-format on

inline std::string TempRangeToString(Range range)
{
    const char *dict[] = {
        "ExtremelyCold", "VeryVeryCold", "VeryCold",    "Cold",      "Chilly",
        "Cool",          "Mild",         "Room",        "HumanWarm", "HumanHot",
        "Hot",           "VeryHot",      "ExtremelyHot"};
    return dict[(int)range];
}
