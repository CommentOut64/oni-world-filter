#include "Setting/NativeCoordinate.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <ranges>
#include <string_view>
#include <utility>

#include "SearchAnalysis/SearchCatalog.hpp"
#include "Setting/SettingsCache.hpp"

namespace NativeCoordinate {

namespace {

bool IsAsciiDigits(std::string_view text)
{
    return !text.empty() &&
           std::ranges::all_of(text, [](unsigned char ch) { return std::isdigit(ch) != 0; });
}

bool IsUpperBase36(std::string_view text)
{
    return !text.empty() && std::ranges::all_of(text, [](unsigned char ch) {
               return std::isdigit(ch) != 0 || (ch >= 'A' && ch <= 'Z');
           });
}

bool SplitCoordSuffix(std::string_view suffix,
                      std::array<std::string_view, 4> *parts)
{
    if (parts == nullptr) {
        return false;
    }

    size_t start = 0;
    for (size_t index = 0; index < 3; ++index) {
        const size_t dashPos = suffix.find('-', start);
        if (dashPos == std::string_view::npos || dashPos == start) {
            return false;
        }
        (*parts)[index] = suffix.substr(start, dashPos - start);
        start = dashPos + 1;
    }

    if (start >= suffix.size()) {
        return false;
    }
    (*parts)[3] = suffix.substr(start);
    return std::ranges::all_of(*parts, [](std::string_view part) { return !part.empty(); });
}

bool IsValidNativeMixingPart(std::string_view mixingPart)
{
    if (mixingPart == "0") {
        return true;
    }
    return mixingPart.size() == 5 && IsUpperBase36(mixingPart);
}

} // namespace

bool ResolveNativeCoordinate(const std::string &rawCoord,
                             NativeCoordinateResolution *result)
{
    if (result == nullptr) {
        return false;
    }

    const auto &worldPrefixes = SearchAnalysis::GetWorldPrefixes();
    int matchedWorldType = -1;
    size_t matchedPrefixLength = 0;
    for (size_t index = 0; index < worldPrefixes.size(); ++index) {
        const auto &prefix = worldPrefixes[index];
        if (rawCoord.starts_with(prefix) && prefix.size() > matchedPrefixLength) {
            matchedWorldType = static_cast<int>(index);
            matchedPrefixLength = prefix.size();
        }
    }
    if (matchedWorldType < 0) {
        return false;
    }

    const std::string_view suffix(rawCoord.data() + matchedPrefixLength,
                                  rawCoord.size() - matchedPrefixLength);
    std::array<std::string_view, 4> parts{};
    if (!SplitCoordSuffix(suffix, &parts)) {
        return false;
    }

    const std::string_view seedPart = parts[0];
    const std::string_view mixingPart = parts[3];
    if (!IsAsciiDigits(seedPart) || !IsValidNativeMixingPart(mixingPart)) {
        return false;
    }

    int parsedSeed = 0;
    const auto seedParse = std::from_chars(seedPart.data(),
                                           seedPart.data() + seedPart.size(),
                                           parsedSeed);
    if (seedParse.ec != std::errc() || seedParse.ptr != seedPart.data() + seedPart.size()) {
        return false;
    }

    result->worldType = matchedWorldType;
    result->seed = parsedSeed;
    result->mixing = static_cast<int>(SettingsCache::Base36ToBinary(std::string(mixingPart)));
    result->code = rawCoord;
    return true;
}

} // namespace NativeCoordinate
