/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ngosen-ibus-program-name.h"

#include "ngosen-flatpak-names.h"
#include "ngosen-ibus-config.h"

#include <glib.h>

namespace ngosen {

    std::vector<std::string> desktopFiles(const std::string& appId) {
        const std::string        file = "applications/" + appId + ".desktop";
        std::vector<std::string> found;
        const auto               add = [&](const char* dir) {
            gchar* path = g_build_filename(dir, file.c_str(), nullptr);
            if (g_file_test(path, G_FILE_TEST_EXISTS))
                found.emplace_back(path);
            g_free(path);
        };
        add(g_get_user_data_dir());
        for (const gchar* const* dir = g_get_system_data_dirs(); *dir != nullptr; ++dir)
            add(*dir);
        return found;
    }

    // Read as fcitx5 reads it, which skips a line it cannot read where GKeyFile gives up.
    std::optional<std::string> readStartupWmClass(const std::string& path) {
        gchar* contents = nullptr;
        if (!g_file_get_contents(path.c_str(), &contents, nullptr, nullptr))
            return std::nullopt;
        const IniFile ini = parseIni(contents);
        g_free(contents);
        const auto entry = ini.sections.find("Desktop Entry");
        if (entry == ini.sections.end())
            return std::string();
        const auto wmClass = entry->second.find("StartupWMClass");
        return wmClass == entry->second.end() ? std::string() : trimIni(wmClass->second);
    }

    // Not cached, so an app installed during the session is named; this runs once per app switch.
    std::string gnomeProgramName(const std::string& appId) {
        return flatpakProgramName(appId, desktopFiles, readStartupWmClass);
    }

} // namespace ngosen
