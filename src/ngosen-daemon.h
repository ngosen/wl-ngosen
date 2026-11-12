/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <optional>
#include <string>

namespace ngosen {

    // Connects to the compositor and types until it says the input method is unavailable or a signal
    // arrives. Returns the exit status: 0 on a signal or a compositor without input methods.
    // desktopLanguage is LANGUAGE as the session set it, before the daemon changed it for its menu.
    int runDaemon(std::optional<std::string> desktopLanguage);

} // namespace ngosen
