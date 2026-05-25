#include "Geyser/GeyserKind.hpp"

namespace Geyser {

std::string_view ToString(GeyserKind kind)
{
    switch (kind) {
    case GeyserKind::GenericGeyser:
        return "generic_geyser";
    case GeyserKind::FixedTemplateGeyser:
        return "fixed_template_geyser";
    case GeyserKind::Reservoir:
        return "reservoir";
    case GeyserKind::WarpPortal:
        return "warp_portal";
    case GeyserKind::Cryopod:
        return "cryopod";
    case GeyserKind::Aquatic:
        return "aquatic";
    case GeyserKind::Facility:
        return "facility";
    default:
        return "unknown";
    }
}

} // namespace Geyser
