#include "Geyser/PreviewWorldOffsetPolicy.hpp"

namespace PreviewWorldOffsetPolicy {

bool ResolveLegacyPrimaryWorldOffset(const SettingsCache &settings, PreviewWorldOffset *offset)
{
    if (offset == nullptr || settings.cluster == nullptr) {
        return false;
    }

    offset->x = 0;
    offset->y = 0;

    const std::string &prefix = settings.cluster->coordinatePrefix;
    if (prefix == "M-CERS-C" || prefix == "M-BAD-C") {
        offset->x = 82;
        return true;
    }
    if (prefix == "M-FLIP-C" || prefix == "M-FRZ-C" ||
        prefix == "M-SWMP-C" || prefix == "M-RAD-C") {
        offset->x = 212;
    }
    return true;
}

} // namespace PreviewWorldOffsetPolicy
