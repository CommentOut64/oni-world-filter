#include "App/SettingsAsset.hpp"

#ifndef __EMSCRIPTEN__

#include "config.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

std::filesystem::path ResolveSettingsAssetPath()
{
#if defined(_WIN32)
    wchar_t executablePath[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, executablePath, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        return std::filesystem::path(executablePath).parent_path() / SETTING_ASSET_FILENAME;
    }
#endif
    return std::filesystem::current_path() / SETTING_ASSET_FILENAME;
}

#endif
