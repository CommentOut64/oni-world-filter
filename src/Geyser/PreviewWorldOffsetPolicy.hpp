#pragma once

#include "Setting/SettingsCache.hpp"

namespace PreviewWorldOffsetPolicy {

struct PreviewWorldOffset {
    int x{};
    int y{};
};

bool ResolveLegacyPrimaryWorldOffset(const SettingsCache &settings, PreviewWorldOffset *offset);

} // namespace PreviewWorldOffsetPolicy
