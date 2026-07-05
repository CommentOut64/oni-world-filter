#include "Geyser/GeyserParameterProvider.hpp"

#include <array>
#include <cmath>
#include <string_view>
#include <utility>

#include "Geyser/GeyserCatalog.hpp"
#include "Utils/KRandom.hpp"

namespace Geyser {

namespace {

struct GenericParameterSpec {
    std::string_view key;
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

constexpr std::array<GenericParameterSpec, 27> kGenericSpecs = {
    GenericParameterSpec{"steam", 383.15f, 1000.0f, 2000.0f, 5.0f, 60.0f, 1140.0f, 0.1f, 0.9f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GenericParameterSpec{"hot_steam", 773.15f, 500.0f, 1000.0f, 5.0f, 60.0f, 1140.0f, 0.1f, 0.9f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GenericParameterSpec{"hot_water", 368.15f, 2000.0f, 4000.0f, 500.0f, 60.0f, 1140.0f, 0.1f, 0.9f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GenericParameterSpec{"slush_water", 263.15f, 1000.0f, 2000.0f, 500.0f, 60.0f, 1140.0f, 0.1f, 0.9f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GenericParameterSpec{"filthy_water", 303.15f, 2000.0f, 4000.0f, 500.0f, 60.0f, 1140.0f, 0.1f, 0.9f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GenericParameterSpec{"slush_salt_water", 263.15f, 1000.0f, 2000.0f, 500.0f, 60.0f, 1140.0f, 0.1f, 0.9f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GenericParameterSpec{"salt_water", 368.15f, 2000.0f, 4000.0f, 500.0f, 60.0f, 1140.0f, 0.1f, 0.9f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GenericParameterSpec{"small_volcano", 2000.0f, 400.0f, 800.0f, 150.0f, 6000.0f, 12000.0f, 0.005f, 0.01f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GenericParameterSpec{"big_volcano", 2000.0f, 800.0f, 1600.0f, 150.0f, 6000.0f, 12000.0f, 0.005f, 0.01f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GenericParameterSpec{"liquid_co2", 218.0f, 100.0f, 200.0f, 50.0f, 60.0f, 1140.0f, 0.1f, 0.9f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GenericParameterSpec{"hot_co2", 773.15f, 70.0f, 140.0f, 5.0f, 60.0f, 1140.0f, 0.1f, 0.9f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GenericParameterSpec{"hot_hydrogen", 773.15f, 70.0f, 140.0f, 5.0f, 60.0f, 1140.0f, 0.1f, 0.9f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GenericParameterSpec{"hot_po2", 773.15f, 70.0f, 140.0f, 5.0f, 60.0f, 1140.0f, 0.1f, 0.9f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GenericParameterSpec{"slimy_po2", 333.15f, 70.0f, 140.0f, 5.0f, 60.0f, 1140.0f, 0.1f, 0.9f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GenericParameterSpec{"chlorine_gas", 333.15f, 70.0f, 140.0f, 5.0f, 60.0f, 1140.0f, 0.1f, 0.9f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GenericParameterSpec{"methane", 423.15f, 70.0f, 140.0f, 5.0f, 60.0f, 1140.0f, 0.1f, 0.9f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GenericParameterSpec{"molten_copper", 2500.0f, 200.0f, 400.0f, 150.0f, 480.0f, 1080.0f, 1.0f / 60.0f, 0.1f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GenericParameterSpec{"molten_iron", 2800.0f, 200.0f, 400.0f, 150.0f, 480.0f, 1080.0f, 1.0f / 60.0f, 0.1f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GenericParameterSpec{"molten_gold", 2900.0f, 200.0f, 400.0f, 150.0f, 480.0f, 1080.0f, 1.0f / 60.0f, 0.1f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GenericParameterSpec{"molten_aluminum", 2000.0f, 200.0f, 400.0f, 150.0f, 480.0f, 1080.0f, 1.0f / 60.0f, 0.1f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GenericParameterSpec{"molten_tungsten", 4000.0f, 200.0f, 400.0f, 150.0f, 480.0f, 1080.0f, 1.0f / 60.0f, 0.1f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GenericParameterSpec{"molten_niobium", 3500.0f, 800.0f, 1600.0f, 150.0f, 6000.0f, 12000.0f, 0.005f, 0.01f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GenericParameterSpec{"molten_cobalt", 2500.0f, 200.0f, 400.0f, 150.0f, 480.0f, 1080.0f, 1.0f / 60.0f, 0.1f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GenericParameterSpec{"oil_drip", 600.0f, 1.0f, 250.0f, 50.0f, 600.0f, 600.0f, 1.0f, 1.0f, 100.0f, 500.0f, 0.4f, 0.8f},
    GenericParameterSpec{"liquid_sulfur", 438.34998f, 1000.0f, 2000.0f, 500.0f, 60.0f, 1140.0f, 0.1f, 0.9f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GenericParameterSpec{"chlorine_gas_cool", 278.15f, 70.0f, 140.0f, 5.0f, 60.0f, 1140.0f, 0.1f, 0.9f, 15000.0f, 135000.0f, 0.4f, 0.8f},
    GenericParameterSpec{"murky_brine", 368.15f, 2000.0f, 4000.0f, 500.0f, 60.0f, 1140.0f, 0.1f, 0.9f, 15000.0f, 135000.0f, 0.4f, 0.8f},
};

const GenericParameterSpec *FindGenericSpec(std::string_view key)
{
    for (const auto &spec : kGenericSpecs) {
        if (spec.key == key) {
            return &spec;
        }
    }
    return nullptr;
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

std::string ResolveParameterKind(const CatalogEntry &entry)
{
    switch (entry.kind) {
    case GeyserKind::Reservoir:
        return "reservoir";
    case GeyserKind::WarpPortal:
    case GeyserKind::Cryopod:
    case GeyserKind::Facility:
        return "facility";
    case GeyserKind::Aquatic:
        return entry.usesGenericParameterAlgorithm ? "aquatic_geyser" : "aquatic_vent";
    case GeyserKind::FixedTemplateGeyser:
    case GeyserKind::GenericGeyser:
        return "geyser";
    default:
        return "unknown";
    }
}

void FillNoParameterDetail(const CatalogEntry &entry,
                           int index,
                           const GeyserSummary &summary,
                           GeyserDetail *detail)
{
    if (detail == nullptr) {
        return;
    }
    detail->index = index;
    detail->summary = summary;
    detail->parameterKind = ResolveParameterKind(entry);
    detail->parameterSource = std::string(entry.parameterSource);
    detail->hasParameters = false;
}

void FillGenericParameterDetail(const CatalogEntry &entry,
                                int index,
                                const GeyserSummary &summary,
                                const GenericParameterSpec &spec,
                                int geyserSeed,
                                int worldHeight,
                                int worldOffsetX,
                                int worldOffsetY,
                                GeyserDetail *detail)
{
    if (detail == nullptr) {
        return;
    }

    (void)worldHeight;
    KRandom random(geyserSeed + (summary.x + worldOffsetX) +
                   (summary.y + worldOffsetY));

    const float rateRoll = random.NextSingle();
    const float iterationLengthRoll = random.NextSingle();
    const float iterationPercentRoll = random.NextSingle();
    const float yearLengthRoll = random.NextSingle();
    const float yearPercentRoll = random.NextSingle();

    detail->index = index;
    detail->summary = summary;
    detail->hasParameters = true;
    detail->parameterKind = ResolveParameterKind(entry);
    detail->parameterSource = std::string(entry.parameterSource);
    detail->native.averageActiveYieldKgPerCycle =
        Resample(rateRoll, spec.minRatePerCycle, spec.maxRatePerCycle);
    detail->native.eruptionPeriodSeconds =
        Resample(iterationLengthRoll, spec.minIterationLength, spec.maxIterationLength);
    detail->native.eruptionRatio =
        Resample(iterationPercentRoll, spec.minIterationPercent, spec.maxIterationPercent);
    detail->native.activePeriodSeconds =
        Resample(yearLengthRoll, spec.minYearLength, spec.maxYearLength);
    detail->native.activeRatio =
        Resample(yearPercentRoll, spec.minYearPercent, spec.maxYearPercent);

    detail->derived.eruptionSeconds =
        detail->native.eruptionPeriodSeconds * detail->native.eruptionRatio;
    detail->derived.activeSeconds =
        detail->native.activePeriodSeconds * detail->native.activeRatio;
    detail->derived.activeCycles = detail->derived.activeSeconds / 600.0f;
    detail->derived.totalCycles = detail->native.activePeriodSeconds / 600.0f;
    detail->derived.eruptionRateKgPerSecond =
        detail->native.averageActiveYieldKgPerCycle /
        (600.0f / detail->native.eruptionPeriodSeconds) /
        detail->derived.eruptionSeconds;
    detail->derived.averageOverallYieldGPerSecond =
        ((detail->derived.activeSeconds / detail->native.eruptionPeriodSeconds) *
         (detail->derived.eruptionRateKgPerSecond * detail->derived.eruptionSeconds) /
         detail->native.activePeriodSeconds) *
        1000.0f;
    detail->derived.temperatureCelsius = spec.temperatureKelvin - 273.15f;
}

} // namespace

bool BuildDetailFromCatalog(const CatalogEntry *entry,
                            int index,
                            const GeyserSummary &summary,
                            int geyserSeed,
                            int worldHeight,
                            int worldOffsetX,
                            int worldOffsetY,
                            GeyserDetail *detail)
{
    if (detail == nullptr) {
        return false;
    }
    *detail = GeyserDetail{};
    if (entry == nullptr) {
        detail->index = index;
        detail->summary = summary;
        detail->parameterKind = "unknown";
        detail->hasParameters = false;
        return false;
    }

    if (!entry->usesGenericParameterAlgorithm) {
        FillNoParameterDetail(*entry, index, summary, detail);
        return false;
    }

    const auto *spec = FindGenericSpec(entry->key);
    if (spec == nullptr) {
        FillNoParameterDetail(*entry, index, summary, detail);
        return false;
    }

    FillGenericParameterDetail(*entry,
                               index,
                               summary,
                               *spec,
                               geyserSeed,
                               worldHeight,
                               worldOffsetX,
                               worldOffsetY,
                               detail);
    return true;
}

} // namespace Geyser
