/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ngosen-wayland-key.h"

#include "ngosen-masks.h"

#include <xkbcommon/xkbcommon.h>

namespace ngosen {

    namespace {
        constexpr uint32_t SimpleModifiers = ShiftMask | ControlMask | Mod1Mask | Mod4Mask | SuperMask | HyperMask | MetaMask;

        bool               active(xkb_state* state, const char* name) {
            return xkb_state_mod_name_is_active(state, name, XKB_STATE_MODS_EFFECTIVE) > 0;
        }
    } // namespace

    uint32_t xStateOf(xkb_state* state) {
        if (state == nullptr)
            return 0;
        struct Mod {
            const char* name;
            uint32_t    mask;
        };
        // Real modifiers keep their X bit; the virtual ones are what IBus adds for Super, Hyper and Meta.
        static const Mod mods[] = {
            {XKB_MOD_NAME_SHIFT, ShiftMask},
            {XKB_MOD_NAME_CAPS, LockMask},
            {XKB_MOD_NAME_CTRL, ControlMask},
            {"Mod1", Mod1Mask},
            {"Mod2", Mod2Mask},
            {"Mod3", Mod3Mask},
            {"Mod4", Mod4Mask},
            {"Mod5", Mod5Mask},
            {"Super", SuperMask},
            {"Hyper", HyperMask},
            {"Meta", MetaMask},
        };
        uint32_t out = 0;
        for (const Mod& mod : mods) {
            if (active(state, mod.name))
                out |= mod.mask;
        }
        return out;
    }

    uint32_t WaylandKeyPress::sym() const {
        return sym_;
    }

    uint32_t WaylandKeyPress::states() const {
        return state_ & ~ReleaseMask;
    }

    bool WaylandKeyPress::isRelease() const {
        return (state_ & ReleaseMask) != 0;
    }

    bool WaylandKeyPress::isModifier() const {
        return (sym_ >= XKB_KEY_Shift_L && sym_ <= XKB_KEY_Hyper_R) || sym_ == XKB_KEY_ISO_Level3_Shift || sym_ == XKB_KEY_ISO_Level5_Shift;
    }

    bool WaylandKeyPress::isBareShift() const {
        return (sym_ == XKB_KEY_Shift_L || sym_ == XKB_KEY_Shift_R) && (state_ & SimpleModifiers & ~ShiftMask) == 0;
    }

    // The typing logic compares X11 keycodes, which are evdev codes plus 8.
    uint32_t WaylandKeyPress::code() const {
        return code_ + 8;
    }

    uint32_t WaylandKeyPress::time() const {
        return time_;
    }

    // As fcitx5's normalized key: Shift that only picks the character (V, !) is no shortcut, so the
    // core keeps the word going.
    bool WaylandKeyPress::hasModifier() const {
        uint32_t       mods   = state_ & SimpleModifiers;
        const bool     letter = (appSym_ >= XKB_KEY_a && appSym_ <= XKB_KEY_z) || (appSym_ >= XKB_KEY_A && appSym_ <= XKB_KEY_Z);
        const bool     keypad = appSym_ >= XKB_KEY_KP_0 && appSym_ <= XKB_KEY_KP_9;
        const uint32_t text   = xkb_keysym_to_utf32(appSym_);
        if (letter) {
            if (mods == ShiftMask)
                mods = 0;
        } else if ((text != 0 && appSym_ != XKB_KEY_space && appSym_ != XKB_KEY_Return && appSym_ != XKB_KEY_Tab) || keypad) {
            mods &= ~ShiftMask;
        }
        return mods != 0;
    }

    bool WaylandKeyPress::isCursorMove() const {
        switch (appSym_) {
            case XKB_KEY_Left:
            case XKB_KEY_Right:
            case XKB_KEY_Up:
            case XKB_KEY_Down:
            case XKB_KEY_Page_Up:
            case XKB_KEY_Page_Down:
            case XKB_KEY_Home:
            case XKB_KEY_End: return (state_ & SimpleModifiers & ~(ControlMask | ShiftMask)) == 0;
            default: return false;
        }
    }

    std::string WaylandKeyPress::name() const {
        std::string out;
        if ((state_ & ControlMask) != 0)
            out += "Control+";
        if ((state_ & Mod1Mask) != 0)
            out += "Alt+";
        if ((state_ & ShiftMask) != 0)
            out += "Shift+";
        if ((state_ & (Mod4Mask | SuperMask)) != 0)
            out += "Super+";
        char keyName[64];
        if (xkb_keysym_get_name(appSym_, keyName, sizeof(keyName)) <= 0)
            return {};
        return out + keyName;
    }

    void WaylandKeyPress::replaceSym(uint32_t sym) {
        appSym_ = sym;
    }

    void WaylandKeyPress::accept() {
        accepted_ = true;
    }

    bool WaylandKeyPress::accepted() const {
        return accepted_;
    }

} // namespace ngosen
