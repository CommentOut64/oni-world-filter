#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace MixingCode {

struct SlotRegistration {
    std::string_view path;
    int type = 0; // 0 dlc, 1 world, 2 subworld
};

inline constexpr std::array<SlotRegistration, 17> kSupportedSlots = {{
    {"DLC2_ID", 0},
    {"dlc2::subworldMixing/IceCavesMixingSettings", 2},
    {"dlc2::subworldMixing/CarrotQuarryMixingSettings", 2},
    {"dlc2::subworldMixing/SugarWoodsMixingSettings", 2},
    {"dlc2::worldMixing/CeresMixingSettings", 1},
    {"DLC3_ID", 0},
    {"DLC4_ID", 0},
    {"dlc4::subworldMixing/GardenMixingSettings", 2},
    {"dlc4::subworldMixing/RaptorMixingSettings", 2},
    {"dlc4::subworldMixing/WetlandsMixingSettings", 2},
    {"dlc4::worldMixing/PrehistoricMixingSettings", 1},
    {"DLC5_ID", 0},
    {"dlc5::subworldMixing/BeachMixingSettings", 2},
    {"dlc5::subworldMixing/ReefMixingSettings", 2},
    {"dlc5::subworldMixing/KelpForestMixingSettings", 2},
    {"dlc5::subworldMixing/AbyssMixingSettings", 2},
    {"dlc5::worldMixing/AquaticMixingSettings", 1},
}};

constexpr uint64_t Pow5(std::size_t exponent)
{
    uint64_t value = 1;
    for (std::size_t i = 0; i < exponent; ++i) {
        value *= 5ULL;
    }
    return value;
}

constexpr std::size_t GetSupportedSlotCount()
{
    return kSupportedSlots.size();
}

constexpr uint64_t MaxValueForSlotCount(std::size_t slotCount)
{
    return slotCount == 0 ? 0ULL : (Pow5(slotCount) - 1ULL);
}

constexpr uint64_t AllEnabledProbeValueForSlotCount(std::size_t slotCount)
{
    uint64_t value = 0;
    for (std::size_t i = 0; i < slotCount; ++i) {
        value = value * 5ULL + 1ULL;
    }
    return value;
}

constexpr uint64_t GetSupportedMixingMax()
{
    return MaxValueForSlotCount(GetSupportedSlotCount());
}

inline int ReadMixingSlotLevel(uint64_t mixing, std::size_t slotCount, std::size_t slot)
{
    if (slot >= slotCount) {
        return 0;
    }
    for (std::size_t i = 0; i < (slotCount - 1 - slot); ++i) {
        mixing /= 5ULL;
    }
    return static_cast<int>(mixing % 5ULL);
}

inline std::string ToLittleEndianBase36(uint64_t input)
{
    if (input == 0) {
        return "0";
    }

    constexpr char dict[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::string result;
    while (input > 0) {
        result.push_back(dict[input % 36ULL]);
        input /= 36ULL;
    }
    return result;
}

inline std::string BuildCanonicalCoordinate(std::string_view prefix, int seed, uint64_t mixing)
{
    std::string code(prefix);
    code += std::to_string(seed);
    code += "-0-D3-";
    code += ToLittleEndianBase36(mixing);
    return code;
}

} // namespace MixingCode
