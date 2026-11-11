# SPDX-FileCopyrightText: 2026 Ngó Sen contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
import os
import tempfile
import unittest

from ngosen_settings import ini, keymap, options
from ngosen_settings.store import Engine, Store, read_text, write_text


class FakeEngine:
    def __init__(self, values, folder):
        self.values = values
        self.folder = folder

    def settings(self):
        return dict(self.values)

    def dictionary_paths(self):
        return os.path.join(self.folder, "own.dict"), os.path.join(self.folder, "bundled.dict")


class IniTest(unittest.TestCase):
    def test_round_trip(self):
        values = [
            "Telex",
            "%I:%M %p",
            'say "hi"',
            '"quoted"',
            "a\\b",
            "two\nlines",
            " padded ",
            "khô gà",
            "",
        ]
        for value in values:
            with self.subTest(value=value):
                self.assertEqual(ini.parse(f"Key={ini.escape(value)}\n")[0].get("Key"), value)

    def test_reads_values_as_fcitx5_does(self):
        self.assertEqual(ini.unescape("a\\n"), "a\\n")  # Unquoted values are literal.
        self.assertEqual(ini.unescape('"a\\tb"'), "a\tb")
        self.assertIsNone(ini.unescape('"a"b"'))
        self.assertIsNone(ini.unescape('"a\\xb"'))

    def test_parse(self):
        top, sections = ini.parse(
            '# c\n A = 1\nB="x y"\n[Macro/0]\nKey=kg\nValue="khô gà"\n[Macro/2]\nKey=z\n'
        )
        self.assertEqual(top, {"A ": " 1", "B": "x y"})
        self.assertEqual(ini.read_list(sections, "Macro"), [("kg", "khô gà")])

    def test_set_value_keeps_other_lines(self):
        text = "# note\nInputMethod=Telex\nUnknownKey=kept\n\n[Group]\nInputMethod=other\n"
        self.assertEqual(
            ini.set_value(text, "InputMethod", "VNI"),
            "# note\nInputMethod=VNI\nUnknownKey=kept\n\n[Group]\nInputMethod=other\n",
        )
        self.assertEqual(
            ini.set_value(text, "TimeFormat", "%I:%M %p"),
            '# note\nInputMethod=Telex\nUnknownKey=kept\n\nTimeFormat="%I:%M %p"\n[Group]\nInputMethod=other\n',
        )

    def test_line_separators(self):
        # The engine splits lines on \n only, so U+2028 and friends are part of a value.
        value = "a b\x85c"
        self.assertEqual(ini.parse(f"Key={value}\n")[0], {"Key": value})
        text = f"Old={value}\nInputMethod=Telex\n"
        self.assertEqual(
            ini.set_value(text, "InputMethod", "VNI"), f"Old={value}\nInputMethod=VNI\n"
        )

    def test_macro_keys(self):
        self.assertTrue(options.valid_macro_key("kg"))
        self.assertTrue(options.valid_macro_key("đt2"))
        self.assertFalse(options.valid_macro_key("k g"))
        self.assertFalse(options.valid_macro_key("k.g"))


class StoreTest(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.mkdtemp()
        self.settings = os.path.join(self.folder, "ngosen.conf")
        self.macros = os.path.join(self.folder, "ngosen-macro-table.conf")
        write_text(self.settings, "InputMethod=Telex\nFutureKey=kept\n")

    def store(self):
        return Store(FakeEngine({"InputMethod": "Telex"}, self.folder), self.folder)

    def test_set_keeps_unknown_keys_and_undoes(self):
        store = self.store()
        self.assertFalse(store.set("InputMethod", "Telex"))
        self.assertFalse(store.can_undo())
        self.assertTrue(store.set("InputMethod", "VNI"))
        self.assertEqual(read_text(self.settings), "InputMethod=VNI\nFutureKey=kept\n")
        store.undo()
        self.assertEqual(read_text(self.settings), "InputMethod=Telex\nFutureKey=kept\n")
        self.assertEqual(store.values["InputMethod"], "Telex")

    def test_macros(self):
        write_text(self.macros, "[Other]\nA=1\n[Macro/0]\nKey=ko\nValue=không\n")
        store = self.store()
        self.assertEqual(store.macros, [("ko", "không")])
        store.set_macros([("ko", "không"), ("kg", "khô gà")])
        top, sections = ini.parse(read_text(self.macros))
        self.assertEqual(ini.read_list(sections, "Macro"), [("ko", "không"), ("kg", "khô gà")])
        self.assertEqual(sections["Other"], {"A": "1"})
        store.set_macros([("kg", "khô gà")])
        self.assertNotIn("Macro/1", ini.parse(read_text(self.macros))[1])
        store.undo()
        self.assertEqual(store.macros, [("ko", "không"), ("kg", "khô gà")])
        self.assertEqual(ini.read_list(ini.parse(read_text(self.macros))[1], "Macro"), store.macros)

    def test_keymap_add_edit_remove_and_undo(self):
        store = self.store()
        self.assertEqual(store.keymap, [])
        store.set_keymap([("s", "DauSac")])
        store.set_keymap([("s", "DauSac"), ("f", "DauHuyen")])
        store.set_keymap([("s", "DauHoi"), ("f", "DauHuyen")])
        store.set_keymap([("f", "DauHuyen")])
        self.assertEqual(self.store().keymap, [("f", "DauHuyen")])
        store.undo()
        self.assertEqual(store.keymap, [("s", "DauHoi"), ("f", "DauHuyen")])
        self.assertEqual(self.store().keymap, store.keymap)

    def test_keymap_written_by_fcitx5_keeps_everything_else(self):
        keymap = os.path.join(self.folder, "ngosen-custom-keymap.conf")
        # As fcitx5 writes the list, with a section of a newer version after it.
        head = "# Written by fcitx5\n\n"
        tail = "[Future]\n# Note\nKept=1\n\n"
        write_text(
            keymap,
            head
            + "[CustomKeymap/0]\n# Key\nKey=z\n# Value\nValue=XoaDauThanh\n\n"
            + "[CustomKeymap/1]\n# Key\nKey=s\n# Value\nValue=DauSac\n\n"
            + tail,
        )
        store = self.store()
        self.assertEqual(store.keymap, [("z", "XoaDauThanh"), ("s", "DauSac")])
        store.set_keymap([("s", "DauSac")])
        self.assertEqual(
            read_text(keymap), head + "[CustomKeymap/0]\nKey=s\nValue=DauSac\n\n" + tail
        )
        store.set_keymap([])
        self.assertEqual(read_text(keymap), head + tail)

    def test_keymap_appended_after_other_lines(self):
        keymap = os.path.join(self.folder, "ngosen-custom-keymap.conf")
        write_text(keymap, "# Custom Keymap\n# CustomKeymap=\n")
        self.store().set_keymap([("\\", "D_Đ")])
        text = read_text(keymap)
        self.assertEqual(
            text, '# Custom Keymap\n# CustomKeymap=\n\n[CustomKeymap/0]\nKey="\\\\"\nValue=D_Đ\n\n'
        )
        self.assertEqual(ini.read_list(ini.parse(text)[1], "CustomKeymap"), [("\\", "D_Đ")])

    def test_keymap_presets_and_keys(self):
        for name, entries in keymap.PRESETS:
            for key, code in entries:
                self.assertIn(code, keymap.ACTION_CODES, f"{name}: {key}")

    def test_keymap_preset_keys_are_editable(self):
        # A key the editor refuses cannot be edited, and typing it again would add a second rule.
        for name, entries in keymap.PRESETS:
            for key, _code in entries:
                self.assertTrue(keymap.valid_key(key), f"{name}: {key!r}")

    def test_keymap_unchanged_keeps_undo(self):
        store = self.store()
        self.assertTrue(store.set_keymap([("s", "DauSac")]))
        self.assertFalse(store.set_keymap([("s", "DauSac")]))
        store.undo()
        self.assertEqual(store.keymap, [])
        self.assertFalse(os.path.exists(store.keymap_file))
        self.assertTrue(keymap.valid_key("é"))
        self.assertFalse(keymap.valid_key(""))
        self.assertFalse(keymap.valid_key(" "))
        self.assertFalse(keymap.valid_key("ss"))
        self.assertEqual(keymap.action_label("Unknown"), "Unknown")

    def test_invalid_utf8(self):
        with open(self.macros, "wb") as f:
            f.write(b"[Macro/0]\nKey=ko\nValue=kh\xffng\n")
        store = self.store()
        store.set_macros(store.macros + [("kg", "khô gà")])
        with open(self.macros, "rb") as f:
            self.assertIn(b"Value=kh\xffng\n", f.read())

    def test_crlf_undo_is_exact(self):
        with open(self.settings, "wb") as f:
            f.write(b"InputMethod=Telex\r\nFutureKey=kept\r\n")
        store = self.store()
        store.set("InputMethod", "VNI")
        store.undo()
        with open(self.settings, "rb") as f:
            self.assertEqual(f.read(), b"InputMethod=Telex\r\nFutureKey=kept\r\n")

    def test_undo_removes_a_file_it_created(self):
        store = self.store()
        store.set_macros([("ko", "không")])
        store.undo()
        self.assertFalse(os.path.exists(self.macros))

    def test_words(self):
        own, bundled = (
            os.path.join(self.folder, "own.dict"),
            os.path.join(self.folder, "bundled.dict"),
        )
        write_text(bundled, "việt\n\nnam\n")
        store = self.store()
        self.assertEqual(store.words, ["việt", "nam"])
        store.set_words(["việt", "nam", "ngoãy"])
        self.assertEqual(read_text(own), "việt\nnam\nngoãy\n")
        self.assertEqual(self.store().words, ["việt", "nam", "ngoãy"])
        store.undo()
        self.assertFalse(os.path.exists(own))
        self.assertEqual(store.words, ["việt", "nam"])

    def test_words_back_to_the_bundled_list_remove_the_own_file(self):
        own, bundled = (
            os.path.join(self.folder, "own.dict"),
            os.path.join(self.folder, "bundled.dict"),
        )
        write_text(bundled, "việt\nnam\n")
        write_text(own, "việt\nnam\nngoãy\n")
        store = self.store()
        store.set_words(["nam", "việt"])
        self.assertFalse(os.path.exists(own))
        store.undo()
        self.assertEqual(read_text(own), "việt\nnam\nngoãy\n")


@unittest.skipUnless(os.environ.get("NGOSEN_IBUS_ENGINE"), "needs the built engine")
class EngineTest(unittest.TestCase):
    def setUp(self):
        self.home = tempfile.mkdtemp()
        os.environ["XDG_CONFIG_HOME"] = os.path.join(self.home, "config")
        os.environ["XDG_DATA_HOME"] = os.path.join(self.home, "data")
        self.engine = Engine(os.environ["NGOSEN_IBUS_ENGINE"])

    def test_window_shows_every_setting_the_engine_reads(self):
        self.assertEqual(set(self.engine.settings()), set(options.LABELS))

    def test_engine_reads_what_the_window_writes(self):
        store = Store(self.engine)
        for key, value in [
            ("TimeFormat", "%I:%M %p"),
            ("InputMethod", "VNI"),
            ("W2U", "Everywhere"),
        ]:
            store.set(key, value)
        settings = self.engine.settings()
        self.assertEqual(settings["TimeFormat"], "%I:%M %p")
        self.assertEqual(settings["InputMethod"], "VNI")
        self.assertEqual(settings["W2U"], "Everywhere")

    def test_offered_names_are_accepted(self):
        store = Store(self.engine)
        for name in self.engine.input_methods():
            store.set("InputMethod", name)
            self.assertEqual(self.engine.settings()["InputMethod"], name)
        self.assertIn("Unicode", self.engine.charsets())

    def test_dictionary_paths(self):
        own, bundled = self.engine.dictionary_paths()
        self.assertEqual(own, os.path.join(self.home, "data", "ngosen", "vietnamese.cm.dict"))
        self.assertEqual(bundled, os.environ["NGOSEN_IBUS_DICTIONARY"])
        self.assertIn("việt", Store(self.engine).words)


if __name__ == "__main__":
    unittest.main()
