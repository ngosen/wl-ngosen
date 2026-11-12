/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ngosen-masks.h"
#include "ngosen-wayland-key.h"

#include <iostream>
#include <string>
#include <xkbcommon/xkbcommon.h>

using namespace ngosen;

namespace {

    int  failures = 0;

    void check(bool ok, const std::string& what) {
        if (!ok) {
            ++failures;
            std::cout << "FAIL " << what << '\n';
        }
    }

    bool shortcut(uint32_t sym, uint32_t state) {
        return WaylandKeyPress(sym, 0, state, 0).hasModifier();
    }

} // namespace

int main() {
    // The core ends the word on a key with a modifier, so Shift that only makes a capital must not count.
    check(!shortcut(XKB_KEY_V, ShiftMask), "Shift+V types a letter");
    check(!shortcut(XKB_KEY_exclam, ShiftMask), "Shift+1 types a character");
    check(!shortcut(XKB_KEY_KP_5, ShiftMask), "Shift on a keypad digit");
    check(!shortcut(XKB_KEY_v, 0), "a plain letter");
    check(shortcut(XKB_KEY_V, ShiftMask | ControlMask), "Ctrl+Shift+V is a shortcut");
    check(shortcut(XKB_KEY_space, ShiftMask), "Shift+space is a chord");
    check(shortcut(XKB_KEY_Return, ShiftMask), "Shift+Enter is a chord");
    check(shortcut(XKB_KEY_Left, ShiftMask), "Shift+Left selects");
    check(shortcut(XKB_KEY_v, ControlMask), "Ctrl+V is a shortcut");
    check(!shortcut(XKB_KEY_v, LockMask), "Caps Lock is no modifier");
    if (failures == 0)
        std::cout << "all key checks passed\n";
    return failures == 0 ? 0 : 1;
}
