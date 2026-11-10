/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "ngosen-key.h"

#include <cstdint>
#include <string>

struct xkb_state;

namespace ngosen {

    // The modifiers of an xkb state as X and IBus masks, which the typing logic and the copied key
    // parsing read. Like X, a key event carries the state before the key itself.
    uint32_t xStateOf(xkb_state* state);

    class WaylandKeyPress final : public KeyPress {
      public:
        // sym: the keysym at the current level; state: X masks, with ReleaseMask on a release.
        WaylandKeyPress(uint32_t sym, uint32_t evdevCode, uint32_t state, uint32_t timeMs) : sym_(sym), appSym_(sym), code_(evdevCode), state_(state), time_(timeMs) {}

        uint32_t    sym() const override;
        uint32_t    states() const override;
        bool        isRelease() const override;
        bool        isModifier() const override;
        bool        isBareShift() const override;
        uint32_t    code() const override;
        uint32_t    time() const override;
        bool        hasModifier() const override;
        bool        isCursorMove() const override;
        std::string name() const override;
        void        replaceSym(uint32_t sym) override;
        void        accept() override;
        bool        accepted() const override;

        // The symbol the app should get instead of the one pressed, or 0 when unchanged.
        uint32_t replacedSym() const {
            return appSym_ != sym_ ? appSym_ : 0;
        }

      private:
        uint32_t sym_;
        uint32_t appSym_;
        uint32_t code_;
        uint32_t state_;
        uint32_t time_;
        bool     accepted_ = false;
    };

} // namespace ngosen
