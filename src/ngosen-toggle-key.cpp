/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ngosen-toggle-key.h"

#include "ngosen-masks.h"

#include <algorithm>
#include <xkbcommon/xkbcommon.h>

namespace ngosen {

    namespace {
        // Longer than this is a hold, such as Ctrl+Shift with a click. Wayland gives an input method no
        // clicks, so only the time tells them apart.
        constexpr uint32_t TapMs = 500;

        bool               isModifierSym(uint32_t sym) {
            return (sym >= XKB_KEY_Shift_L && sym <= XKB_KEY_Hyper_R) || sym == XKB_KEY_ISO_Level3_Shift || sym == XKB_KEY_ISO_Level5_Shift;
        }
    } // namespace

    bool ToggleKey::matches(uint32_t sym, uint32_t state) const {
        return std::any_of(chords_.begin(), chords_.end(), [&](const KeyChord& chord) { return matchesKey(chord, sym, state); });
    }

    void ToggleKey::reset() {
        armed_ = pending_ = chordHeld_ = false;
    }

    ToggleKey::Result ToggleKey::feed(uint32_t sym, uint32_t state, uint32_t timeMs) {
        const bool release  = (state & ReleaseMask) != 0;
        const bool modifier = isModifierSym(sym);
        if (release && swallowedKey_ != 0 && xkb_keysym_to_lower(sym) == swallowedKey_) {
            swallowedKey_ = 0;
            chordHeld_    = false;
            return {false, true};
        }
        if (!release && !modifier) {
            armed_ = pending_ = false;
            if (!matches(sym, state))
                return {};
            // A held chord repeats; only its first press switches.
            const bool toggle = !chordHeld_;
            chordHeld_        = true;
            swallowedKey_     = xkb_keysym_to_lower(sym);
            return {toggle, true};
        }
        if (!modifier)
            return {};
        if (!release) {
            // Nothing else held means every key went up, even if a grab ate the last release.
            if (pending_ && otherModifierHeld(sym, state))
                return {};
            pending_ = false;
            // A held key repeats; the hold counts from the first press.
            const bool matched = matches(sym, state);
            if (matched && !armed_)
                since_ = timeMs;
            armed_ = matched;
            return {};
        }
        // A grab can eat the release of the letter, so a modifier release ends the chord too.
        chordHeld_ = false;
        if (armed_ && matches(sym, state)) {
            armed_   = false;
            pending_ = true;
        }
        if (pending_ && !otherModifierHeld(sym, state)) {
            pending_ = false;
            if (timeMs - since_ <= TapMs)
                return {true, false};
        }
        return {};
    }

} // namespace ngosen
