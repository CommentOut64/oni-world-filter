#include "Batch/BatchMatcher.hpp"

#include <cmath>

namespace Batch {

MatchResult MatchFilter(const FilterConfig &cfg, const BatchCaptureRecord &capture)
{
    MatchResult result;

    if (capture.worldW <= 0 || capture.worldH <= 0) {
        result.errors.push_back({
            .code = MatchErrorCode::MissingWorldSize,
            .detail = "world size is missing",
        });
    }
    if (capture.startX == 0 && capture.startY == 0) {
        result.errors.push_back({
            .code = MatchErrorCode::MissingStart,
            .detail = "start position is missing",
        });
    }
    if (!result.errors.empty()) {
        return result;
    }

    const float startX = static_cast<float>(capture.startX);
    const float startY = static_cast<float>(capture.startY);

    for (const auto &geyser : capture.geysers) {
        for (int forbiddenId : cfg.forbidden) {
            if (geyser.type == forbiddenId) {
                result.matched = false;
                return result;
            }
        }
    }

    for (int requiredId : cfg.required) {
        bool found = false;
        for (const auto &geyser : capture.geysers) {
            if (geyser.type == requiredId) {
                found = true;
                break;
            }
        }
        if (!found) {
            result.matched = false;
            return result;
        }
    }

    for (const auto &rule : cfg.distanceRules) {
        bool withinRange = false;
        for (const auto &geyser : capture.geysers) {
            if (geyser.type != rule.type) {
                continue;
            }
            const float dx = static_cast<float>(geyser.x) - startX;
            const float dy = static_cast<float>(geyser.y) - startY;
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (dist >= rule.minDist && dist <= rule.maxDist) {
                withinRange = true;
                break;
            }
        }
        if (!withinRange) {
            result.matched = false;
            return result;
        }
    }

    for (const auto &rule : cfg.countRules) {
        int count = 0;
        for (const auto &geyser : capture.geysers) {
            if (geyser.type == rule.type) {
                ++count;
            }
        }
        if (count < rule.minCount || count > rule.maxCount) {
            result.matched = false;
            return result;
        }
    }

    result.matched = true;
    return result;
}

} // namespace Batch
