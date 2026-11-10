/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ngosen-ibus-keys.h"
#include "ngosen-masks.h"

#include <cstring>
#include <xkbcommon/xkbcommon.h>

namespace ngosen {

    namespace {

        constexpr guint HeldModifiers = ShiftMask | ControlMask | Mod1Mask | SuperMask | HyperMask;

        // The US layout puts Meta on the second level of Alt, so Shift then Alt arrives as Meta.
        guint canonicalKey(guint keyval) {
            switch (keyval) {
                case XKB_KEY_Meta_L: return XKB_KEY_Alt_L;
                case XKB_KEY_Meta_R: return XKB_KEY_Alt_R;
                default: return xkb_keysym_to_lower(keyval);
            }
        }

        // The modifier a key itself sets, so releasing Shift_L is not read as Shift held.
        guint ownModifier(guint keyval) {
            switch (canonicalKey(keyval)) {
                case XKB_KEY_Shift_L:
                case XKB_KEY_Shift_R: return ShiftMask;
                case XKB_KEY_Control_L:
                case XKB_KEY_Control_R: return ControlMask;
                case XKB_KEY_Alt_L:
                case XKB_KEY_Alt_R: return Mod1Mask;
                case XKB_KEY_Super_L:
                case XKB_KEY_Super_R: return SuperMask;
                default: return 0;
            }
        }

        // Super arrives as Mod4, while "Super+space" parses to the Super bit. GTK 4 reports the Meta bit
        // beside the Mod1 of Alt.
        guint heldModifiers(guint state) {
            if ((state & Mod4Mask) != 0)
                state |= SuperMask;
            if ((state & MetaMask) != 0)
                state |= Mod1Mask;
            return state & HeldModifiers;
        }

        // Modifier names as ibus_key_event_from_string reads them, plus fcitx5's Alt.
        guint modifierNamed(const std::string& name) {
            static const std::pair<const char*, guint> names[] = {
                {"Shift", ShiftMask}, {"Lock", LockMask}, {"Control", ControlMask}, {"Alt", Mod1Mask},    {"Mod1", Mod1Mask}, {"Mod2", Mod2Mask},       {"Mod3", Mod3Mask},
                {"Mod4", Mod4Mask},   {"Mod5", Mod5Mask}, {"Super", SuperMask},     {"Hyper", HyperMask}, {"Meta", MetaMask}, {"Release", ReleaseMask},
            };
            for (const auto& [text, mask] : names) {
                if (name == text)
                    return mask;
            }
            return 0;
        }

        // "Control+Shift_L": modifiers joined by +, then a key symbol name.
        bool parseKeyString(const std::string& text, guint& keyval, guint& modifiers) {
            modifiers         = 0;
            std::size_t start = 0;
            for (std::size_t plus = text.find('+'); plus != std::string::npos; plus = text.find('+', start)) {
                const guint mask = modifierNamed(text.substr(start, plus - start));
                if (mask == 0)
                    return false;
                modifiers |= mask;
                start = plus + 1;
            }
            const std::string name = text.substr(start);
            if (name.empty())
                return false;
            keyval = xkb_keysym_from_name(name.c_str(), XKB_KEYSYM_NO_FLAGS);
            return keyval != XKB_KEY_NoSymbol && keyval != XKB_KEY_VoidSymbol;
        }

    } // namespace

    const std::vector<std::string>& defaultToggleKeys() {
        static const std::vector<std::string> keys = {"Control+Shift_L", "Control+Shift_R", "Shift+Control_L", "Shift+Control_R"};
        return keys;
    }

    const std::vector<std::string>& defaultModeMenuKeys() {
        static const std::vector<std::string> keys = {"grave"};
        return keys;
    }

    std::optional<KeyChord> parseToggleKey(const std::string& text) {
        KeyChord chord;
        if (!parseKeyString(text, chord.keyval, chord.modifiers))
            return std::nullopt;
        chord.modifiers = heldModifiers(chord.modifiers) & ~ownModifier(chord.keyval);
        if (chord.modifiers == 0)
            return std::nullopt;
        // fcitx5 matches letters whatever their case, so "Alt+Z" is Alt with the z key.
        chord.keyval = canonicalKey(chord.keyval);
        return chord;
    }

    std::optional<KeyChord> parseMenuKey(const std::string& text) {
        KeyChord chord;
        if (!parseKeyString(text, chord.keyval, chord.modifiers) || ownModifier(chord.keyval) != 0)
            return std::nullopt;
        chord.modifiers = heldModifiers(chord.modifiers);
        chord.keyval    = canonicalKey(chord.keyval);
        return chord;
    }

    bool otherModifierHeld(guint keyval, guint state) {
        return (heldModifiers(state) & ~ownModifier(keyval)) != 0;
    }

    bool matchesKey(const KeyChord& chord, guint keyval, guint state) {
        return canonicalKey(keyval) == chord.keyval && (heldModifiers(state) & ~ownModifier(keyval)) == chord.modifiers;
    }

} // namespace ngosen
