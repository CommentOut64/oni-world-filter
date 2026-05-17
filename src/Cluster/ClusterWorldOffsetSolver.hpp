#pragma once

#include <string>
#include <vector>

#include "Setting/WorldEffectiveState.hpp"
#include "Utils/Vector2f.hpp"

struct ClusterWorldOffset {
    int placementIndex{-1};
    Vector2i offset{};
    Vector2i size{};
    int hiddenY{};
};

bool ComputeClusterWorldOffsets(const std::vector<ResolvedWorldPlacement> &placements,
                                std::vector<ClusterWorldOffset> *offsets,
                                std::string *errorMessage = nullptr);

const ClusterWorldOffset *FindClusterWorldOffset(const std::vector<ClusterWorldOffset> &offsets,
                                                 int placementIndex);
