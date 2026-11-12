/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "ngosen-ibus-keys.h"

#include <cstdint>
#include <vector>

namespace ngosen {

    // The English/Vietnamese key, as the IBus and fcitx5 editions read it. A tap of a modifier-only
    // key such as Ctrl+Shift toggles once every modifier is up, so Ctrl+Shift+T or Ctrl+Shift then
    // Ctrl+V does not. A key with a letter such as Alt+Z toggles on its first press, and the app sees
    // neither its press nor its release.
    class ToggleKey {
      public:
        struct Result {
            bool toggle  = false;
            bool swallow = false; // the app must not see this key
        };

        void setChords(std::vector<KeyChord> chords) {
            chords_ = std::move(chords);
        }
        // sym as pressed, X masks with ReleaseMask on a release, the compositor's time in ms.
        Result feed(uint32_t sym, uint32_t state, uint32_t timeMs);
        // A focus change: a tap started before it does not count.
        void reset();

      private:
        bool                  matches(uint32_t sym, uint32_t state) const;

        std::vector<KeyChord> chords_;
        bool                  armed_        = false;
        bool                  pending_      = false;
        uint32_t              since_        = 0;
        uint32_t              swallowedKey_ = 0;
        bool                  chordHeld_    = false;
    };

} // namespace ngosen
