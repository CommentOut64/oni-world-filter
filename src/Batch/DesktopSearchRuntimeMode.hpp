#pragma once

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

enum class DesktopSearchRuntimeMode {
    Legacy,
    Optimized,
};

inline std::optional<DesktopSearchRuntimeMode>
ParseDesktopSearchRuntimeMode(std::string_view text)
{
    std::string normalized(text);
    std::transform(normalized.begin(),
                   normalized.end(),
                   normalized.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (normalized == "legacy") {
        return DesktopSearchRuntimeMode::Legacy;
    }
    if (normalized == "optimized") {
        return DesktopSearchRuntimeMode::Optimized;
    }
    return std::nullopt;
}
