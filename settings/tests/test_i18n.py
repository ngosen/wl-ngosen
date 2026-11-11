# SPDX-FileCopyrightText: 2026 Ngó Sen contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
import os
import tempfile
import unittest
from unittest import mock

from ngosen_settings import i18n


class I18nTest(unittest.TestCase):
    def setUp(self):
        saved = i18n._translation
        self.addCleanup(setattr, i18n, "_translation", saved)

    def languages(self, settings, fcitx5=None):
        with tempfile.TemporaryDirectory() as home:
            os.makedirs(os.path.join(home, "ngosen"))
            os.makedirs(os.path.join(home, "fcitx5", "conf"))
            if settings is not None:
                with open(os.path.join(home, "ngosen", "ngosen.conf"), "w") as f:
                    f.write(settings)
            if fcitx5 is not None:
                with open(os.path.join(home, "fcitx5", "conf", "ngosen.conf"), "w") as f:
                    f.write(fcitx5)
            asked = []

            def translation(_domain, _localedir, languages, fallback):
                asked.append(languages)
                return i18n.gettext.NullTranslations()

            with (
                mock.patch.dict(os.environ, {"XDG_CONFIG_HOME": home}),
                mock.patch.object(i18n.gettext, "translation", translation),
            ):
                i18n.setup("/nowhere")
            return asked

    def test_vietnamese_unless_english_is_picked(self):
        self.assertEqual(self.languages(None), [["vi"]])
        self.assertEqual(self.languages("UiLanguage=Vietnamese\n"), [["vi"]])
        self.assertEqual(self.languages("UiLanguage=English\n"), [])
        self.assertEqual(self.languages("UiLanguage=Klingon\n"), [["vi"]])

    def test_before_the_first_import_fcitx5_picks(self):
        self.assertEqual(self.languages(None, fcitx5="UiLanguage=English\n"), [])
        self.assertEqual(
            self.languages("UiLanguage=Vietnamese\n", fcitx5="UiLanguage=English\n"), [["vi"]]
        )


if __name__ == "__main__":
    unittest.main()
