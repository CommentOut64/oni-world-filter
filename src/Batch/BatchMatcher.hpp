#pragma once

#include <string>
#include <vector>

#include "App/ResultSink.hpp"
#include "Batch/FilterConfig.hpp"

namespace Batch {

enum class MatchErrorCode {
    MissingStart,
    MissingWorldSize,
};

struct MatchError {
    MatchErrorCode code{};
    std::string detail;
};

struct MatchResult {
    bool matched = false;
    std::vector<MatchError> errors;

    bool Ok() const
    {
        return errors.empty();
    }
};

MatchResult MatchFilter(const FilterConfig &cfg, const BatchCaptureRecord &capture);

} // namespace Batch
