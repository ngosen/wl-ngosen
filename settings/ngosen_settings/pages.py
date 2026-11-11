# SPDX-FileCopyrightText: 2026 Ngó Sen contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""The pages of the window. Each control saves as soon as it changes."""

from gi.repository import Adw, Gdk, GLib, Gtk, Pango

from . import keymap, keys, options
from .i18n import N_, _

# The last toggle key choice, which asks for keys instead of saving.
OTHER_KEYS = "\0other"
# Alone, or with Shift, these move between the buttons of the dialog instead of being recorded.
NAVIGATION = {"Tab", "ISO_Left_Tab", "Return", "KP_Enter", "Escape"}


def _whole_value_factory():
    """Shows the chosen value in full; libadwaita's label shrinks to "C…" beside a wrapped subtitle."""
    factory = Gtk.SignalListItemFactory()
    factory.connect("setup", lambda _f, item: item.set_child(Gtk.Label(xalign=1)))
    factory.connect(
        "bind", lambda _f, item: item.get_child().set_label(item.get_item().get_string())
    )
    return factory


class SettingRows:
    """Builds a row for each setting and puts the saved values back into them."""

    def __init__(self, store, names, on_change, taken_keys=set):
        self.store = store
        self.names = names  # Values the engine accepts for InputMethod and OutputCharset.
        self.on_change = on_change
        self.taken_keys = taken_keys  # GNOME's shortcuts, which a toggle key may not use.
        self.loading = False
        self.rows = {}
        self.pairs = {}  # What each combo row lists, as (value, label).

    def build(self, key):
        if key in options.SHORTCUTS:
            row = Adw.EntryRow()
            row.connect("changed", lambda r: self._shortcut_changed(key, r))
        elif key == "DirectTyping":
            row = self._mode_row(key)
        elif options.is_switch(key):
            row = Adw.SwitchRow()
            row.connect(
                "notify::active",
                lambda r, _p: self._changed(key, "True" if r.get_active() else "False"),
            )
        else:
            row = Adw.ComboRow()
            # The default factory still draws the popup list, with its check marks.
            row.set_list_factory(row.get_factory())
            row.set_factory(_whole_value_factory())
            row.connect("notify::selected", lambda r, _p: self._changed(key, self._choice(key, r)))
        row.set_title(_(options.LABELS[key]))
        if key in options.HINTS:
            row.set_subtitle(_(options.HINTS[key]))
        self.rows[key] = row
        return row

    def _mode_row(self, key):
        """Linked buttons, as the fcitx5 edition shows its Mode, so the other choice is in view."""
        row = Adw.ActionRow()
        box = Gtk.Box(css_classes=["linked"], valign=Gtk.Align.CENTER)
        first = None
        self.mode_buttons = {}
        for value, label, _hint in options.TYPING_MODES:
            button = Gtk.ToggleButton(label=_(label), css_classes=["mode-button"])
            button.set_group(first)
            first = first or button
            button.connect("toggled", self._mode_toggled, key, value)
            box.append(button)
            self.mode_buttons[value] = button
        row.add_suffix(box)
        return row

    def _mode_toggled(self, button, key, value):
        if button.get_active():
            hints = {v: hint for v, _label, hint in options.TYPING_MODES}
            self.rows[key].set_subtitle(_(hints[value]))
            self._changed(key, value)

    def choices(self, key):
        """(value, label) pairs; a value saved by hand that is not in the list stays offered."""
        if key in options.CHOICES:
            # Each language keeps its own name, as in fcitx5.
            translate = (lambda label: label) if key == "UiLanguage" else _
            pairs = [(value, translate(label)) for value, label in options.CHOICES[key]]
        else:
            pairs = [(value, value) for value in self.names[key]]
        current = self.store.values.get(key, "")
        if key == "ModeOrder" and "Vim" not in current.split(","):
            # The menu puts Vim mode last when the order leaves it out, so such an order is its preset.
            pairs = [
                (current if value == current + ",Vim" else value, label) for value, label in pairs
            ]
        if current not in (value for value, _label in pairs):
            shown = (
                keys.label(current)
                if key in ("ToggleKey", "ModeMenuKey", "CycleModeKey")
                else current
            )
            pairs.append((current, shown))
        if key == "ToggleKey":
            pairs.append((OTHER_KEYS, _("Other keys…")))
        return pairs

    def _choice(self, key, row):
        index = row.get_selected()
        pairs = self.pairs.get(key, [])
        return pairs[index][0] if index < len(pairs) else None

    def _changed(self, key, value):
        if key == "InputMethod":
            self._show_example()
        if self.loading or value is None:
            return
        if value == OTHER_KEYS:
            self.capture = KeyCapture(self.taken_keys(), self._save_toggle_key, self.refresh)
            self.capture.present(self.rows[key].get_root())
            return
        self.on_change(key, value)

    def _shortcut_changed(self, key, row):
        value = row.get_text()
        problem = options.shortcut_problem(key, value, self.store.values)
        if problem and value:
            row.add_css_class("error")
        else:
            row.remove_css_class("error")
        row.set_tooltip_text(_(problem) if problem and value else None)
        if not self.loading and not problem:
            # The engine matches the key as typed without Shift.
            self.on_change(key, value.lower())

    def _save_toggle_key(self, value):
        self.on_change("ToggleKey", value)
        self.refresh()

    def _show_example(self):
        row = self.rows.get("InputMethod")
        if row is not None:
            row.set_subtitle(options.TYPING_EXAMPLES.get(self._choice("InputMethod", row), ""))

    def refresh(self):
        self.loading = True
        try:
            for key, row in self.rows.items():
                value = self.store.values.get(key, "")
                if isinstance(row, Adw.SwitchRow):
                    row.set_active(value == "True")
                    continue
                if isinstance(row, Adw.EntryRow):
                    row.set_text(value)
                    continue
                if key == "DirectTyping":
                    # Anything but False types with Sen, as the engine reads it.
                    self.mode_buttons["False" if value == "False" else "True"].set_active(True)
                    continue
                pairs = self.pairs[key] = self.choices(key)
                row.set_model(Gtk.StringList.new([label for _value, label in pairs]))
                row.set_selected([v for v, _label in pairs].index(value))
            self._show_example()
        finally:
            self.loading = False


class KeyCapture:
    """Asks for a toggle key: the keys held together when the last of them comes up."""

    def __init__(self, taken, on_save, on_cancel):
        self.taken = taken
        self.on_save = on_save
        self.on_cancel = on_cancel
        # By key code, since a key can come up under another name (Alt as Meta, 1 as ! with Shift).
        self.held = set()
        self.pressed = {}
        self.value = None
        self.parent = None
        self.watch = None
        self.navigating = False
        self.dialog = Adw.AlertDialog(
            heading=_("Press the new keys"), body=_("Hold the keys together, then let go.")
        )
        self.shown = Gtk.Label()
        self.shown.add_css_class("title-2")
        self.note = Gtk.Label(wrap=True, justify=Gtk.Justification.CENTER)
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        box.append(self.shown)
        box.append(self.note)
        self.dialog.set_extra_child(box)
        self.dialog.add_response("cancel", _("Cancel"))
        self.dialog.add_response("save", _("Save"))
        self.dialog.set_response_appearance("save", Adw.ResponseAppearance.SUGGESTED)
        self.dialog.set_response_enabled("save", False)
        self.dialog.set_close_response("cancel")
        self.dialog.connect("response", lambda _d, response: self._answer(response))
        # Captures keys before the buttons do; Tab, Enter and Escape alone still reach them.
        keyboard = Gtk.EventControllerKey(propagation_phase=Gtk.PropagationPhase.CAPTURE)
        keyboard.connect(
            "key-pressed", lambda _c, k, code, _s: self.press(Gdk.keyval_name(k), code)
        )
        keyboard.connect("key-released", lambda _c, _k, code, _s: self.release(code))
        self.dialog.add_controller(keyboard)

    def present(self, parent):
        self.parent = parent
        # A release that went to another window, after Alt+Tab, would keep a key held forever.
        self.watch = parent.connect("notify::is-active", lambda *_: self.forget())
        self.dialog.present(parent)

    def forget(self):
        self.held.clear()
        self.pressed.clear()
        self.navigating = False

    def press(self, name, code):
        if name is None:
            return True
        if name in NAVIGATION and all(self.pressed[c].startswith("Shift") for c in self.held):
            # These keys move between the buttons; the round they were in records nothing.
            self.navigating = bool(self.held)
            return False
        if not self.held:
            self.pressed.clear()
        self.held.add(code)
        self.pressed.setdefault(code, name)
        return True

    def release(self, code):
        if code not in self.held:
            return
        self.held.discard(code)
        if self.held:
            return
        if self.navigating:
            self.navigating = False
            return
        names = list(self.pressed.values())
        chord = keys.from_pressed(names)
        error, warning = keys.problem(chord, self.taken)
        if not error and not _modifiers_first(names):
            error = keys.MODIFIERS_FIRST
        self.value = None if error else keys.value(chord)
        self.shown.set_label(keys.label(keys.value(chord)) if chord else "")
        # gettext answers the empty string with the catalog header.
        self.note.set_label(_(error or warning) if error or warning else "")
        if error:
            self.note.add_css_class("error")
        else:
            self.note.remove_css_class("error")
        self.dialog.set_response_enabled("save", self.value is not None)

    def _answer(self, response):
        if self.watch is not None:
            self.parent.disconnect(self.watch)
            self.watch = None
        if response == "save" and self.value is not None:
            self.on_save(self.value)
        else:
            self.on_cancel()


def _modifiers_first(names):
    """The engine matches a key with the modifiers held at its press, and Shift changes its name."""
    others = [i for i, n in enumerate(names) if n not in keys.MODIFIER_KEYS]
    return not others or all(i < others[0] for i, n in enumerate(names) if n in keys.MODIFIER_KEYS)


def preferences_group(title=None, description=None):
    group = Adw.PreferencesGroup()
    if title:
        group.set_title(_(title))
    if description:
        group.set_description(description)
    return group


def typing_page(rows, tip=None, on_tip=None):
    """tip, if given, shows above the settings; on_tip() runs when it is clicked."""
    page = Adw.PreferencesPage(title=_("Typing"), icon_name="input-keyboard-symbolic")
    if tip is not None:
        row = Adw.ActionRow(
            title=_("Tip: {}").format(_(tip["text"])), use_markup=False, activatable=True
        )
        row.add_suffix(Gtk.Image(icon_name="go-next-symbolic"))
        if on_tip is not None:
            row.connect("activated", lambda _r: on_tip())
        page.tip_row = row
        group = preferences_group()
        group.searchable = False  # A tip is not a setting.
        group.add(row)
        page.add(group)
    for title, names in options.TYPING_GROUPS:
        group = preferences_group(title)
        for key in names:
            group.add(rows.build(key))
        if title is None:
            try_it = Adw.EntryRow(title=_("Try it"))
            try_it.add_css_class("typed")
            group.add(try_it)
        page.add(group)
    return page


class MacroPage:
    """The macro switches and the list, which saves on every add, edit and removal."""

    def __init__(self, store, rows, on_list_change):
        self.store = store
        self.on_list_change = on_list_change
        self.page = Adw.PreferencesPage(title=_("Macro"), icon_name="document-edit-symbolic")
        group = preferences_group()
        for key in options.MACRO_SWITCHES + ["TimeFormat", "DateFormat"]:
            group.add(rows.build(key))
        self.page.add(group)
        self.page.add(self._editor())
        self.page.add(self._list())

    def _editor(self):
        group = preferences_group()
        self.key_row = Adw.EntryRow(title=_("Abbreviation (e.g. kg)"))
        self.value_row = Adw.EntryRow(title=_("Full text (e.g. khô gà)"))
        self.value_row.add_css_class("typed")
        self.add_button = Gtk.Button(label=_("Add"), valign=Gtk.Align.CENTER)
        self.add_button.add_css_class("suggested-action")
        self.add_button.connect("clicked", lambda _b: self._add())
        self.value_row.add_suffix(self.add_button)
        for row in (self.key_row, self.value_row):
            row.connect("changed", lambda _r: self._check())
            row.connect("entry-activated", lambda _r: self._add())
            group.add(row)
        self._check()
        return group

    def _list(self):
        group = preferences_group()
        self.search = Gtk.SearchEntry(placeholder_text=_("Search macros..."))
        self.search.connect("search-changed", lambda _s: self.list.invalidate_filter())
        group.set_header_suffix(self.search)
        # The user's own entries are not settings to find.
        self.list = Gtk.ListBox(selection_mode=Gtk.SelectionMode.NONE)
        self.list.searchable = False
        self.list.add_css_class("boxed-list")
        self.list.set_placeholder(
            Gtk.Label(label=_("No macros yet."), margin_top=12, margin_bottom=12)
        )
        self.list.set_filter_func(self._matches)
        group.add(self.list)
        return group

    def _check(self):
        key = self.key_row.get_text()
        bad = bool(key) and not options.valid_macro_key(key)
        if bad:
            self.key_row.add_css_class("error")
        else:
            self.key_row.remove_css_class("error")
        self.key_row.set_tooltip_text(
            _("Macro keys cannot contain spaces or special characters.") if bad else None
        )
        exists = any(k == key for k, _v in self.store.macros)
        self.add_button.set_label(_("Update") if exists else _("Add"))
        self.add_button.set_sensitive(bool(key) and not bad and bool(self.value_row.get_text()))

    def _add(self):
        if not self.add_button.get_sensitive():
            return
        key, value = self.key_row.get_text(), self.value_row.get_text()
        macros = list(self.store.macros)
        index = next((i for i, (k, _v) in enumerate(macros) if k == key), None)
        if index is None:
            macros.append((key, value))
        else:
            macros[index] = (key, value)
        if self.on_list_change(macros, key):
            self.key_row.set_text("")
            self.value_row.set_text("")
            self.key_row.grab_focus()

    def _remove(self, key):
        self.on_list_change([(k, v) for k, v in self.store.macros if k != key], key)

    def _edit(self, key, value):
        self.key_row.set_text(key)
        self.value_row.set_text(value)
        self.value_row.grab_focus()

    def _matches(self, row):
        needle = self.search.get_text().casefold()
        return not needle or needle in row.macro[0].casefold() or needle in row.macro[1].casefold()

    def refresh(self):
        self.list.remove_all()
        for key, value in self.store.macros:
            self.list.append(self._macro_row(key, value))
        self._check()

    def _macro_row(self, key, value):
        row = Adw.ActionRow(use_markup=False, activatable=True)
        row.set_title(key)
        row.set_subtitle(value)
        row.macro = (key, value)
        row.connect("activated", lambda _r: self._edit(key, value))
        remove = Gtk.Button(
            icon_name="user-trash-symbolic", valign=Gtk.Align.CENTER, tooltip_text=_("Remove")
        )
        remove.add_css_class("flat")
        remove.connect("clicked", lambda _b: self._remove(key))
        row.add_suffix(remove)
        return row


class DictionaryPage:
    """The custom dictionary switch and its word list, saved on every add and removal."""

    # Thousands of rows make GTK slow, so the list shows search results only.
    SHOWN = 100

    def __init__(self, store, rows, on_list_change):
        self.store = store
        self.on_list_change = on_list_change
        self.page = Adw.PreferencesPage(
            title=_("Dictionary"), icon_name="accessories-dictionary-symbolic"
        )
        group = preferences_group()
        self.switch = rows.build("EnableDictionary")
        group.add(self.switch)
        self.page.add(group)
        self.editor = preferences_group()
        self.word_row = Adw.EntryRow(title=_("Word (e.g. ngoãy)"))
        self.word_row.add_css_class("typed")
        self.add_button = Gtk.Button(label=_("Add"), valign=Gtk.Align.CENTER)
        self.add_button.add_css_class("suggested-action")
        self.add_button.connect("clicked", lambda _b: self._add())
        self.word_row.add_suffix(self.add_button)
        self.word_row.connect("changed", lambda _r: self._check())
        self.word_row.connect("entry-activated", lambda _r: self._add())
        self.editor.add(self.word_row)
        self.page.add(self.editor)
        self.page.add(self._list())
        self.switch.connect("notify::active", lambda _r, _p: self._enable())

    def _list(self):
        self.list_group = preferences_group()
        self.search = Gtk.SearchEntry(placeholder_text=_("Search words..."))
        self.search.connect("search-changed", lambda _s: self._show())
        self.list_group.set_header_suffix(self.search)
        self.list = Gtk.ListBox(selection_mode=Gtk.SelectionMode.NONE)
        self.list.searchable = False
        self.list.add_css_class("boxed-list")
        self.list_group.add(self.list)
        return self.list_group

    def _check(self):
        word = self.word_row.get_text().strip()
        bad = any(c.isspace() for c in word)
        if bad:
            self.word_row.add_css_class("error")
        else:
            self.word_row.remove_css_class("error")
        self.word_row.set_tooltip_text(_("One word, without spaces.") if bad else None)
        self.add_button.set_sensitive(bool(word) and not bad and word not in self.store.words)

    def _add(self):
        if not self.add_button.get_sensitive():
            return
        word = self.word_row.get_text().strip()
        if self.on_list_change(self.store.words + [word]):
            self.word_row.set_text("")
            self.search.set_text(word)
            self._show()  # The search entry reports the change only after a delay.

    def _remove(self, word):
        self.on_list_change([w for w in self.store.words if w != word])

    def _enable(self):
        on = self.switch.get_active()
        self.editor.set_sensitive(on)
        self.list_group.set_sensitive(on)

    def _show(self):
        self.list.remove_all()
        needle = self.search.get_text().strip().casefold()
        if not needle:
            count = len(self.store.words)
            text = _("{} words. Search to see them.").format(count) if count else _("No words yet.")
            self.list_group.set_description(text)
            return
        found = [w for w in self.store.words if needle in w.casefold()]
        for word in found[: self.SHOWN]:
            self.list.append(self._word_row(word))
        if not found:
            text = _("No words found.")
        elif len(found) > self.SHOWN:
            text = _("Showing {} of {} words found.").format(self.SHOWN, len(found))
        else:
            text = None
        self.list_group.set_description(text)

    def refresh(self):
        self._show()
        self._check()
        self._enable()

    def _word_row(self, word):
        row = Adw.ActionRow(use_markup=False, title=word)
        remove = Gtk.Button(
            icon_name="user-trash-symbolic", valign=Gtk.Align.CENTER, tooltip_text=_("Remove")
        )
        remove.add_css_class("flat")
        remove.connect("clicked", lambda _b: self._remove(word))
        row.add_suffix(remove)
        return row


class KeymapPage:
    """The custom keymap switch, its presets and the key list, saved on every change."""

    def __init__(self, store, rows, on_list_change):
        self.store = store
        self.on_list_change = on_list_change
        self.page = Adw.PreferencesPage(
            title=_("Keymap"), icon_name="preferences-desktop-keyboard-symbolic"
        )
        group = preferences_group()
        group.add(rows.build("EnableCustomKeymap"))
        self.preset = Adw.ComboRow(title=_("Original Input Method:").rstrip(":"), use_markup=False)
        self.preset.set_model(Gtk.StringList.new([_(name) for name, _keys in keymap.PRESETS]))
        apply = Gtk.Button(label=_("Apply Preset"), valign=Gtk.Align.CENTER)
        apply.connect("clicked", lambda _b: self.apply_preset())
        self.preset.add_suffix(apply)
        group.add(self.preset)
        self.page.add(group)
        self.page.add(self._editor())
        self.page.add(self._list())

    def _editor(self):
        self.editor = preferences_group()
        self.key_row = Adw.EntryRow(title=_("Key (Example: s)"))
        self.key_row.add_css_class("typed")
        self.add_button = Gtk.Button(label=_("Add"), valign=Gtk.Align.CENTER)
        self.add_button.add_css_class("suggested-action")
        self.add_button.connect("clicked", lambda _b: self._add())
        self.key_row.add_suffix(self.add_button)
        self.key_row.connect("changed", lambda _r: self._check())
        self.key_row.connect("entry-activated", lambda _r: self._add())
        self.editor.add(self.key_row)
        self.action = Adw.ComboRow(title=_("Action"), use_markup=False)
        self.action.set_model(Gtk.StringList.new([_(label) for _code, label in keymap.ACTIONS]))
        self.action.connect("notify::selected", lambda _r, _p: self._check())
        self.editor.add(self.action)
        return self.editor

    def _list(self):
        group = preferences_group()
        self.search = Gtk.SearchEntry(placeholder_text=_("Search keys..."))
        self.search.connect("search-changed", lambda _s: self.list.invalidate_filter())
        group.set_header_suffix(self.search)
        self.list = Gtk.ListBox(selection_mode=Gtk.SelectionMode.NONE)
        self.list.searchable = False
        self.list.add_css_class("boxed-list")
        self.list.set_placeholder(
            Gtk.Label(label=_("No keys yet."), margin_top=12, margin_bottom=12)
        )
        self.list.set_filter_func(self._matches)
        group.add(self.list)
        return group

    def _check(self):
        key = self.key_row.get_text()
        bad = bool(key) and not keymap.valid_key(key)
        if bad:
            self.key_row.add_css_class("error")
        else:
            self.key_row.remove_css_class("error")
        self.key_row.set_tooltip_text(_("A key is one character, not a space.") if bad else None)
        action = next((v for k, v in self.store.keymap if k == key), None) if key else None
        # Adding a key that is already listed replaces its action instead of adding it twice.
        self.editor.set_description(
            _("{key} already does “{action}”; Update replaces it.").format(
                key=GLib.markup_escape_text(key),
                action=GLib.markup_escape_text(_(keymap.action_label(action))),
            )
            if action is not None
            else None
        )
        self.add_button.set_label(_("Update") if action is not None else _("Add"))
        chosen = self.action.get_selected() < len(keymap.ACTION_CODES)
        self.add_button.set_sensitive(bool(key) and not bad and chosen)

    def _add(self):
        selected = self.action.get_selected()
        if not self.add_button.get_sensitive() or selected >= len(keymap.ACTION_CODES):
            return
        key = self.key_row.get_text()
        action = keymap.ACTION_CODES[selected]
        entries = list(self.store.keymap)
        index = next((i for i, (k, _v) in enumerate(entries) if k == key), None)
        if index is None:
            entries.append((key, action))
        else:
            entries[index] = (key, action)
        if self.on_list_change(entries):
            self.key_row.set_text("")
            self.key_row.grab_focus()

    def _remove(self, key):
        self.on_list_change([(k, v) for k, v in self.store.keymap if k != key])

    def _edit(self, key, action):
        self.key_row.set_text(key)
        self._drop_unknown_action()
        if action in keymap.ACTION_CODES:
            self.action.set_selected(keymap.ACTION_CODES.index(action))
        else:
            # ComboRow cannot show no choice; an action this window does not know is shown as
            # is and must be replaced by a listed one before Update, so it is not lost unseen.
            self.action.get_model().append(action)
            self.action.set_selected(len(keymap.ACTION_CODES))
        self.action.grab_focus()

    def _drop_unknown_action(self):
        actions = self.action.get_model()
        if actions.get_n_items() > len(keymap.ACTION_CODES):
            if self.action.get_selected() >= len(keymap.ACTION_CODES):
                self.action.set_selected(0)
            actions.remove(len(keymap.ACTION_CODES))

    def apply_preset(self):
        # The toast offers to undo, so replacing the list needs no confirmation.
        self.on_list_change(list(keymap.PRESETS[self.preset.get_selected()][1]))

    def _matches(self, row):
        needle = self.search.get_text().casefold()
        key = row.entry[0]
        return not needle or needle in key.casefold() or needle in row.get_subtitle().casefold()

    def refresh(self):
        self.list.remove_all()
        for key, action in self.store.keymap:
            self.list.append(self._key_row(key, action))
        self._drop_unknown_action()
        self._check()

    def _key_row(self, key, action):
        row = Adw.ActionRow(use_markup=False, activatable=True)
        row.set_title(key)
        row.set_subtitle(_(keymap.action_label(action)))
        row.entry = (key, action)
        row.connect("activated", lambda _r: self._edit(key, action))
        remove = Gtk.Button(
            icon_name="user-trash-symbolic", valign=Gtk.Align.CENTER, tooltip_text=_("Remove")
        )
        remove.add_css_class("flat")
        remove.connect("clicked", lambda _b: self._remove(key))
        row.add_suffix(remove)
        return row


FCITX5_ONLY = [
    N_("Sen in every app, with its fixes for each app"),
    N_("Cứu dấu in every app"),
    N_("Vim mode for each app"),
    N_("The emoji picker, and turning Vietnamese off for one app"),
]
FULL_EDITION_URL = "https://github.com/ngosen/ngosen"
# The window only hands the command over: the installer needs a terminal for sudo.
FULL_EDITION_COMMAND = (
    "curl -fsSL https://raw.githubusercontent.com/ngosen/ngosen/main/install.sh | bash"
)
PROJECT_URL = "https://github.com/ngosen/ibus-ngosen"
# The name and its link stay on one line: it is the only place this project may name it.
FORK = ("fcitx5-lotus", "https://github.com/LotusInputMethod/fcitx5-lotus")
VMK = ("VMK", "https://github.com/thanhpy2009/VMK")


def about_dialog(version):
    dialog = Adw.AboutDialog(
        application_name="Ngó Sen",
        application_icon="io.github.ngosen.IBusNgoSen",
        version=version,
        comments=_("Vietnamese input method for GNOME")
        + "\n\n"
        + _("Based on the code of {fork} and {vmk}.").format(fork=FORK[0], vmk=VMK[0]),
        website=PROJECT_URL,
        issue_url=PROJECT_URL + "/issues/new/choose",
        license_type=Gtk.License.GPL_3_0,
    )
    for name, url in (FORK, VMK):
        dialog.add_link(name, url)
    return dialog


def fcitx5_group(show_toast, back_up):
    group = preferences_group(
        N_("In the fcitx5 edition"),
        _("Ngó Sen for fcitx5 also has these. It runs on fcitx5 instead of IBus."),
    )
    for text in FCITX5_ONLY:
        group.add(Adw.ActionRow(title=_(text), use_markup=False))
    switch = Adw.ActionRow(
        title=_("Switch to the fcitx5 edition"),
        subtitle=_("Shows the install command to copy."),
        activatable=True,
    )
    switch.add_suffix(Gtk.Image(icon_name="go-next-symbolic"))
    switch.connect("activated", lambda r: show_switch_dialog(r.get_root(), show_toast, back_up))
    group.add(switch)
    group.add(
        _link_row(
            _("Ngó Sen full edition"), _("Runs on fcitx5 and has all of these."), FULL_EDITION_URL
        )
    )
    return group


def show_switch_dialog(parent, show_toast, back_up=None):
    dialog = Adw.AlertDialog(
        heading=_("Switch to the fcitx5 edition"),
        body=_(
            "Run this command in a terminal, then log out and back in. On Debian and Ubuntu it "
            "replaces this edition. On Fedora, openSUSE and Arch, copy the command, remove "
            "ibus-ngosen, then run it; this window goes with the package."
        )
        + "\n\n"
        + _(
            "Settings made here are not copied over. Back them up, then restore the file in the "
            "fcitx5 edition."
        ),
    )
    command = Gtk.Label(label=FULL_EDITION_COMMAND, selectable=True, wrap=True, xalign=0)
    command.set_wrap_mode(Pango.WrapMode.CHAR)
    # A hyphen at the break would read as part of the command.
    attributes = Pango.AttrList()
    attributes.insert(Pango.attr_insert_hyphens_new(False))
    command.set_attributes(attributes)
    command.add_css_class("monospace")
    dialog.set_extra_child(command)
    dialog.add_response("close", _("Close"))
    if back_up is not None:
        dialog.add_response("backup", _("Back Up First…"))
    dialog.add_response("copy", _("Copy command"))
    dialog.set_response_appearance("copy", Adw.ResponseAppearance.SUGGESTED)
    dialog.set_default_response("copy")
    dialog.set_close_response("close")
    dialog.connect(
        "response", lambda d, response: _switch_response(d, response, show_toast, back_up)
    )
    dialog.present(parent)
    return dialog


def _switch_response(dialog, response, show_toast, back_up):
    if response == "backup":
        back_up()
        return
    if response != "copy":
        return
    dialog.get_clipboard().set(FULL_EDITION_COMMAND)
    show_toast(Adw.Toast(title=_("Command copied.")))


def _link_row(title, subtitle, url):
    row = Adw.ActionRow(title=title, activatable=True)
    if subtitle:
        row.set_subtitle(subtitle)
    row.add_suffix(Gtk.Image(icon_name="adw-external-link-symbolic"))
    row.connect(
        "activated", lambda r: Gtk.UriLauncher(uri=url).launch(r.get_root(), None, None, None)
    )
    return row
