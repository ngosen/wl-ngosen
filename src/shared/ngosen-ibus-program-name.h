/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace ngosen {

    // The name the core knows a GNOME app by, from the id GNOME gives it: a Flatpak app is named as it
    // would be outside Flatpak.
    std::string gnomeProgramName(const std::string& appId);

    // As fcitx5 finds and reads desktop files, so both editions name an app alike.
    std::vector<std::string>   desktopFiles(const std::string& appId);
    std::optional<std::string> readStartupWmClass(const std::string& path);

} // namespace ngosen
