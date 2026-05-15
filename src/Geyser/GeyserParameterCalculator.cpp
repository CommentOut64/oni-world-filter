#include "Geyser/GeyserParameterCalculator.hpp"

#include <array>
#include <cmath>
#include <string_view>
#include <utility>

#include "Batch/FilterConfig.hpp"
#include "Utils/KRandom.hpp"

namespace GeyserCalc {

namespace {

struct GeyserTypeSpec {
    const char *id;
    float temperatureKelvin;
    float minRatePerCycle;
    float maxRatePerCycle;
    float maxPressure;
    float minIterationLength;
    float maxIterationLength;
    float minIterationPercent;
    float maxIterationPercent;
    float minYearLength;
    float maxYearLength;
    float minYearPercent;
    float maxYearPercent;
};

constexpr std::array<GeyserTypeSpec, 26> kGeyserTypeSpecs = {
    GeyserTypeSpec{"steam", 383.15f, 1000.0f, 2000.0f, 5.0f, 60.0f, 1140.0f, 0.1f, 0.9f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GeyserTypeSpec{"hot_steam", 773.15f, 500.0f, 1000.0f, 5.0f, 60.0f, 1140.0f, 0.1f, 0.9f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GeyserTypeSpec{"hot_water", 368.15f, 2000.0f, 4000.0f, 500.0f, 60.0f, 1140.0f, 0.1f, 0.9f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GeyserTypeSpec{"slush_water", 263.15f, 1000.0f, 2000.0f, 500.0f, 60.0f, 1140.0f, 0.1f, 0.9f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GeyserTypeSpec{"filthy_water", 303.15f, 2000.0f, 4000.0f, 500.0f, 60.0f, 1140.0f, 0.1f, 0.9f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GeyserTypeSpec{"slush_salt_water", 263.15f, 1000.0f, 2000.0f, 500.0f, 60.0f, 1140.0f, 0.1f, 0.9f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GeyserTypeSpec{"salt_water", 368.15f, 2000.0f, 4000.0f, 500.0f, 60.0f, 1140.0f, 0.1f, 0.9f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GeyserTypeSpec{"small_volcano", 2000.0f, 400.0f, 800.0f, 150.0f, 6000.0f, 12000.0f, 0.005f, 0.01f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GeyserTypeSpec{"big_volcano", 2000.0f, 800.0f, 1600.0f, 150.0f, 6000.0f, 12000.0f, 0.005f, 0.01f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GeyserTypeSpec{"liquid_co2", 218.0f, 100.0f, 200.0f, 50.0f, 60.0f, 1140.0f, 0.1f, 0.9f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GeyserTypeSpec{"hot_co2", 773.15f, 70.0f, 140.0f, 5.0f, 60.0f, 1140.0f, 0.1f, 0.9f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GeyserTypeSpec{"hot_hydrogen", 773.15f, 70.0f, 140.0f, 5.0f, 60.0f, 1140.0f, 0.1f, 0.9f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GeyserTypeSpec{"hot_po2", 773.15f, 70.0f, 140.0f, 5.0f, 60.0f, 1140.0f, 0.1f, 0.9f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GeyserTypeSpec{"slimy_po2", 333.15f, 70.0f, 140.0f, 5.0f, 60.0f, 1140.0f, 0.1f, 0.9f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GeyserTypeSpec{"chlorine_gas", 333.15f, 70.0f, 140.0f, 5.0f, 60.0f, 1140.0f, 0.1f, 0.9f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GeyserTypeSpec{"methane", 423.15f, 70.0f, 140.0f, 5.0f, 60.0f, 1140.0f, 0.1f, 0.9f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GeyserTypeSpec{"molten_copper", 2500.0f, 200.0f, 400.0f, 150.0f, 480.0f, 1080.0f, 1.0f / 60.0f, 0.1f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GeyserTypeSpec{"molten_iron", 2800.0f, 200.0f, 400.0f, 150.0f, 480.0f, 1080.0f, 1.0f / 60.0f, 0.1f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GeyserTypeSpec{"molten_gold", 2900.0f, 200.0f, 400.0f, 150.0f, 480.0f, 1080.0f, 1.0f / 60.0f, 0.1f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GeyserTypeSpec{"molten_aluminum", 2000.0f, 200.0f, 400.0f, 150.0f, 480.0f, 1080.0f, 1.0f / 60.0f, 0.1f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GeyserTypeSpec{"molten_tungsten", 4000.0f, 200.0f, 400.0f, 150.0f, 480.0f, 1080.0f, 1.0f / 60.0f, 0.1f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GeyserTypeSpec{"molten_niobium", 3500.0f, 800.0f, 1600.0f, 150.0f, 6000.0f, 12000.0f, 0.005f, 0.01f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GeyserTypeSpec{"molten_cobalt", 2500.0f, 200.0f, 400.0f, 150.0f, 480.0f, 1080.0f, 1.0f / 60.0f, 0.1f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GeyserTypeSpec{"oil_drip", 600.0f, 1.0f, 250.0f, 50.0f, 600.0f, 600.0f, 1.0f, 1.0f, 100.0f, 500.0f, 0.4f, 0.8f},
    GeyserTypeSpec{"liquid_sulfur", 438.34998f, 1000.0f, 2000.0f, 500.0f, 60.0f, 1140.0f, 0.1f, 0.9f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GeyserTypeSpec{"chlorine_gas_cool", 278.15f, 70.0f, 140.0f, 5.0f, 60.0f, 1140.0f, 0.1f, 0.9f, 15000.0f, 135000.0f, 0.4f, 0.8f},
};

const GeyserTypeSpec *FindGeyserTypeSpec(std::string_view id)
{
    for (const auto &spec : kGeyserTypeSpecs) {
        if (id == spec.id) {
            return &spec;
        }
    }
    return nullptr;
}

std::string_view ResolveGeyserId(int type)
{
    const auto &ids = Batch::GetGeyserIds();
    if (type < 0 || type >= static_cast<int>(ids.size())) {
        return {};
    }
    return ids[static_cast<size_t>(type)];
}

std::string ResolveParameterKind(int type)
{
    const auto id = ResolveGeyserId(type);
    if (id.empty()) {
        return "unknown";
    }
    if (FindGeyserTypeSpec(id) != nullptr) {
        return "geyser";
    }
    if (id == "oil_reservoir") {
        return "reservoir";
    }
    if (id == "printing_pod" || id == "warp_sender" || id == "warp_receiver" ||
        id == "warp_portal" || id == "cryo_tank") {
        return "facility";
    }
    return "unknown";
}

float Resample(float t, float min, float max)
{
    constexpr float kSlope = 6.0f;
    constexpr float kBias = 0.002472623f;
    const float normalized = t * (1.0f - kBias * 2.0f) + kBias;
    return ((0.0f - std::log(1.0f / normalized - 1.0f) + kSlope) / (kSlope * 2.0f)) *
               (max - min) +
           min;
}

GeyserDetail MakeNoParameterDetail(int index, const GeyserSummary &summary, std::string kind)
{
    GeyserDetail detail;
    detail.index = index;
    detail.summary = summary;
    detail.parameterKind = std::move(kind);
    detail.hasParameters = false;
    return detail;
}

GeyserDetail MakeParameterDetail(int index,
                                 const GeyserSummary &summary,
                                 const GeyserTypeSpec &spec,
                                 int geyserSeed,
                                 int worldHeight,
                                 int worldOffsetX,
                                 int worldOffsetY)
{
    KRandom random(geyserSeed + (summary.x + worldOffsetX) +
                   ((worldHeight - summary.y) + worldOffsetY));

    const float rateRoll = random.NextSingle();
    const float iterationLengthRoll = random.NextSingle();
    const float iterationPercentRoll = random.NextSingle();
    const float yearLengthRoll = random.NextSingle();
    const float yearPercentRoll = random.NextSingle();

    GeyserDetail detail;
    detail.index = index;
    detail.summary = summary;
    detail.hasParameters = true;
    detail.parameterKind = "geyser";
    detail.native.averageActiveYieldKgPerCycle = Resample(rateRoll, spec.minRatePerCycle, spec.maxRatePerCycle);
    detail.native.eruptionPeriodSeconds = Resample(iterationLengthRoll, spec.minIterationLength, spec.maxIterationLength);
    detail.native.eruptionRatio = Resample(iterationPercentRoll, spec.minIterationPercent, spec.maxIterationPercent);
    detail.native.activePeriodSeconds = Resample(yearLengthRoll, spec.minYearLength, spec.maxYearLength);
    detail.native.activeRatio = Resample(yearPercentRoll, spec.minYearPercent, spec.maxYearPercent);

    detail.derived.eruptionSeconds = detail.native.eruptionPeriodSeconds * detail.native.eruptionRatio;
    detail.derived.activeSeconds = detail.native.activePeriodSeconds * detail.native.activeRatio;
    detail.derived.activeCycles = detail.derived.activeSeconds / 600.0f;
    detail.derived.totalCycles = detail.native.activePeriodSeconds / 600.0f;
    detail.derived.eruptionRateKgPerSecond = detail.native.averageActiveYieldKgPerCycle /
                                             (600.0f / detail.native.eruptionPeriodSeconds) /
                                             detail.derived.eruptionSeconds;
    detail.derived.averageOverallYieldGPerSecond =
        ((detail.derived.activeSeconds / detail.native.eruptionPeriodSeconds) *
         (detail.derived.eruptionRateKgPerSecond * detail.derived.eruptionSeconds) /
         detail.native.activePeriodSeconds) *
        1000.0f;
    detail.derived.temperatureCelsius = spec.temperatureKelvin - 273.15f;
    return detail;
}

} // namespace

std::vector<GeyserDetail> BuildGeyserDetails(int geyserSeed,
                                             int worldHeight,
                                             const std::vector<GeyserSummary> &geysers,
                                             int worldOffsetX,
                                             int worldOffsetY)
{
    std::vector<GeyserDetail> details;
    details.reserve(geysers.size());
    for (int index = 0; index < static_cast<int>(geysers.size()); ++index) {
        const auto &summary = geysers[static_cast<size_t>(index)];
        const auto kind = ResolveParameterKind(summary.type);
        const auto id = ResolveGeyserId(summary.type);
        const auto *spec = id.empty() ? nullptr : FindGeyserTypeSpec(id);
        if (spec == nullptr) {
            details.push_back(MakeNoParameterDetail(index, summary, std::move(kind)));
            continue;
        }
        details.push_back(
            MakeParameterDetail(index, summary, *spec, geyserSeed, worldHeight, worldOffsetX, worldOffsetY));
    }
    return details;
}

WorldReportData BuildWorldReportData(const GeneratedWorldPreview &preview,
                                     int geyserSeed,
                                     int mixing,
                                     const std::string &coord,
                                     int worldOffsetX,
                                     int worldOffsetY)
{
    WorldReportData report;
    report.preview = preview;
    report.geyserDetails = BuildGeyserDetails(geyserSeed,
                                              preview.summary.worldSize.y,
                                              preview.summary.geysers,
                                              worldOffsetX,
                                              worldOffsetY);
    report.mixing = mixing;
    report.coord = coord;
    return report;
}

} // namespace GeyserCalc
