# SPDX-FileCopyrightText: 2026 Ngó Sen contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
import unittest

from ngosen_settings import keys, options


class KeysTest(unittest.TestCase):
    def test_suggestions_round_trip(self):
        for text, shown in options.TOGGLE_KEYS:
            if text:
                self.assertEqual(keys.value(keys.parse(text)), text)
                self.assertEqual(keys.label(text), shown)

    def test_pressed_keys(self):
        ctrl_shift = keys.from_pressed(["Control_L", "Shift_R"])
        self.assertEqual(keys.value(ctrl_shift), options.TOGGLE_KEYS[0][0])
        # GTK 4 reports Alt pressed after Shift as Meta.
        self.assertEqual(
            keys.label(keys.value(keys.from_pressed(["Shift_L", "Meta_L"]))), "Shift+Alt"
        )
        self.assertEqual(keys.value(keys.from_pressed(["Control_L", "grave"])), "Control+grave")
        self.assertEqual(keys.value(keys.from_pressed(["Alt_L", "Z"])), "Alt+z")
        self.assertEqual(keys.label("Alt+z"), "Alt+Z")
        self.assertIsNone(keys.from_pressed(["Control_L", "a", "b"]))

    def test_three_modifiers(self):
        text = keys.value(keys.from_pressed(["Control_L", "Shift_L", "Alt_L"]))
        self.assertEqual(len(text.split()), 6)
        self.assertEqual(keys.label(text), "Ctrl+Shift+Alt")

    def test_rules(self):
        taken = keys.gnome_chords(["<Super>space", "<Shift><Super>space"], [])
        self.assertEqual(keys.problem(keys.from_pressed(["Control_L"]), taken)[0], keys.NEEDS_TWO)
        self.assertEqual(
            keys.problem(keys.from_pressed(["Shift_L", "z"]), taken)[0], keys.TYPES_TEXT
        )
        self.assertEqual(
            keys.problem(keys.from_pressed(["Super_L", "space"]), taken)[0], keys.GNOME_USES
        )
        self.assertEqual(
            keys.problem(keys.from_pressed(["Alt_L", "z"]), taken), (None, keys.ALT_MENU)
        )
        self.assertEqual(
            keys.problem(keys.from_pressed(["Control_L", "Shift_L"]), taken), (None, None)
        )
        self.assertEqual(
            keys.problem(keys.from_pressed(["Shift_L", "Super_L", "space"]), taken)[0],
            keys.GNOME_USES,
        )

    def test_ctrl_letter_warns(self):
        self.assertEqual(
            keys.problem(keys.from_pressed(["Control_L", "c"]), set()), (None, keys.TAKES_KEY)
        )

    def test_gnome_key_names(self):
        # GNOME names the key above Tab by place, and Shift+Tab arrives as ISO_Left_Tab.
        taken = keys.gnome_chords(["<Alt>Above_Tab", "<Shift><Super>Tab"], [])
        alt_grave = keys.from_pressed(["Alt_L", "grave"])
        self.assertEqual(keys.problem(alt_grave, taken)[0], keys.GNOME_USES)
        super_tab = keys.from_pressed(["Shift_L", "Super_L", "ISO_Left_Tab"])
        self.assertEqual(keys.problem(super_tab, taken)[0], keys.GNOME_USES)
        # GNOME writes Shift shortcuts with the unshifted key; the dialog records the shifted one.
        taken = keys.gnome_chords(["<Shift><Alt>Above_Tab", "<Shift><Super>1"], [])
        alt_tilde = keys.from_pressed(["Alt_L", "Shift_L", "asciitilde"])
        self.assertEqual(keys.problem(alt_tilde, taken)[0], keys.GNOME_USES)
        super_exclam = keys.from_pressed(["Super_L", "Shift_L", "exclam"])
        self.assertEqual(keys.problem(super_exclam, taken)[0], keys.GNOME_USES)

    def test_label_iso_left_tab(self):
        self.assertEqual(keys.label("Control+Shift+ISO_Left_Tab"), "Ctrl+Shift+Tab")

    def test_shifted_symbols(self):
        self.assertEqual(keys.label("Control+Shift+exclam"), "Ctrl+Shift+!")

    def test_xkb_toggles(self):
        xkb = [
            "grp:alt_shift_toggle",
            "grp:lctrl_lshift_toggle",
            "caps:none",
            "grp:caps_toggle",
            "grp:ctrl_alt_toggle_bidir",
        ]
        self.assertEqual(
            keys.gnome_chords([], xkb),
            {
                (frozenset({"Alt", "Shift"}), None),
                (frozenset({"Control", "Shift"}), None),
                (frozenset({"Control", "Alt"}), None),
            },
        )
        self.assertIn(keys.from_pressed(["Shift_L", "Alt_L"]), keys.gnome_chords([], xkb))
