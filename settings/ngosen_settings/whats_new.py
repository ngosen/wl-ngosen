# SPDX-FileCopyrightText: 2026 Ngó Sen contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""The "What's new & tips" page: recent features and typing tips, each with a way to its setting.

FEATURES is the one list to update before a feature release. A target names what the button
shows: ("setting", key) for a setting row, ("page", name) for a whole page, or None.
"""

import random

from gi.repository import Adw, Gtk

from .i18n import N_, _

# Newest release first. A release with no entry gets no section.
FEATURES = [
    {
        "version": "1.3",
        "title": N_("English/Vietnamese per app"),
        "how": N_(
            "Ctrl+Shift and OFF in the ` menu now apply to the app in use only; an app you never "
            "switched follows your last switch."
        ),
        "example": "",
        "target": ("setting", "ToggleKey"),
    },
    {
        "version": "1.3",
        "title": N_("Your own mode menu"),
        "how": N_(
            "Reorder or hide the modes of the ` menu, and set a key that moves the app in use to "
            "the next mode."
        ),
        "example": "",
        "target": ("setting", "CycleModeKey"),
    },
    {
        "version": "1.3",
        "title": N_("Search settings"),
        "how": N_("Type in the field above the page list to find a setting; Ctrl+F gets there."),
        "example": "",
        "target": None,
    },
    {
        "version": "1.2",
        "title": N_("Type with Sen"),
        "how": N_(
            "Text goes straight into the field instead of an underlined word. Press ` to pick the "
            "underline for one app."
        ),
        "example": "",
        "target": ("setting", "DirectTyping"),
    },
    {
        "version": "1.2",
        "title": N_("Cứu chữ"),
        "how": N_(
            "Forgot to switch Vietnamese on? Press the English/Vietnamese key and the word is "
            "fixed; Tab or Esc gives back what you typed."
        ),
        "example": "vieetj → việt",
        "target": ("setting", "RetypeAfterSwitch"),
    },
    {
        "version": "1.2",
        "title": N_("Cứu dấu"),
        "how": N_(
            "Click right after a word and type a tone key to change its tone mark; Tab or Esc "
            "gives it back."
        ),
        "example": "chao + f → chào",
        "target": ("setting", "RetoneSelection"),
    },
    {
        "version": "1.2",
        "title": N_("The Check page"),
        "how": N_(
            "Shows what keeps Vietnamese typing from working on this machine, with a button to fix "
            "each item."
        ),
        "example": "",
        "target": ("page", "check"),
    },
]

TIPS = [
    {
        "text": N_("Press ` to pick Sen, Preedit or OFF for the app in use."),
        "target": ("setting", "ModeMenuKey"),
    },
    {
        "text": N_("Tap Ctrl alone to keep the word as typed: tunx, Ctrl, Space keeps tũn."),
        "target": ("setting", "LockWordModifier"),
    },
    {
        "text": N_("A macro ko → không also types Không when you type Ko."),
        "target": ("page", "macros"),
    },
    {
        "text": N_("] types ư and [ types ơ, so ch]a gives chưa. Off by default."),
        "target": ("setting", "BracketTransform"),
    },
    {
        "text": N_("Space twice types a period. Off by default."),
        "target": ("setting", "DoubleSpaceToPeriod"),
    },
    {
        "text": N_("-- types an em dash (—). Off by default."),
        "target": ("setting", "DoubleHyphenToEmDash"),
    },
    {
        "text": N_(
            "A capital letter after . ! ? and Enter. Off by default; the top bar menu's Auto "
            "Capitalize pauses it."
        ),
        "target": ("setting", "AutoCapitalizeAfterPunctuation"),
    },
    {
        "text": N_("hoà or hòa? Pick with “Use oà, uý (Instead Of òa, úy)”."),
        "target": ("setting", "ModernStyle"),
    },
    {
        "text": N_("$DATE and $TIME in a macro type today's date and time: hn → Hôm nay $DATE."),
        "target": ("page", "macros"),
    },
    {
        "text": N_("Typing wrong in some app? The Check page says why."),
        "target": ("page", "check"),
    },
    {
        "text": N_(
            "“Save typing log” in the top bar menu records what you type, to attach to a bug "
            "report."
        ),
        "target": None,
    },
    {
        "text": N_(
            "“Back Up…” and “Restore…” in the window menu keep your settings, macros and dictionary."
        ),
        "target": None,
    },
]


def random_tip():
    """A different tip each time the window opens."""
    return random.choice(TIPS)


class WhatsNewPage:
    """open_setting(key) and show_page(name) take the buttons to what they name."""

    def __init__(self, open_setting, show_page, page_titles):
        self.open_setting = open_setting
        self.show_page = show_page
        self.page_titles = page_titles
        self.page = Adw.PreferencesPage(
            title=_("What's new & tips"), icon_name="dialog-information-symbolic"
        )
        groups = {}
        for feature in FEATURES:
            version = feature["version"]
            if version not in groups:
                groups[version] = Adw.PreferencesGroup(title=_("New in {}").format(version))
                self.page.add(groups[version])
            groups[version].add(self._feature_row(feature))
        tips = Adw.PreferencesGroup(title=_("Tips"))
        self.tip_rows = []
        for tip in TIPS:
            row = Adw.ActionRow(title=_(tip["text"]), use_markup=False)
            self._add_button(row, tip["target"])
            self.tip_rows.append(row)
            tips.add(row)
        self.page.add(tips)

    def _feature_row(self, feature):
        row = Adw.ActionRow(title=_(feature["title"]), subtitle=_(feature["how"]), use_markup=False)
        if feature["example"]:
            example = Gtk.Label(
                label=feature["example"],
                css_classes=["typed"],
                valign=Gtk.Align.CENTER,
                wrap=True,
                width_chars=12,
            )
            row.add_suffix(example)
        self._add_button(row, feature["target"])
        return row

    def _add_button(self, row, target):
        if target is None:
            return
        kind, name = target
        if kind == "setting":
            label = _("Open setting")
            button = Gtk.Button(label=label, valign=Gtk.Align.CENTER)
            button.connect("clicked", lambda _b: self.open_setting(name))
        else:
            label = _("Open the {} page").format(self.page_titles[name])
            button = Gtk.Button(label=label, valign=Gtk.Align.CENTER)
            button.connect("clicked", lambda _b: self.show_page(name))
        button.target = target
        row.add_suffix(button)
