# SPDX-FileCopyrightText: 2026 Ngó Sen contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
import json
import os
import tempfile
import unittest

from ngosen_settings import backup, ini, keys
from ngosen_settings.store import Engine, Store, read_text, write_text

from .test_window import CAN_RUN, Vietnamese, window

DEFAULT_TOGGLE = "Control+Shift_L Control+Shift_R Shift+Control_L Shift+Control_R"
DEFAULTS = {
    "InputMethod": "Telex",
    "SpellCheck": "True",
    "EnableMacro": "True",
    "EnableDictionary": "False",
    "EnableCustomKeymap": "False",
    "DirectTyping": "True",
    "ToggleKey": DEFAULT_TOGGLE,
    "ModeMenuKey": "grave",
    "CycleModeKey": "",
}


class FileEngine:
    """Reads the settings file as the engine does, for the keys these tests use."""

    def __init__(self, folder):
        self.folder = folder

    def settings(self):
        top = ini.parse(read_text(os.path.join(self.folder, "ngosen.conf")) or "")[0]
        return {key: top.get(key, value) for key, value in DEFAULTS.items()}

    def dictionary_paths(self):
        return os.path.join(self.folder, "own.dict"), os.path.join(self.folder, "bundled.dict")

    def usable_toggle_keys(self, names):
        usable = set()
        for name in names:
            *held, key = name.split("+")
            modifiers = set(held) - {keys.MODIFIER_KEYS.get(key)}
            if key and modifiers and modifiers <= {"Control", "Shift", "Alt", "Super"}:
                usable.add(name)
        return usable


def fcitx5_backup(**config):
    """A backup as the fcitx5 edition writes one, with config changed by the arguments."""
    values = {
        "Mode": "Sen",
        "InputMethod": "VNI",
        "OutputCharset": "Unicode",
        "ToggleKey": {"0": "Shift+Alt_L", "1": "Alt+Shift_L"},
        "VietnameseOn": "True",
        "SettingsVersion": "3",
        "ModeMenuKey": {"0": "grave"},
        "SpellCheck": "False",
        "EnableMacro": "True",
        "EnableDictionary": "True",
        "EnableCustomKeymap": "True",
    }
    values.update(config)
    return {
        "config": values,
        "macros": [{"Key": "kg", "Value": "khô gà"}],
        "keymaps": [{"Key": "z", "Value": "Tone0"}],
        "rules": [],
        "dictionary": "việt\nnam\n",
        "meta": {"version": 1, "timestamp": "2026-10-30T10:00:00", "components": []},
    }


class UserFiles:
    """A user's files in a temporary home, the English marker included."""

    def setUp(self):
        self.home = tempfile.mkdtemp()
        os.environ["XDG_STATE_HOME"] = os.path.join(self.home, "state")
        self.folder = os.path.join(self.home, "config")
        self.engine = FileEngine(self.folder)
        write_text(os.path.join(self.folder, "ngosen.conf"), "SpellCheck=True\nFutureKey=kept\n")
        write_text(
            os.path.join(self.folder, "ngosen-macro-table.conf"),
            ini.list_text("Macro", [("dt", "điện thoại")]),
        )
        write_text(os.path.join(self.folder, "bundled.dict"), "việt\n")
        self.store = Store(self.engine, self.folder)
        self.paths = backup.Paths(self.store)

    def files(self):
        paths = self.paths
        names = (
            paths.settings,
            paths.macros,
            paths.keymaps,
            paths.dictionary,
            paths.app_modes,
            paths.app_switches,
            paths.english,
        )
        return {path: backup.read_bytes(path) for path in names}

    def settings(self):
        return ini.parse(read_text(self.paths.settings))[0]

    def restore(self, data, write=backup.put):
        changes, notes = backup.plan(data, self.paths, self.engine)
        return backup.replace(changes, write), notes

    def save(self, data):
        path = os.path.join(self.home, "backup.json")
        with open(path, "w", encoding="utf-8") as f:
            f.write(data if isinstance(data, str) else json.dumps(data))
        return path


class BackupTest(UserFiles, unittest.TestCase):
    def test_fcitx5_backup_is_restored(self):
        _, notes = self.restore(fcitx5_backup(ModeMenuKey={"0": "apostrophe"}))
        self.assertEqual(notes, [])
        settings = self.settings()
        self.assertEqual(settings["InputMethod"], "VNI")
        self.assertEqual(settings["SpellCheck"], "False")
        self.assertEqual(settings["DirectTyping"], "True")
        self.assertEqual(settings["ToggleKey"], "Shift+Alt_L Alt+Shift_L")
        self.assertEqual(settings["ModeMenuKey"], "apostrophe")
        self.assertEqual(settings["FutureKey"], "kept")
        # fcitx5-only keys stay out of the IBus file.
        self.assertNotIn("SettingsVersion", settings)
        self.assertNotIn("Mode", settings)
        macros = ini.parse(read_text(self.paths.macros))[1]
        self.assertEqual(ini.read_list(macros, "Macro"), [("kg", "khô gà")])
        keymaps = ini.parse(read_text(self.paths.keymaps))[1]
        self.assertEqual(ini.read_list(keymaps, "CustomKeymap"), [("z", "Tone0")])
        self.assertEqual(read_text(self.paths.dictionary), "việt\nnam\n")
        self.assertFalse(os.path.exists(self.paths.english))

    def test_modes(self):
        for mode, direct in (
            ("Sen", "True"),
            ("OFF", "True"),
            ("Off", "True"),
            ("Preedit", "False"),
            ("Emoji Picker", "False"),
        ):
            with self.subTest(mode):
                _, notes = self.restore(fcitx5_backup(Mode=mode))
                self.assertEqual(self.settings()["DirectTyping"], direct)
                self.assertEqual(len(notes), 1 if mode == "Emoji Picker" else 0, notes)

    def test_unknown_mode_keeps_the_current_one(self):
        write_text(self.paths.settings, "DirectTyping=True\n")
        _, notes = self.restore(fcitx5_backup(Mode="Vim"))
        self.assertEqual(self.settings()["DirectTyping"], "True")
        self.assertIn("Vim", notes[0])

    def test_english(self):
        _, notes = self.restore(fcitx5_backup(VietnameseOn="False"))
        self.assertEqual(backup.read_bytes(self.paths.english), b"")
        self.assertEqual(len(notes), 1)
        _, notes = self.restore(fcitx5_backup(VietnameseOn="True"))
        self.assertFalse(os.path.exists(self.paths.english))
        self.assertEqual(notes, [])

    def test_state_folder_is_private(self):
        self.restore(fcitx5_backup(VietnameseOn="False"))
        mode = os.stat(os.path.dirname(self.paths.english)).st_mode & 0o777
        self.assertEqual(mode, 0o700)

    def test_single_keys_are_reported_and_the_current_key_stays(self):
        write_text(self.paths.settings, "ToggleKey=Control+Alt_L\n")
        for toggle in ({"0": "F12"}, {"0": "Shift_L"}, {"0": "Shift_L", "1": "Control_R"}):
            with self.subTest(toggle):
                _, notes = self.restore(fcitx5_backup(ToggleKey=toggle))
                self.assertEqual(self.settings()["ToggleKey"], "Control+Alt_L")
                self.assertEqual(len(notes), 2)
                self.assertIn("Shift" if "Shift_L" in toggle.values() else "F12", notes[0])

    def test_mixed_keys_keep_the_usable_ones(self):
        toggle = {"0": "Shift_L", "1": "Control+Shift_L", "2": "F12", "3": "Shift+Control_L"}
        _, notes = self.restore(fcitx5_backup(ToggleKey=toggle))
        self.assertEqual(self.settings()["ToggleKey"], "Control+Shift_L Shift+Control_L")
        self.assertEqual(len(notes), 1)
        self.assertIn("F12", notes[0])
        self.assertNotIn("Control+Shift", notes[0])

    def test_switch_keys_that_gnome_or_typing_need_are_left_out(self):
        taken = keys.gnome_chords(["<Super>space"], [])
        toggle = {
            "0": "Super+space",
            "1": "Shift+a",
            "2": "Control+Shift_L",
            "3": "Control+Shift_R",
        }
        changes, notes = backup.plan(
            fcitx5_backup(ToggleKey=toggle), self.paths, self.engine, taken
        )
        backup.replace(changes)
        self.assertEqual(self.settings()["ToggleKey"], "Control+Shift_L Control+Shift_R")
        self.assertEqual(len(notes), 2)
        self.assertIn("Super+Space", notes[0])
        self.assertIn("Shift+A", notes[1])
        # Without GNOME's shortcuts, Super+space is a key like any other.
        changes, notes = backup.plan(fcitx5_backup(ToggleKey=toggle), self.paths, self.engine)
        self.assertEqual(len(notes), 1)

    def test_key_list_stops_at_the_first_gap_as_fcitx5_does(self):
        self.restore(fcitx5_backup(ToggleKey={"0": "Alt+z", "2": "Alt+x"}))
        self.assertEqual(self.settings()["ToggleKey"], "Alt+z")

    def test_empty_key_list_turns_the_key_off(self):
        for toggle in ({}, ""):
            with self.subTest(toggle):
                _, notes = self.restore(fcitx5_backup(ToggleKey=toggle))
                self.assertEqual(self.settings()["ToggleKey"], "")
                self.assertEqual(notes, [])

    def test_rules_come_back_as_the_mode_for_each_app(self):
        data = fcitx5_backup()
        data["rules"] = [{"App": "firefox", "Mode": "5"}, {"App": "kate", "Mode": "2"}]
        _, notes = self.restore(data)
        self.assertEqual(notes, [])
        self.assertEqual(
            read_text(self.paths.app_modes), backup.APP_MODES_HEADER + "firefox=5\nkate=2\n"
        )

    def test_emoji_rules_are_kept_and_reported(self):
        data = fcitx5_backup()
        data["rules"] = [{"App": "code", "Mode": "0"}, {"App": "kate", "Mode": "6"}]
        _, notes = self.restore(data)
        self.assertEqual(len(notes), 1)
        self.assertEqual(
            read_text(self.paths.app_modes), backup.APP_MODES_HEADER + "code=0\nkate=6\n"
        )

    def test_switches_come_back_for_each_app(self):
        data = fcitx5_backup()
        data["switches"] = [
            {"App": "kate", "Vietnamese": "False"},
            {"App": "firefox", "Vietnamese": True},
            {"App": "bad=name", "Vietnamese": "False"},
            {"App": "odd", "Vietnamese": "Maybe"},
        ]
        self.restore(data)
        self.assertEqual(
            read_text(self.paths.app_switches),
            "[Switches]\n[Switches/0]\nApp=kate\nVietnamese=False\n\n"
            "[Switches/1]\nApp=firefox\nVietnamese=True\n\n",
        )

    def test_rules_without_switches_clear_the_switches_as_fcitx5_does(self):
        write_text(
            self.paths.app_switches, "[Switches]\n[Switches/0]\nApp=kate\nVietnamese=False\n\n"
        )
        self.restore({"rules": [{"App": "kate", "Mode": "2"}]})
        self.assertEqual(read_text(self.paths.app_switches), "[Switches]\n")

    def test_switches_export_as_fcitx5_reads_them(self):
        write_bytes = backup.put
        write_bytes(
            self.paths.app_switches,
            b'[Switches/0]\nApp="My App"\nVietnamese=False\n\n[Switches/1]\nApp=\xff\nVietnamese=True\n\n'
            b"[Switches/3]\nApp=late\nVietnamese=True\n\n",
        )
        target = os.path.join(self.home, "out.json")
        backup.export(self.engine, self.paths, target)
        self.assertEqual(
            backup.load(target)["switches"],
            [{"App": "My App", "Vietnamese": "False"}, {"App": "\ufffd", "Vietnamese": "True"}],
        )

    def test_no_switches_write_no_file(self):
        self.restore({"switches": []})
        self.assertFalse(os.path.exists(self.paths.app_switches))

    def test_rules_that_would_break_the_file_are_skipped(self):
        data = fcitx5_backup()
        data["rules"] = [
            {"App": "a=b", "Mode": "5"},
            {"App": "x\ny", "Mode": "5"},
            {"App": "", "Mode": "5"},
            {"App": "#c", "Mode": "5"},
            {"App": "ok", "Mode": "5\nevil=2"},
        ]
        self.restore(data)
        self.assertFalse(os.path.exists(self.paths.app_modes))

    def test_empty_rules_clear_the_modes_picked_for_each_app(self):
        write_text(self.paths.app_modes, "firefox=5\n")
        self.restore({"rules": []})
        self.assertEqual(read_text(self.paths.app_modes), backup.APP_MODES_HEADER)

    def test_missing_parts_are_left_alone(self):
        before = self.files()
        _, notes = self.restore({"macros": []})
        after = self.files()
        self.assertEqual(notes, [])
        self.assertEqual(
            {p for p in before if before[p] != after[p]}, {self.paths.macros}, "only macros"
        )
        self.assertEqual(ini.read_list(ini.parse(read_text(self.paths.macros))[1], "Macro"), [])

    def test_damaged_files_are_refused_before_anything_is_written(self):
        before = self.files()
        for data in (
            "{",
            "[]",
            {"meta": {}},
            {"dictionary": 5},
            {"config": [1]},
            {"macros": 5},
            {"macros": [{"Key": 1}]},
            {"rules": [1]},
            {"rules": [{"App": 1}]},
            {"switches": [1]},
            {"switches": [{"App": 1}]},
            {"config": {}, "keymaps": None},
            '{"dictionary": "\\ud800"}',
        ):
            with self.subTest(data):
                with self.assertRaises(ValueError):
                    backup.load(self.save(data))
        for config in ({"VietnameseOn": "Maybe"}, {"InputMethod": 5}, {"ToggleKey": [1]}):
            with self.subTest(config):
                with self.assertRaises(ValueError):
                    self.restore({"config": config})
        self.assertEqual(self.files(), before)

    def test_fcitx5_files_load(self):
        data = fcitx5_backup()
        self.assertEqual(backup.load(self.save(data)), data)

    def test_failure_at_each_step_puts_every_file_back(self):
        write_text(self.paths.keymaps, "[CustomKeymap/0]\nKey=a\nValue=b\n\n")
        before = self.files()
        data = fcitx5_backup(VietnameseOn="False")
        changes, _ = backup.plan(data, self.paths, self.engine)
        steps = sum(1 for path, new in changes.items() if new != before[path])
        self.assertEqual(steps, 5)
        for fail_at in range(steps):
            for after_write in (False, True):
                with self.subTest(fail_at=fail_at, after_write=after_write):
                    calls = []

                    def write(path, data, fail_at=fail_at, after_write=after_write):
                        calls.append(path)
                        if len(calls) - 1 == fail_at and after_write:
                            backup.put(path, data)
                        if len(calls) - 1 == fail_at:
                            raise OSError(28, "No space left on device")
                        backup.put(path, data)

                    with self.assertRaises(OSError):
                        backup.replace(changes, write)
                    self.assertEqual(self.files(), before)

    def test_files_that_cannot_be_put_back_are_named(self):
        changes, _ = backup.plan(fcitx5_backup(), self.paths, self.engine)
        calls = []

        def write(path, data):
            calls.append(path)
            if len(calls) > 1:
                raise OSError(5, "Input/output error")
            backup.put(path, data)

        with self.assertRaises(backup.RestoreError) as caught:
            backup.replace(changes, write)
        # The second write failed, so it may have happened or not.
        self.assertEqual(caught.exception.paths, calls[:2])

    def test_undo_puts_back_the_exact_bytes(self):
        write_text(self.paths.settings, "# mine\r\nSpellCheck=True\n\xff")
        before = self.files()
        snapshot, _ = self.restore(fcitx5_backup(VietnameseOn="False"))
        self.assertNotEqual(self.files(), before)
        backup.replace(snapshot)
        self.assertEqual(self.files(), before)
        self.assertFalse(os.path.exists(self.paths.english))
        self.assertFalse(os.path.exists(self.paths.keymaps))
        # And back to English, where the marker was there before.
        backup.put(self.paths.english, b"")
        before = self.files()
        snapshot, _ = self.restore(fcitx5_backup())
        self.assertFalse(os.path.exists(self.paths.english))
        backup.replace(snapshot)
        self.assertEqual(self.files(), before)

    def test_cycle_key_list_from_fcitx5_is_restored(self):
        _, notes = self.restore(fcitx5_backup(CycleModeKey={"0": "Control+grave", "1": "Alt+m"}))
        self.assertEqual(notes, [])
        self.assertEqual(self.settings()["CycleModeKey"], "Control+grave Alt+m")

    def test_cycle_key_exports_as_fcitx5_key_list(self):
        write_text(self.paths.settings, "CycleModeKey=Control+grave\n")
        target = os.path.join(self.home, "out.json")
        backup.export(self.engine, self.paths, target)
        self.assertEqual(backup.load(target)["config"]["CycleModeKey"], {"0": "Control+grave"})

    def test_export_then_restore_gives_the_same_files(self):
        write_text(
            self.paths.settings,
            "InputMethod=VNI\nSpellCheck=False\nEnableMacro=True\nEnableDictionary=True\n"
            "EnableCustomKeymap=False\nDirectTyping=True\nToggleKey=Alt+z\n"
            "ModeMenuKey=Shift+asciitilde\n",
        )
        write_text(self.paths.keymaps, "[CustomKeymap/0]\nKey=a\nValue=b\n\n")
        write_text(self.paths.dictionary, "khô\ngà\n")
        write_text(self.paths.app_modes, backup.APP_MODES_HEADER + "firefox=5\ncode=0\n")
        write_text(
            self.paths.app_switches, "[Switches]\n[Switches/0]\nApp=kate\nVietnamese=False\n\n"
        )
        backup.put(self.paths.english, b"")
        before = self.files()
        target = os.path.join(self.home, "out.json")
        backup.export(self.engine, self.paths, target)
        exported = backup.load(target)
        self.assertEqual(
            exported["rules"], [{"App": "firefox", "Mode": "5"}, {"App": "code", "Mode": "0"}]
        )
        self.assertEqual(exported["switches"], [{"App": "kate", "Vietnamese": "False"}])
        self.assertEqual(exported["config"]["Mode"], "Sen")
        self.assertEqual(exported["config"]["ToggleKey"], {"0": "Alt+z"})
        self.assertEqual(exported["config"]["ModeMenuKey"], {"0": "Shift+asciitilde"})
        self.assertEqual(exported["config"]["VietnameseOn"], "False")
        self.assertNotIn("DirectTyping", exported["config"])

        self.restore(fcitx5_backup())
        self.assertNotEqual(self.files(), before)
        _, notes = self.restore(exported)
        self.assertEqual(len(notes), 1)  # Switching to English.
        self.assertEqual(self.files(), before)
        again = os.path.join(self.home, "again.json")
        backup.export(self.engine, self.paths, again)
        second = backup.load(again)
        del exported["meta"]["timestamp"], second["meta"]["timestamp"]
        self.assertEqual(second, exported)

    def test_no_app_modes_file_exports_no_rules(self):
        target = os.path.join(self.home, "out.json")
        backup.export(self.engine, self.paths, target)
        self.assertNotIn("rules", backup.load(target))

    def test_export_without_own_dictionary(self):
        target = os.path.join(self.home, "out.json")
        backup.export(self.engine, self.paths, target)
        exported = backup.load(target)
        self.assertEqual(exported["dictionary"], "việt\n")
        self.assertEqual(
            exported["meta"]["components"],
            ["config", "macros", "keymaps", "switches", "dictionary"],
        )
        # The fcitx5 edition keeps its own per-app switches when it restores this.
        self.assertEqual(exported["meta"]["edition"], "wl")
        self.assertEqual(exported["config"]["Mode"], "Sen")
        self.assertEqual(exported["config"]["VietnameseOn"], "True")
        # A list edited after the backup goes back to the packaged one.
        write_text(self.paths.dictionary, "việt\nkhô\n")
        self.restore(exported)
        self.assertFalse(os.path.exists(self.paths.dictionary))

    def test_restored_keymap_is_what_the_keymap_page_reads(self):
        self.assertEqual(self.paths.keymaps, self.store.keymap_file)
        write_text(self.paths.keymaps, "# mine\n[CustomKeymap/0]\nKey=a\nValue=DauHoi\n\n")
        self.restore(
            {"keymaps": [{"Key": "s", "Value": "DauSac"}, {"Key": "f", "Value": "DauHuyen"}]}
        )
        self.store.reload(self.engine)
        self.assertEqual(self.store.keymap, [("s", "DauSac"), ("f", "DauHuyen")])
        self.assertTrue(read_text(self.paths.keymaps).startswith("# mine\n"))
        # The page saving the same list changes nothing.
        self.assertFalse(self.store.set_keymap(self.store.keymap))

    def test_bundled_words_remove_the_own_file(self):
        write_text(self.paths.bundled_dictionary, "việt\nnam\n")
        write_text(self.paths.dictionary, "việt\nkhô\n")
        self.restore({"dictionary": "nam\n\nviệt\n"})
        self.assertFalse(os.path.exists(self.paths.dictionary))
        self.restore({"dictionary": "nam\nkhô\n"})
        self.assertEqual(read_text(self.paths.dictionary), "nam\nkhô\n")

    def test_undo_only_while_the_files_hold_the_restore(self):
        before = self.files()
        changes, _ = backup.plan(fcitx5_backup(VietnameseOn="False"), self.paths, self.engine)
        snapshot = backup.replace(changes)
        # The language switched back from the menu after the restore.
        backup.put(self.paths.english, None)
        changed = self.files()
        self.assertFalse(backup.undo(snapshot, changes))
        self.assertEqual(self.files(), changed)
        backup.put(self.paths.english, b"")
        self.assertTrue(backup.undo(snapshot, changes))
        self.assertEqual(self.files(), before)


@unittest.skipUnless(CAN_RUN, "needs GTK 4, libadwaita and a display")
class WindowBackupTest(UserFiles, unittest.TestCase):
    def setUp(self):
        window.Adw.init()
        window.i18n._translation = Vietnamese()
        super().setUp()
        names = {"InputMethod": ["Telex", "VNI"], "OutputCharset": ["Unicode"]}
        self.window = window.Window(None, self.store, names, "1.0.0", self.engine)

    def test_restore_then_undo(self):
        before = self.files()
        self.assertTrue(self.window.rows.rows["SpellCheck"].get_active())
        self.assertIsNone(self.window.restore_backup(self.save(fcitx5_backup())))
        self.assertEqual(self.window.toast.get_title(), "Restored from backup.json.")
        self.assertFalse(self.window.rows.rows["SpellCheck"].get_active())
        self.assertEqual(self.store.macros, [("kg", "khô gà")])
        self.assertEqual(self.store.keymap, [("z", "Tone0")])
        self.assertIsNotNone(self.window.keymap.list.get_row_at_index(0))
        self.window.toast.emit("button-clicked")
        self.assertEqual(self.files(), before)
        self.assertTrue(self.window.rows.rows["SpellCheck"].get_active())
        self.assertEqual(self.store.macros, [("dt", "điện thoại")])
        self.assertEqual(self.store.keymap, [])
        self.assertIsNone(self.window.keymap.list.get_row_at_index(0))

    def test_losses_are_asked_about_first(self):
        before = self.files()
        path = self.save(fcitx5_backup(Mode="Emoji Picker"))
        dialog = self.window.restore_backup(path)
        self.assertIn("emoji", dialog.get_body())
        self.assertEqual(self.files(), before)
        dialog.emit("response", "cancel")
        self.assertEqual(self.files(), before)
        self.window.restore_backup(path).emit("response", "restore")
        self.assertNotEqual(self.files(), before)
        self.assertEqual(self.window.toast.get_title(), "Restored from backup.json.")

    def test_undo_skipped_after_a_later_change(self):
        self.window.restore_backup(self.save(fcitx5_backup()))
        undo = self.window.toast
        self.window.save("SpellCheck", "True")
        changed = self.files()
        undo.emit("button-clicked")
        self.assertEqual(self.files(), changed)
        self.assertIn("nothing was undone", self.window.toast.get_title())

    def test_file_dialog_errors(self):
        def fail(code):
            def finish(_result):
                raise window.GLib.Error.new_literal(window.Gtk.dialog_error_quark(), "boom", code)

            return finish

        used = []
        self.window._on_file(None, None, fail(window.Gtk.DialogError.DISMISSED), used.append)
        self.assertIsNone(self.window.toast)
        self.window._on_file(None, None, fail(window.Gtk.DialogError.FAILED), used.append)
        self.assertIn("boom", self.window.toast.get_title())
        self.assertEqual(used, [])

    def test_damaged_file_changes_nothing(self):
        before = self.files()
        self.window.restore_backup(self.save('{"config": [1]}'))
        self.assertIn("Nothing was changed", self.window.toast.get_title())
        self.assertEqual(self.files(), before)

    def test_back_up(self):
        target = os.path.join(self.home, "out.json")
        self.window.export_backup(target)
        self.assertEqual(self.window.toast.get_title(), f"Backed up to {target}.")
        self.assertEqual(backup.load(target)["macros"], [{"Key": "dt", "Value": "điện thoại"}])


@unittest.skipUnless(os.environ.get("NGOSEN_WL_DAEMON"), "needs the built engine")
class EngineBackupTest(unittest.TestCase):
    def setUp(self):
        self.home = tempfile.mkdtemp()
        for name in ("CONFIG", "DATA", "STATE"):
            os.environ[f"XDG_{name}_HOME"] = os.path.join(self.home, name.lower())
        self.engine = Engine(os.environ["NGOSEN_WL_DAEMON"])
        self.store = Store(self.engine)
        self.paths = backup.Paths(self.store)

    def test_usable_toggle_keys_are_the_ones_the_engine_switches_with(self):
        names = ["Control+Shift_L", "Shift_L", "F12", "a", "Alt+z", "Super+space", "Foo+a", ""]
        self.assertEqual(
            self.engine.usable_toggle_keys(names), {"Control+Shift_L", "Alt+z", "Super+space"}
        )

    def test_engine_reads_a_restored_fcitx5_backup(self):
        data = fcitx5_backup(Mode="OFF", ToggleKey={"0": "Shift_L", "1": "Alt+z"})
        changes, notes = backup.plan(data, self.paths, self.engine)
        backup.replace(changes)
        self.assertEqual(len(notes), 1)
        settings = self.engine.settings()
        self.assertEqual(settings["DirectTyping"], "True")
        self.assertEqual(settings["ToggleKey"], "Alt+z")
        self.assertEqual(settings["InputMethod"], "VNI")
        self.assertEqual(settings["EnableCustomKeymap"], "True")

    def test_export_round_trip_through_the_engine(self):
        backup.replace(backup.plan(fcitx5_backup(), self.paths, self.engine)[0])
        target = os.path.join(self.home, "out.json")
        backup.export(self.engine, self.paths, target)
        exported = backup.load(target)
        before = {p: backup.read_bytes(p) for p in (self.paths.settings, self.paths.macros)}
        backup.replace(backup.plan(fcitx5_backup(Mode="Preedit"), self.paths, self.engine)[0])
        backup.replace(backup.plan(exported, self.paths, self.engine)[0])
        self.assertEqual({p: backup.read_bytes(p) for p in before}, before)
        # Every key the engine reads, under the fcitx5 edition's names.
        expected = set(self.engine.settings()) - {"DirectTyping"} | {"Mode", "VietnameseOn"}
        self.assertEqual(set(exported["config"]), expected)


if __name__ == "__main__":
    unittest.main()
