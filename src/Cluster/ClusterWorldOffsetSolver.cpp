#include "Cluster/ClusterWorldOffsetSolver.hpp"

#include <algorithm>
#include <ranges>
#include <vector>

namespace {

struct LayoutRect {
    int x{};
    int y{};
    int width{};
    int height{};

    [[nodiscard]] int X1() const { return x; }
    [[nodiscard]] int X2() const { return x + width + 2; }
    [[nodiscard]] int Y1() const { return y; }
    [[nodiscard]] int Y2() const { return y + height + 2; }
};

bool IsUnoccupied(const LayoutRect &candidate, const std::vector<LayoutRect> &placed)
{
    for (const auto &item : placed) {
        if (candidate.X1() < item.X2() &&
            candidate.X2() > item.X1() &&
            candidate.Y1() < item.Y2() &&
            candidate.Y2() > item.Y1()) {
            return false;
        }
    }
    return true;
}

} // namespace

bool ComputeClusterWorldOffsets(const std::vector<ResolvedWorldPlacement> &placements,
                                std::vector<ClusterWorldOffset> *offsets,
                                std::string *errorMessage)
{
    if (offsets == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "cluster world offset output is null";
        }
        return false;
    }
    offsets->clear();
    if (placements.empty()) {
        return true;
    }

    offsets->reserve(placements.size());
    std::vector<size_t> sortedIndexes;
    sortedIndexes.reserve(placements.size());
    int tallestHeight = 0;
    for (size_t i = 0; i < placements.size(); ++i) {
        const auto &placement = placements[i];
        if (placement.sourceWorld == nullptr) {
            if (errorMessage != nullptr) {
                *errorMessage = "resolved placement source world is null";
            }
            offsets->clear();
            return false;
        }
        const auto size = placement.sourceWorld->worldsize;
        offsets->push_back(ClusterWorldOffset{
            .placementIndex = placement.placementIndex,
            .offset = {},
            .size = {static_cast<int>(size.x), static_cast<int>(size.y)},
            .hiddenY = placement.sourceWorld->hiddenY,
        });
        sortedIndexes.push_back(i);
        tallestHeight = std::max(tallestHeight, static_cast<int>(size.y));
    }

    std::sort(sortedIndexes.begin(), sortedIndexes.end(), [offsets](const size_t lhs, const size_t rhs) {
        return (*offsets)[lhs].size.y > (*offsets)[rhs].size.y;
    });

    std::vector<LayoutRect> placedRects;
    placedRects.reserve(placements.size());
    for (const size_t index : sortedIndexes) {
        auto &offset = (*offsets)[index];
        Vector2i position{};
        LayoutRect candidate{position.x, position.y, offset.size.x, offset.size.y};
        while (!IsUnoccupied(candidate, placedRects)) {
            if (position.y + offset.size.y >= tallestHeight + 32) {
                position.y = 0;
                ++position.x;
            } else {
                ++position.y;
            }
            candidate = LayoutRect{position.x, position.y, offset.size.x, offset.size.y};
        }
        offset.offset = position;
        placedRects.push_back(candidate);
    }

    return true;
}

const ClusterWorldOffset *FindClusterWorldOffset(const std::vector<ClusterWorldOffset> &offsets,
                                                 int placementIndex)
{
    const auto itr = std::ranges::find(offsets, placementIndex, &ClusterWorldOffset::placementIndex);
    return itr == offsets.end() ? nullptr : &*itr;
}
