/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <glib.h>
#include <optional>
#include <string>
#include <vector>

namespace ngosen {

    // A shortcut as fcitx5 writes it in its config, such as "Control+Shift_L" or "Alt+z".
    struct KeyChord {
        guint keyval    = 0;
        guint modifiers = 0;
    };

    // Ctrl+Shift pressed in either order, with either Shift or Ctrl, as the fcitx5 edition stores it.
    const std::vector<std::string>& defaultToggleKeys();

    // ` as in the fcitx5 edition.
    const std::vector<std::string>& defaultModeMenuKeys();

    // Empty for a string IBus cannot read, or a single key: a bare Shift would switch on every capital.
    std::optional<KeyChord> parseToggleKey(const std::string& text);

    // A key that opens the mode menu, which may be a single key such as ` but not a lone modifier.
    std::optional<KeyChord> parseMenuKey(const std::string& text);

    // Whether a key event, pressed or released, is this chord. Lock keys such as NumLock do not count,
    // and a modifier being released does not count as held.
    bool matchesKey(const KeyChord& chord, guint keyval, guint state);

    // Whether a modifier other than the key itself is still down.
    bool otherModifierHeld(guint keyval, guint state);

} // namespace ngosen
