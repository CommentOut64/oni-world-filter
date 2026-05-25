#pragma once

#include <string_view>

namespace Geyser {

enum class GeyserKind {
    GenericGeyser,
    FixedTemplateGeyser,
    Reservoir,
    WarpPortal,
    Cryopod,
    Aquatic,
    Facility,
    Unknown,
};

std::string_view ToString(GeyserKind kind);

} // namespace Geyser
