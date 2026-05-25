#include "Geyser/GeyserParameterCalculator.hpp"

#include "Geyser/GeyserCatalog.hpp"
#include "Geyser/GeyserParameterProvider.hpp"

namespace GeyserCalc {

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
        GeyserDetail detail;
        Geyser::BuildDetailFromCatalog(Geyser::FindById(summary.type),
                                       index,
                                       summary,
                                       geyserSeed,
                                       worldHeight,
                                       worldOffsetX,
                                       worldOffsetY,
                                       &detail);
        details.push_back(std::move(detail));
    }
    return details;
}

WorldReportData BuildWorldReportData(const GeneratedWorldPreview &preview,
                                     int geyserSeed,
                                     uint64_t mixing,
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
