# SPDX-FileCopyrightText: 2026 Ngó Sen contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
import os
import tempfile
import time
import unittest
from unittest import mock

from ngosen_settings import ini, keymap
from ngosen_settings.store import Store, read_text, write_text

try:
    from ngosen_settings import window  # Picks the GTK and libadwaita versions first.

    Adw, GLib = window.Adw, window.GLib
    from gi.repository import Pango
except (ImportError, ValueError):
    window = None


class Vietnamese:
    """Labels that differ from the saved values, as they do in the real window."""

    def gettext(self, text):
        # A real catalog answers the empty string with its header.
        if not text:
            return "Project-Id-Version: wl-ngosen"
        return {"Everywhere": "Mọi nơi", "Non-Start": "Trong từ", "English": "Tiếng Anh"}.get(
            text, text
        )


class FakeEngine:
    def __init__(self, folder):
        self.folder = folder

    def dictionary_paths(self):
        return os.path.join(self.folder, "own.dict"), os.path.join(self.folder, "bundled.dict")

    def settings(self):
        return {
            "InputMethod": "Telex",
            "OutputCharset": "Unicode",
            "SpellCheck": "True",
            "W2U": "Non-Start",
            "TimeFormat": "%H:%M:%S %Z",
            "EnableMacro": "True",
            "EnableDictionary": "True",
            "ModeMenuKey": "grave",
            "ShortcutSen": "1",
            "ShortcutPreedit": "q",
            "ShortcutOff": "e",
            "UiLanguage": "Vietnamese",
        }


CAN_RUN = bool(window and os.environ.get("DISPLAY"))
# CI sets this, so a missing package fails the run instead of skipping these tests.
REQUIRED = os.environ.get("NGOSEN_WINDOW_TESTS") == "required"


def descendants(widget):
    child = widget.get_first_child()
    while child is not None:
        yield child
        yield from descendants(child)
        child = child.get_next_sibling()


def labels(widget):
    child = widget.get_first_child()
    while child is not None:
        if isinstance(child, window.Gtk.Label):
            yield child
        yield from labels(child)
        child = child.get_next_sibling()


class RequiredTest(unittest.TestCase):
    @unittest.skipUnless(REQUIRED, "not required here")
    def test_window_tests_run(self):
        self.assertTrue(CAN_RUN, "the window tests need GTK 4, libadwaita and xvfb-run")


@unittest.skipUnless(CAN_RUN, "needs GTK 4, libadwaita and a display")
class WindowTest(unittest.TestCase):
    """Builds the real window on the oldest libadwaita CI has, and saves through its rows."""

    def setUp(self):
        Adw.init()
        window.i18n._translation = Vietnamese()
        self.folder = tempfile.mkdtemp()
        write_text(os.path.join(self.folder, "ngosen.conf"), "SpellCheck=True\nFutureKey=kept\n")
        write_text(os.path.join(self.folder, "bundled.dict"), "việt\nnam\n")
        self.store = Store(FakeEngine(self.folder), self.folder)
        names = {
            "InputMethod": ["Telex", "VNI", "Telex + VNI"],
            "OutputCharset": ["Unicode", "TCVN3 (ABC)"],
        }
        self.window = window.Window(None, self.store, names, "1.0.0")

    def settings(self):
        return ini.parse(read_text(self.store.settings_file))[0]

    def test_settings_this_edition_lacks_are_not_shown(self):
        rows = self.window.rows.rows
        for key in (
            "ModeMenuKey",
            "CycleModeKey",
            "ShortcutSen",
            "ShowModeVim",
            "RetoneSelection",
            "RetypeAfterSwitch",
        ):
            self.assertNotIn(key, rows)
        for key in ("ToggleKey", "ModeOrder", "ShowModeSen", "ShowMenuCharset"):
            self.assertIn(key, rows)

    def test_switch_saves_and_undoes(self):
        self.window.rows.rows["SpellCheck"].set_active(False)
        self.assertEqual(self.settings(), {"SpellCheck": "False", "FutureKey": "kept"})
        self.assertIsNotNone(self.window.toast)
        self.window.undo()
        self.assertEqual(self.settings()["SpellCheck"], "True")
        self.assertTrue(self.window.rows.rows["SpellCheck"].get_active())

    def test_mode_buttons_save_sen_or_preedit(self):
        buttons = self.window.rows.mode_buttons
        row = self.window.rows.rows["DirectTyping"]
        self.assertEqual([b.get_label() for b in buttons.values()], ["Sen", "Preedit"])
        # No saved value types with Sen, as the engine does.
        self.assertTrue(buttons["True"].get_active())
        self.assertIn("straight", row.get_subtitle())
        buttons["False"].set_active(True)
        self.assertEqual(self.settings()["DirectTyping"], "False")
        self.assertFalse(buttons["True"].get_active())
        self.assertIn("underlined", row.get_subtitle())
        self.window.undo()
        self.assertNotIn("DirectTyping", self.settings())
        self.window.refresh()
        self.assertTrue(buttons["True"].get_active())

    def test_combo_saves_the_value_not_the_label(self):
        self.window.rows.rows["W2U"].set_selected(2)
        self.window.rows.rows["InputMethod"].set_selected(2)
        self.assertEqual(self.settings()["W2U"], "Everywhere")
        self.assertEqual(self.settings()["InputMethod"], "Telex + VNI")

    def test_mode_order_saved_without_vim_is_its_preset(self):
        engine = FakeEngine(self.folder)
        saved = engine.settings()
        saved["ModeOrder"] = "Off,Sen,Preedit,Emoji"
        engine.settings = lambda: saved
        store = Store(engine, self.folder)
        win = window.Window(
            None,
            store,
            {"InputMethod": ["Telex"], "OutputCharset": ["Unicode"]},
            "1.0.0",
        )
        model = win.rows.rows["ModeOrder"].get_model()
        self.assertEqual(model.get_n_items(), 3)
        self.assertEqual(
            model.get_string(win.rows.rows["ModeOrder"].get_selected()), "OFF, Sen, Preedit"
        )

    def test_languages_are_named_in_their_own_language(self):
        model = self.window.rows.rows["UiLanguage"].get_model()
        self.assertEqual(
            [model.get_string(i) for i in range(model.get_n_items())], ["Tiếng Việt", "English"]
        )

    def test_hand_written_value_is_kept(self):
        row = self.window.rows.rows["TimeFormat"]
        self.assertEqual(row.get_model().get_string(row.get_selected()), "%H:%M:%S %Z")
        self.assertNotIn("TimeFormat", self.settings())

    def test_macro_add_and_remove(self):
        macros = self.window.macros
        macros.key_row.set_text("k g")
        self.assertFalse(macros.add_button.get_sensitive())
        macros.key_row.set_text("kg")
        macros.value_row.set_text("khô gà")
        macros._add()
        self.assertEqual(self.store.macros, [("kg", "khô gà")])
        self.assertEqual(macros.key_row.get_text(), "")
        macros._remove("kg")
        self.assertEqual(self.store.macros, [])
        self.window.undo()
        self.assertEqual(self.store.macros, [("kg", "khô gà")])
        self.assertIsNotNone(macros.list.get_row_at_index(0))

    def test_keymap_add_edit_remove(self):
        page = self.window.keymap
        for bad in ("ss", " "):
            page.key_row.set_text(bad)
            self.assertFalse(page.add_button.get_sensitive(), bad)
            self.assertTrue(page.key_row.has_css_class("error"), bad)
        page.key_row.set_text("s")
        page.action.set_selected(keymap.ACTION_CODES.index("DauSac"))
        page._add()
        self.assertEqual(self.store.keymap, [("s", "DauSac")])
        self.assertEqual(page.key_row.get_text(), "")
        self.assertEqual(page.list.get_row_at_index(0).get_subtitle(), "Sắc tone")
        page.list.get_row_at_index(0).activate()
        self.assertEqual(page.key_row.get_text(), "s")
        page.action.set_selected(keymap.ACTION_CODES.index("DauHoi"))
        page._add()
        self.assertEqual(self.store.keymap, [("s", "DauHoi")])
        page._remove("s")
        self.assertEqual(self.store.keymap, [])
        self.window.undo()
        self.assertEqual(self.store.keymap, [("s", "DauHoi")])
        self.assertEqual(page.list.get_row_at_index(0).get_title(), "s")

    def test_keymap_duplicate_key_updates(self):
        page = self.window.keymap
        page.key_row.set_text("s")
        page._add()
        page.key_row.set_text("s")
        self.assertEqual(page.add_button.get_label(), "Update")
        self.assertIn("Update replaces it", page.editor.get_description())
        page.action.set_selected(keymap.ACTION_CODES.index("DauNga"))
        page._add()
        self.assertEqual(self.store.keymap, [("s", "DauNga")])
        self.assertFalse(page.editor.get_description())

    def test_keymap_preset_and_switch(self):
        page = self.window.keymap
        page.preset.set_selected(1)
        page.apply_preset()
        self.assertEqual(self.store.keymap, keymap.PRESETS[1][1])
        self.window.undo()
        self.assertEqual(self.store.keymap, [])
        self.window.rows.rows["EnableCustomKeymap"].set_active(True)
        self.assertEqual(self.settings()["EnableCustomKeymap"], "True")

    def test_keymap_hint_escapes_markup(self):
        page = self.window.keymap
        self.store.set_keymap([("&", "XoaDauThanh")])
        page.refresh()
        page.key_row.set_text("&")
        # What the label shows: libadwaita 1.5 returns the parsed text from
        # get_description(), and a label whose markup fails to parse keeps its old text.
        shown = [label.get_text() for label in labels(page.editor)]
        self.assertIn("& already does “Remove the tone mark”; Update replaces it.", shown)

    def test_keymap_unknown_action_is_not_overwritten(self):
        page = self.window.keymap
        self.store.set_keymap([("q", "FutureAction")])
        page.refresh()
        page.action.set_selected(keymap.ACTION_CODES.index("DauSac"))
        page.list.get_row_at_index(0).activate()
        self.assertEqual(page.key_row.get_text(), "q")
        shown = page.action.get_model().get_string(page.action.get_selected())
        self.assertEqual(shown, "FutureAction")
        self.assertFalse(page.add_button.get_sensitive())
        page._add()
        self.assertEqual(self.store.keymap, [("q", "FutureAction")])
        page.action.set_selected(keymap.ACTION_CODES.index("DauNga"))
        self.assertTrue(page.add_button.get_sensitive())
        page._add()
        self.assertEqual(self.store.keymap, [("q", "DauNga")])
        self.assertEqual(page.action.get_model().get_n_items(), len(keymap.ACTION_CODES))

    def test_keymap_same_preset_twice_keeps_undo(self):
        page = self.window.keymap
        page.preset.set_selected(1)
        page.apply_preset()
        toast = self.window.toast
        page.apply_preset()
        self.assertIs(self.window.toast, toast)
        self.window.undo()
        self.assertEqual(self.store.keymap, [])

    def test_dictionary_add_and_remove(self):
        page = self.window.dictionary
        page.word_row.set_text("ngo ãy")
        self.assertFalse(page.add_button.get_sensitive())
        page.word_row.set_text("việt")
        self.assertFalse(page.add_button.get_sensitive())
        page.word_row.set_text("ngoãy")
        page._add()
        self.assertEqual(self.store.words, ["việt", "nam", "ngoãy"])
        self.assertEqual(page.list.get_row_at_index(0).get_title(), "ngoãy")
        page._remove("ngoãy")
        self.assertFalse(os.path.exists(self.store.dictionary_file))
        self.window.undo()
        self.assertEqual(read_text(self.store.dictionary_file), "việt\nnam\nngoãy\n")

    def test_dictionary_says_when_it_has_no_words(self):
        page = self.window.dictionary
        self.assertEqual(page.list_group.get_description(), "2 words. Search to see them.")
        page.search.set_text("zzz")
        page._show()
        self.assertEqual(page.list_group.get_description(), "No words found.")
        page.search.set_text("")
        self.window.save_words([])
        page._show()
        self.assertEqual(page.list_group.get_description(), "No words yet.")

    def test_dictionary_list_follows_the_switch(self):
        page = self.window.dictionary
        self.assertTrue(page.editor.get_sensitive())
        self.window.rows.rows["EnableDictionary"].set_active(False)
        self.assertFalse(page.editor.get_sensitive())

    def capture(self, *names):
        """Picks Other keys, then holds the keys named and lets them go."""
        row = self.window.rows.rows["ToggleKey"]
        row.set_selected(row.get_model().get_n_items() - 1)
        capture = self.window.rows.capture
        for code, name in enumerate(names):
            capture.press(name, code)
        for code in range(len(names)):
            capture.release(code)
        return capture

    def test_toggle_key_from_pressed_keys(self):
        capture = self.capture("Control_L", "grave")
        self.assertEqual(capture.shown.get_label(), "Ctrl+`")
        capture.dialog.emit("response", "save")
        self.assertEqual(self.settings()["ToggleKey"], "Control+grave")
        row = self.window.rows.rows["ToggleKey"]
        self.assertEqual(row.get_model().get_string(row.get_selected()), "Ctrl+`")

    def test_toggle_key_same_as_a_suggestion(self):
        self.capture("Shift_L", "Meta_L").dialog.emit("response", "save")
        self.assertEqual(self.settings()["ToggleKey"], window.options.TOGGLE_KEYS[1][0])

    def test_toggle_key_refused(self):
        row = self.window.rows.rows["ToggleKey"]
        before = row.get_selected()
        for names in (
            ["Control_L"],
            ["Shift_L", "a"],
        ):
            capture = self.capture(*names)
            self.assertFalse(capture.dialog.get_response_enabled("save"), names)
            capture.dialog.emit("response", "cancel")
        self.assertNotIn("ToggleKey", self.settings())
        self.assertEqual(row.get_selected(), before)

    def test_toggle_key_note_empty_without_problem(self):
        self.assertEqual(self.capture("Control_L", "Shift_L").note.get_label(), "")

    def test_toggle_key_lost_release(self):
        row = self.window.rows.rows["ToggleKey"]
        row.set_selected(row.get_model().get_n_items() - 1)
        capture = self.window.rows.capture
        capture.press("Alt_L", 9)
        # Alt+Tab to another window: the release of Alt goes there.
        window.Gtk.Widget.notify(self.window, "is-active")
        capture.press("Control_L", 1)
        capture.press("Shift_L", 2)
        capture.release(1)
        capture.release(2)
        self.assertEqual(capture.shown.get_label(), "Ctrl+Shift")

    def test_toggle_key_navigation_keys_pass(self):
        capture = self.capture()
        self.assertFalse(capture.press("Tab", 1))
        capture.release(1)
        self.assertEqual(capture.shown.get_label(), "")
        self.assertTrue(capture.press("Control_L", 2))
        self.assertTrue(capture.press("Tab", 1))

    def test_toggle_key_shifted_repeat(self):
        row = self.window.rows.rows["ToggleKey"]
        row.set_selected(row.get_model().get_n_items() - 1)
        capture = self.window.rows.capture
        capture.press("Control_L", 0)
        capture.press("Shift_L", 1)
        capture.press("exclam", 2)
        for code in range(3):
            capture.release(code)
        self.assertEqual(capture.shown.get_label(), "Ctrl+Shift+!")
        capture.dialog.emit("response", "save")
        self.assertEqual(self.settings()["ToggleKey"], "Control+Shift+exclam")

    def test_toggle_key_modifiers_first(self):
        # The engine gets the key as the modifiers held at its press make it.
        capture = self.capture("Control_L", "1", "Shift_L")
        self.assertFalse(capture.dialog.get_response_enabled("save"))
        self.assertEqual(capture.note.get_label(), window.pages.keys.MODIFIERS_FIRST)

    def test_toggle_key_forget_ends_navigation(self):
        capture = self.capture()
        capture.press("Shift_L", 0)
        capture.press("ISO_Left_Tab", 1)
        window.Gtk.Widget.notify(self.window, "is-active")
        capture.press("Control_L", 2)
        capture.press("Shift_L", 3)
        capture.release(2)
        capture.release(3)
        self.assertEqual(capture.shown.get_label(), "Ctrl+Shift")

    def test_toggle_key_shift_tab_navigates(self):
        capture = self.capture()
        capture.press("Shift_L", 0)
        self.assertFalse(capture.press("ISO_Left_Tab", 1))
        capture.release(1)
        capture.press("Control_L", 2)
        capture.release(2)
        capture.release(0)
        self.assertEqual(capture.note.get_label(), "")
        self.assertFalse(capture.dialog.get_response_enabled("save"))

    def test_toggle_key_unnamed_key(self):
        capture = self.capture()
        capture.press(None, 3)
        capture.press("Tab", 1)
        capture.release(1)
        capture.release(3)
        capture.press("Control_L", 4)
        capture.press("Shift_L", 5)
        capture.release(4)
        capture.release(5)
        self.assertEqual(capture.shown.get_label(), "Ctrl+Shift")

    def test_toggle_key_stray_release(self):
        capture = self.capture()
        capture.release(5)
        self.assertEqual(capture.note.get_label(), "")
        self.assertFalse(capture.dialog.get_response_enabled("save"))

    def test_toggle_key_alt_letter_warns(self):
        capture = self.capture("Alt_L", "z")
        self.assertTrue(capture.dialog.get_response_enabled("save"))
        self.assertIn("menu bar", capture.note.get_label())

    def test_about_is_in_the_menu_not_a_tab(self):
        tabs = self.window.stack.get_pages()
        titles = [tabs.get_item(i).get_title() for i in range(tabs.get_n_items())]
        self.assertEqual(titles[-1], "What's new & tips")
        self.assertNotIn("About", titles)
        self.assertIsNotNone(self.window.lookup_action("about"))
        dialog = self.window.show_about("1.0.0")
        self.assertEqual(dialog.get_version(), "1.0.0")
        self.assertEqual(dialog.get_license_type(), window.Gtk.License.GPL_3_0)
        for name, _url in (window.pages.FORK, window.pages.VMK):
            self.assertIn(name, dialog.get_comments())
        dialog.close()

    def test_pages_are_listed_in_a_sidebar(self):
        tabs = self.window.stack.get_pages()
        titles = [tabs.get_item(i).get_title() for i in range(tabs.get_n_items())]
        sidebar = self.window.sidebar
        names = []
        row = sidebar.get_row_at_index(0)
        while row is not None:
            names.append(row.get_child().get_last_child().get_label())
            row = sidebar.get_row_at_index(len(names))
        self.assertEqual(names, titles)
        sidebar.select_row(sidebar.get_row_at_index(len(titles) - 1))
        self.assertIs(self.window.stack.get_visible_child(), self.window.whats_new.page)
        self.assertEqual(self.window.content.get_title(), "What's new & tips")
        # Shown another way, the page still names itself and its row follows.
        self.window.stack.set_visible_child(self.window.macros.page)
        self.assertEqual(self.window.content.get_title(), titles[1])
        self.assertEqual(sidebar.get_selected_row().get_index(), 1)

    def search(self, text):
        self.window.search_entry.set_text(text)
        self.window._on_search(self.window.search_entry)
        results, i = [], 0
        while (row := self.window.results.get_row_at_index(i)) is not None:
            results.append(row)
            i += 1
        return results

    def test_a_found_setting_opens_on_its_page_with_the_focus(self):
        spell = self.window.rows.rows["SpellCheck"]
        results = self.search("spell check dictionary")
        self.assertEqual([r.found[0] for r in results if hasattr(r, "found")], [spell])
        self.assertEqual(self.window.side_stack.get_visible_child_name(), "results")
        self.window.stack.set_visible_child(self.window.whats_new.page)
        self.window.search_entry.emit("activate")
        self.assertIs(
            self.window.stack.get_visible_child(), spell.get_ancestor(Adw.PreferencesPage)
        )
        self.assertIs(self.window.get_focus(), spell)
        self.assertEqual(self.window.side_stack.get_visible_child_name(), "pages")
        self.assertEqual(self.window.search_entry.get_text(), "")

    def test_search_ignores_tone_marks_and_case(self):
        self.assertEqual(window.search.fold("Chính TẢ Đúng"), "chinh ta dung")
        self.assertTrue(any(hasattr(r, "found") for r in self.search("SPELL")))

    def test_enter_right_after_typing_opens_what_was_typed(self):
        self.search("macro")
        self.window.search_entry.set_text("spell check dictionary")
        self.window.search_entry.emit("activate")
        self.assertIs(self.window.get_focus(), self.window.rows.rows["SpellCheck"])

    def test_the_users_own_entries_are_not_settings(self):
        # A macro row's title is its abbreviation.
        self.window.macros.key_row.set_text("zqx")
        self.window.macros.value_row.set_text("khô gà")
        self.window.macros._add()
        (result,) = self.search("zqx")
        self.assertFalse(hasattr(result, "found"))
        self.assertEqual(result.get_child().get_label(), "No settings found")

    def test_find_shows_the_sidebar_of_a_narrow_window(self):
        self.window.split.set_collapsed(True)
        self.window.split.set_show_content(True)
        self.window.focus_search()
        self.assertFalse(self.window.split.get_show_content())
        self.assertIs(
            self.window.get_focus().get_ancestor(window.Gtk.SearchEntry), self.window.search_entry
        )

    def test_every_whats_new_button_leads_somewhere(self):
        targets = [f["target"] for f in window.whats_new.FEATURES + window.whats_new.TIPS]
        for kind, name in filter(None, targets):
            if kind == "setting":
                self.assertTrue(self.window.open_setting(name), name)
                self.assertIs(self.window.get_focus(), self.window.rows.rows[name])
            else:
                self.window.show_page(name)
                self.assertIs(self.window.stack.get_visible_child(), self.window.named_pages[name])

    def test_the_tip_opens_the_tips(self):
        self.window.typing.tip_row.emit("activated")
        self.assertIs(self.window.stack.get_visible_child(), self.window.whats_new.page)
        self.assertIs(self.window.get_focus(), self.window.whats_new.tip_rows[0])

    def test_button_labels_never_break(self):
        page = self.window.whats_new.page
        buttons = [w for w in descendants(page) if isinstance(w, window.Gtk.Button)]
        self.assertTrue(any("{}" not in b.get_child().get_label() for b in buttons))
        for button in buttons:
            label = button.get_child()
            self.assertTrue(
                not label.get_wrap() or label.get_width_chars() >= len(label.get_label()),
                label.get_label(),
            )

    def test_page_from_the_command_line(self):
        self.assertEqual(window.requested_page(["x", "--page", "whats-new"]), "whats-new")
        self.assertIsNone(window.requested_page(["x", "--page"]))
        self.assertIsNone(window.requested_page(["x"]))
        self.window.show_page("whats-new")
        self.assertIs(self.window.stack.get_visible_child(), self.window.whats_new.page)
        self.window.show_page("no-such-page")
        self.assertIs(self.window.stack.get_visible_child(), self.window.whats_new.page)

    def test_whats_new_and_tips_are_not_settings(self):
        self.assertFalse([r for r in self.search("per app") if hasattr(r, "found")])
        # The tip above the typing settings is picked at random; a short one is short enough to match.
        short = {"text": "Space twice types a period.", "target": None}
        with mock.patch.object(window.whats_new, "random_tip", return_value=short):
            self.window.destroy()
            self.window = window.Window(None, self.store, self.window.rows.names, "1.0.0")
        self.assertFalse([r for r in self.search("space twice") if hasattr(r, "found")])

    def test_chosen_value_is_never_cut(self):
        # A wrapped subtitle can leave the value no room, so "Ctrl+Shift" showed as "C…".
        self.window.save(
            "ToggleKey", "Control+Shift_L Control+Shift_R Shift+Control_L Shift+Control_R"
        )
        self.window.rows.refresh()
        self.window.present()
        row = self.window.rows.rows["ToggleKey"]
        context = GLib.MainContext.default()
        shown = []
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if not context.iteration(False):
                time.sleep(0.01)
            shown = [
                label
                for label in labels(row)
                if label.get_text() == "Ctrl+Shift"
                and label.get_ancestor(window.Gtk.Popover) is None
            ]
            if shown:
                break
        self.assertEqual(len(shown), 1, [label.get_text() for label in labels(row)])
        self.assertEqual(shown[0].get_ellipsize(), Pango.EllipsizeMode.NONE)

    def tearDown(self):
        self.window.destroy()
        context = GLib.MainContext.default()
        while context.pending():
            context.iteration(False)


if __name__ == "__main__":
    unittest.main()
