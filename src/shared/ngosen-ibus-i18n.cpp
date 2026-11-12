/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ngosen-ibus-i18n.h"

#include <atomic>

namespace ngosen {

    namespace {

        // A flag rather than LANGUAGE: changing the environment is unsafe once GLib's threads run.
        std::atomic<bool> english{false};

    } // namespace

    void setUiEnglish(bool value) {
        english = value;
    }

    bool uiEnglish() {
        return english;
    }

    const char* uiText(const char* text) {
        return english ? text : g_dgettext(GETTEXT_PACKAGE, text);
    }

} // namespace ngosen
