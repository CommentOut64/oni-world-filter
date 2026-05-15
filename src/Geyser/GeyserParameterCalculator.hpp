#pragma once

#include <string>
#include <vector>

#include "App/ResultModels.hpp"

namespace GeyserCalc {

std::vector<GeyserDetail> BuildGeyserDetails(int geyserSeed,
                                             int worldHeight,
                                             const std::vector<GeyserSummary> &geysers,
                                             int worldOffsetX = 0,
                                             int worldOffsetY = 0);

WorldReportData BuildWorldReportData(const GeneratedWorldPreview &preview,
                                     int geyserSeed,
                                     int mixing,
                                     const std::string &coord,
                                     int worldOffsetX = 0,
                                     int worldOffsetY = 0);

} // namespace GeyserCalc
