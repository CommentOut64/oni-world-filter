#pragma once

#include <string>

namespace NativeCoordinate {

struct NativeCoordinateResolution {
    int worldType = -1;
    int seed = 0;
    int mixing = 0;
    std::string code;
};

bool ResolveNativeCoordinate(const std::string &rawCoord,
                             NativeCoordinateResolution *result);

} // namespace NativeCoordinate
