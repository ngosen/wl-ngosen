/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ngosen-masks.h"
#include "ngosen-toggle-key.h"

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

    ToggleKey withKeys(const std::vector<std::string>& keys) {
        std::vector<KeyChord> chords;
        for (const auto& key : keys) {
            if (const auto chord = parseToggleKey(key))
                chords.push_back(*chord);
        }
        ToggleKey toggle;
        toggle.setChords(chords);
        return toggle;
    }

    // States as X reports them: a key carries the modifiers held before it.
    struct Keys {
        ToggleKey& toggle;
        uint32_t   time      = 1000;
        int        toggles   = 0;
        int        swallowed = 0;

        void       send(uint32_t sym, uint32_t state, uint32_t after = 10) {
            time += after;
            const auto result = toggle.feed(sym, state, time);
            toggles += result.toggle ? 1 : 0;
            swallowed += result.swallow ? 1 : 0;
        }
        void press(uint32_t sym, uint32_t state, uint32_t after = 10) {
            send(sym, state, after);
        }
        void release(uint32_t sym, uint32_t state, uint32_t after = 10) {
            send(sym, state | ReleaseMask, after);
        }
    };

    void testCtrlShiftTap() {
        ToggleKey toggle = withKeys(defaultToggleKeys());
        Keys      keys{toggle};
        keys.press(XKB_KEY_Control_L, 0);
        keys.press(XKB_KEY_Shift_L, ControlMask);
        check(keys.toggles == 0, "nothing toggles while the keys are down");
        keys.release(XKB_KEY_Shift_L, ControlMask | ShiftMask);
        keys.release(XKB_KEY_Control_L, ControlMask);
        check(keys.toggles == 1, "a tap of Ctrl+Shift toggles once every key is up");
        check(keys.swallowed == 0, "the app sees a modifier-only toggle key");

        keys.press(XKB_KEY_Shift_R, 0);
        keys.press(XKB_KEY_Control_R, ShiftMask);
        keys.release(XKB_KEY_Control_R, ShiftMask | ControlMask);
        keys.release(XKB_KEY_Shift_R, ShiftMask);
        check(keys.toggles == 2, "Shift then Ctrl toggles too");
    }

    void testShortcutsDoNotToggle() {
        ToggleKey toggle = withKeys(defaultToggleKeys());
        Keys      keys{toggle};
        keys.press(XKB_KEY_Control_L, 0);
        keys.press(XKB_KEY_Shift_L, ControlMask);
        keys.press(XKB_KEY_T, ControlMask | ShiftMask);
        keys.release(XKB_KEY_T, ControlMask | ShiftMask);
        keys.release(XKB_KEY_Shift_L, ControlMask | ShiftMask);
        keys.release(XKB_KEY_Control_L, ControlMask);
        check(keys.toggles == 0, "Ctrl+Shift+T does not toggle");

        keys.press(XKB_KEY_Control_L, 0);
        keys.press(XKB_KEY_Shift_L, ControlMask);
        keys.release(XKB_KEY_Shift_L, ControlMask | ShiftMask);
        keys.press(XKB_KEY_v, ControlMask);
        keys.release(XKB_KEY_v, ControlMask);
        keys.release(XKB_KEY_Control_L, ControlMask);
        check(keys.toggles == 0, "Ctrl+Shift then Ctrl+V does not toggle");
    }

    void testHold() {
        ToggleKey toggle = withKeys(defaultToggleKeys());
        Keys      keys{toggle};
        keys.press(XKB_KEY_Control_L, 0);
        keys.press(XKB_KEY_Shift_L, ControlMask);
        keys.release(XKB_KEY_Shift_L, ControlMask | ShiftMask, 600);
        keys.release(XKB_KEY_Control_L, ControlMask);
        check(keys.toggles == 0, "Ctrl+Shift held longer than a tap does not toggle");

        keys.press(XKB_KEY_Control_L, 0);
        keys.press(XKB_KEY_Shift_L, ControlMask);
        keys.release(XKB_KEY_Shift_L, ControlMask | ShiftMask, 490);
        keys.release(XKB_KEY_Control_L, ControlMask);
        check(keys.toggles == 1, "a slow tap within the limit still toggles");
    }

    void testFocusChange() {
        ToggleKey toggle = withKeys(defaultToggleKeys());
        Keys      keys{toggle};
        keys.press(XKB_KEY_Control_L, 0);
        keys.press(XKB_KEY_Shift_L, ControlMask);
        toggle.reset();
        keys.release(XKB_KEY_Shift_L, ControlMask | ShiftMask);
        keys.release(XKB_KEY_Control_L, ControlMask);
        check(keys.toggles == 0, "a tap that started in another field does not toggle");
    }

    void testLetterChord() {
        ToggleKey toggle = withKeys({"Alt+z"});
        Keys      keys{toggle};
        keys.press(XKB_KEY_Alt_L, 0);
        keys.press(XKB_KEY_z, Mod1Mask);
        check(keys.toggles == 1 && keys.swallowed == 1, "Alt+Z toggles on its press, which the app does not see");
        keys.press(XKB_KEY_z, Mod1Mask);
        check(keys.toggles == 1 && keys.swallowed == 2, "a held Alt+Z toggles once");
        keys.release(XKB_KEY_z, Mod1Mask);
        check(keys.swallowed == 3, "nor does the app see its release");
        keys.release(XKB_KEY_Alt_L, Mod1Mask);
        check(keys.toggles == 1 && keys.swallowed == 3, "the app sees Alt itself");
        keys.press(XKB_KEY_z, 0);
        keys.release(XKB_KEY_z, 0);
        check(keys.toggles == 1 && keys.swallowed == 3, "a plain z types");
    }

    void testNoKeys() {
        ToggleKey toggle = withKeys({});
        Keys      keys{toggle};
        keys.press(XKB_KEY_Control_L, 0);
        keys.press(XKB_KEY_Shift_L, ControlMask);
        keys.release(XKB_KEY_Shift_L, ControlMask | ShiftMask);
        keys.release(XKB_KEY_Control_L, ControlMask);
        check(keys.toggles == 0, "no toggle key set means nothing toggles");
    }

} // namespace

int main() {
    testCtrlShiftTap();
    testShortcutsDoNotToggle();
    testHold();
    testFocusChange();
    testLetterChord();
    testNoKeys();
    if (failures == 0)
        std::cout << "all toggle key checks passed\n";
    return failures == 0 ? 0 : 1;
}
