// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MELEE_STAGE_ASSETS_H
#define MELEE_STAGE_ASSETS_H

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace MeleeStageAssets {
    inline bool LightingEnabled()
    {
        using Setting = int (*)();
        static const Setting setting = [] {
            void* symbol = dlsym(RTLD_DEFAULT, "MeleeStageLightingEnabled");
            Setting result = nullptr;
            static_assert(sizeof(result) == sizeof(symbol));
            std::memcpy(&result, &symbol, sizeof(result));
            return result;
        }();
        if (setting != nullptr) {
            return setting() == 1;
        }
        const char* value = std::getenv("MELEE_STAGE_LIGHTING");
        return value == nullptr || std::string_view(value) != "0";
    }

    /* DirectoryBlob keeps the original file and disc offsets. Substitute
     * only this app's same-size Final Destination archive during a read.
     * Loaded stage data stays valid until the next stage load.
     */
    inline std::optional<std::string> Replacement(std::string_view source)
    {
        constexpr std::string_view suffix =
            "/Contents/Resources/Game/files/GrNLa.dat";
        constexpr std::uintmax_t archive_size = 611125;
        if (!source.ends_with(suffix) ||
            std::getenv("MELEE_APP_BUNDLE") == nullptr || !LightingEnabled())
        {
            return std::nullopt;
        }
        const std::filesystem::path original(source);
        const auto replacement =
            original.parent_path().parent_path().parent_path() /
            "Lighting/GrNLa.dat";
        std::error_code error;
        const auto original_size = std::filesystem::file_size(original, error);
        if (error || original_size != archive_size) {
            return std::nullopt;
        }
        const auto replacement_size =
            std::filesystem::file_size(replacement, error);
        if (error || replacement_size != original_size) {
            return std::nullopt;
        }
        static const bool reported = [] {
            std::fputs("[melee-stage-assets] Final Destination sky variant\n",
                       stderr);
            return true;
        }();
        (void) reported;
        return replacement.string();
    }
}

#endif
