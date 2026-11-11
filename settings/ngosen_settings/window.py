# SPDX-FileCopyrightText: 2026 Ngó Sen contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
import os
import subprocess
import sys
from datetime import datetime

import gi

gi.require_version("Gtk", "4.0")
gi.require_version("Adw", "1")
gi.require_version("PangoCairo", "1.0")

from gi.repository import Adw, Gio, GLib, Gtk  # noqa: E402,F401

from . import (  # noqa: E402
    backup,
    brand,
    i18n,
    options,
    pages,
    search,
    whats_new,
)
from .i18n import _  # noqa: E402
from .store import Engine, Store  # noqa: E402

APP_ID = "io.github.ngosen.WlNgoSen"
# A file dialog the user closed; older GTK has no CANCELLED.
CLOSED_DIALOG = [
    getattr(Gtk.DialogError, name)
    for name in ("DISMISSED", "CANCELLED")
    if hasattr(Gtk.DialogError, name)
]


class Window(Adw.ApplicationWindow):
    def __init__(self, app, store, names, version, engine=None):
        super().__init__(application=app, title="Ngó Sen", default_width=800, default_height=720)
        self.store = store
        self.engine = engine
        self.toast = None
        self.rows = pages.SettingRows(store, names, self.save)
        self.macros = pages.MacroPage(store, self.rows, self.save_macros)
        self.dictionary = pages.DictionaryPage(store, self.rows, self.save_words)
        self.keymap = pages.KeymapPage(store, self.rows, self.save_keymap)

        self.stack = stack = Adw.ViewStack()
        self.typing = typing = pages.typing_page(self.rows, whats_new.random_tip(), self.show_tips)
        self.whats_new = whats_new.WhatsNewPage(
            self.open_setting, self.show_page, {"macros": self.macros.page.get_title()}
        )
        self.named_pages = {
            "typing": typing,
            "macros": self.macros.page,
            "dictionary": self.dictionary.page,
            "keymap": self.keymap.page,
            "whats-new": self.whats_new.page,
        }
        for page in self.named_pages.values():
            stack.add_titled_with_icon(page, None, page.get_title(), page.get_icon_name())
        self.search_pages = [typing, self.macros.page, self.dictionary.page, self.keymap.page]
        # Header tabs cut the longer Vietnamese page names; a sidebar has room for them.
        self.split = Adw.NavigationSplitView(
            sidebar=self._sidebar_page(version), content=self._content_page()
        )
        stack.connect("notify::visible-child", self._page_shown)
        self._page_shown(stack, None)
        self.toasts = Adw.ToastOverlay(child=self.split)
        self.set_content(self.toasts)

        narrow = Adw.Breakpoint.new(Adw.BreakpointCondition.parse("max-width: 700sp"))
        narrow.add_setter(self.split, "collapsed", True)
        self.add_breakpoint(narrow)
        self.refresh()

    def _sidebar_page(self, version):
        self.sidebar = Gtk.ListBox(css_classes=["navigation-sidebar"])
        tabs = self.stack.get_pages()
        for i in range(tabs.get_n_items()):
            self.sidebar.append(self._sidebar_row(tabs.get_item(i)))
        self.sidebar.connect("row-selected", self._page_selected)
        # Clicking the row already shown still opens it when the split view is collapsed.
        self.sidebar.connect("row-activated", lambda _l, _r: self.split.set_show_content(True))
        self.results = Gtk.ListBox(css_classes=["navigation-sidebar"])
        self.results.connect("row-activated", lambda _l, row: self.open_result(row))
        self.side_stack = Gtk.Stack()
        self.side_stack.add_named(Gtk.ScrolledWindow(child=self.sidebar), "pages")
        self.side_stack.add_named(Gtk.ScrolledWindow(child=self.results), "results")
        self.search_entry = Gtk.SearchEntry(
            placeholder_text=_("Search settings"), margin_start=8, margin_end=8, margin_bottom=6
        )
        self.search_entry.connect("search-changed", self._on_search)
        self.search_entry.connect("activate", self._open_first_result)
        find = Gtk.ShortcutController(scope=Gtk.ShortcutScope.MANAGED)
        find.add_shortcut(
            Gtk.Shortcut(
                trigger=Gtk.ShortcutTrigger.parse_string("<Control>f"),
                action=Gtk.CallbackAction.new(lambda *_a: self.focus_search()),
            )
        )
        self.add_controller(find)
        header = Adw.HeaderBar()
        header.pack_end(self._menu(version))
        view = Adw.ToolbarView(content=self.side_stack)
        view.add_top_bar(header)
        view.add_top_bar(self.search_entry)
        return Adw.NavigationPage(title="Ngó Sen", child=view)

    def _on_search(self, entry):
        self.results.remove_all()
        text = entry.get_text()
        self.side_stack.set_visible_child_name("results" if text.strip() else "pages")
        if not text.strip():
            return
        for title, row, page in search.find(self.search_pages, text):
            result = Gtk.ListBoxRow()
            box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, margin_top=4, margin_bottom=4)
            box.append(Gtk.Label(label=title, xalign=0, wrap=True))
            where = self.stack.get_page(page).get_title()
            box.append(Gtk.Label(label=where, xalign=0, css_classes=["dim-label"]))
            result.set_child(box)
            result.found = (row, page)
            self.results.append(result)
        if self.results.get_row_at_index(0) is None:
            empty = Gtk.ListBoxRow(activatable=False, selectable=False)
            empty.set_child(Gtk.Label(label=_("No settings found"), css_classes=["dim-label"]))
            self.results.append(empty)

    def focus_search(self):
        # A narrow window shows the page alone, so the search field has to be brought back.
        self.split.set_show_content(False)
        self.search_entry.grab_focus()
        return True

    def _open_first_result(self, entry):
        # Results follow typing after a short delay; Enter must not open the previous query's.
        self._on_search(entry)
        first = self.results.get_row_at_index(0)
        if first is not None and hasattr(first, "found"):
            self.open_result(first)

    def open_result(self, result):
        """Shows the page holding a found row and puts the focus on it."""
        if not hasattr(result, "found"):
            return
        row, page = result.found
        self.search_entry.set_text("")
        self.side_stack.set_visible_child_name("pages")
        self._show_row(page, row)

    def _show_row(self, page, row):
        self.stack.set_visible_child(page)
        self.split.set_show_content(True)
        search.focus_when_laid_out(row)

    def open_setting(self, key):
        row = self.rows.rows.get(key)
        if row is None:
            return False
        self._show_row(row.get_ancestor(Adw.PreferencesPage), row)
        return True

    def show_page(self, name):
        """Shows the named page; a name this window does not have is ignored."""
        if name in self.named_pages:
            self.stack.set_visible_child(self.named_pages[name])
            self.split.set_show_content(True)

    def show_tips(self):
        # Scrolling follows the focus, so focusing the first tip brings the list into view.
        self._show_row(self.whats_new.page, self.whats_new.tip_rows[0])

    def _sidebar_row(self, tab):
        box = Gtk.Box(spacing=12, margin_top=6, margin_bottom=6, margin_start=6, margin_end=6)
        box.append(Gtk.Image(icon_name=tab.get_icon_name()))
        box.append(Gtk.Label(label=tab.get_title(), xalign=0, hexpand=True))
        return Gtk.ListBoxRow(child=box)

    def _content_page(self):
        view = Adw.ToolbarView(content=self.stack)
        view.add_top_bar(Adw.HeaderBar())
        self.content = Adw.NavigationPage(title="", child=view)
        return self.content

    def _page_selected(self, _sidebar, row):
        if row is None:
            return
        self.stack.set_visible_child(self.stack.get_pages().get_item(row.get_index()).get_child())
        self.split.set_show_content(True)

    def _page_shown(self, stack, _param):
        tab = stack.get_page(stack.get_visible_child())
        self.content.set_title(tab.get_title())
        tabs = stack.get_pages()
        index = next(i for i in range(tabs.get_n_items()) if tabs.get_item(i) is tab)
        self.sidebar.select_row(self.sidebar.get_row_at_index(index))

    def refresh(self):
        self.rows.refresh()
        self.macros.refresh()
        self.dictionary.refresh()
        self.keymap.refresh()

    def save(self, key, value):
        self._write(lambda: self.store.set(key, value), _(options.LABELS[key]))

    def save_macros(self, macros, _key):
        saved = self._write(lambda: self.store.set_macros(macros) or True, _("Macro"))
        self.macros.refresh()
        return saved

    def save_words(self, words):
        saved = self._write(lambda: self.store.set_words(words) or True, _("Dictionary"))
        self.dictionary.refresh()
        return saved

    def save_keymap(self, entries):
        saved = self._write(lambda: self.store.set_keymap(entries), _("Keymap"))
        self.keymap.refresh()
        return saved

    def _write(self, write, label):
        try:
            if not write():
                return True
        except OSError as error:
            self._show(Adw.Toast(title=_("Cannot save: {}").format(error.strerror), timeout=0))
            self.refresh()
            return False
        toast = Adw.Toast(title=_("Saved “{}”.").format(label), button_label=_("_Undo"))
        toast.connect("button-clicked", lambda _t: self.undo())
        self._show(toast)
        return True

    def _show(self, toast):
        if self.toast is not None:
            self.toast.dismiss()
        self.toast = toast
        toast.set_use_markup(False)
        self.toasts.add_toast(toast)

    def undo(self):
        if not self.store.can_undo():
            return
        try:
            self.store.undo()
        except OSError as error:
            self._show(Adw.Toast(title=_("Cannot save: {}").format(error.strerror), timeout=0))
        self.refresh()

    def _menu(self, version):
        backups, about = Gio.Menu(), Gio.Menu()
        for section, label, name, run in (
            (backups, _("Back _Up…"), "backup", self.choose_backup_file),
            (backups, _("_Restore…"), "restore-backup", self.choose_restore_file),
            (about, _("_About Ngó Sen"), "about", lambda: self.show_about(version)),
        ):
            section.append(label, f"win.{name}")
            action = Gio.SimpleAction.new(name, None)
            action.connect("activate", lambda *_args, run=run: run())
            self.add_action(action)
        menu = Gio.Menu()
        menu.append_section(None, backups)
        menu.append_section(None, about)
        return Gtk.MenuButton(
            icon_name="open-menu-symbolic", menu_model=menu, tooltip_text=_("Menu")
        )

    def show_about(self, version):
        dialog = pages.about_dialog(version)
        dialog.present(self)
        return dialog

    def choose_backup_file(self):
        dialog = self._file_dialog(_("Back Up Settings"))
        dialog.set_initial_name(f"ngosen-backup-{datetime.now():%Y%m%d-%H%M%S}.json")
        dialog.save(self, None, self._on_file, dialog.save_finish, self.export_backup)

    def choose_restore_file(self):
        dialog = self._file_dialog(_("Restore Settings"))
        dialog.open(self, None, self._on_file, dialog.open_finish, self.restore_backup)

    def _file_dialog(self, title):
        json_files = Gtk.FileFilter(name=_("Ngó Sen backup"))
        json_files.add_suffix("json")
        filters = Gio.ListStore.new(Gtk.FileFilter)
        filters.append(json_files)
        return Gtk.FileDialog(title=title, modal=True, filters=filters, default_filter=json_files)

    def _on_file(self, _dialog, result, finish, use):
        try:
            file = finish(result)
        except GLib.Error as error:
            if not any(error.matches(Gtk.dialog_error_quark(), code) for code in CLOSED_DIALOG):
                text = _("Cannot open the file dialog: {}").format(error.message)
                self._show(Adw.Toast(title=text, timeout=0))
            return
        if file is not None and file.get_path():
            use(file.get_path())

    def export_backup(self, path):
        try:
            backup.export(self.engine, backup.Paths(self.store), path)
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            self._show(Adw.Toast(title=_("Cannot back up: {}").format(reason(error)), timeout=0))
            return
        self._show(Adw.Toast(title=_("Backed up to {}.").format(path)))

    def restore_backup(self, path):
        """Checks the whole file before writing; what the user would lose is asked about first."""
        try:
            data = backup.load(path)
            _changes, notes = backup.plan(
                data, backup.Paths(self.store), self.engine, self.rows.taken_keys()
            )
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            self._cannot_restore(error)
            return None
        name = os.path.basename(path)
        if not notes:
            self.apply_backup(data, name)
            return None
        dialog = Adw.AlertDialog(heading=_("Restore {}?").format(name), body="\n\n".join(notes))
        dialog.add_response("cancel", _("Cancel"))
        dialog.add_response("restore", _("Restore"))
        dialog.set_default_response("cancel")
        dialog.set_close_response("cancel")
        dialog.connect(
            "response", lambda _d, answer: answer == "restore" and self.apply_backup(data, name)
        )
        dialog.present(self)
        return dialog

    def apply_backup(self, data, name):
        try:
            # Planned again, so a change made while the dialog was open is not written over.
            changes, _notes = backup.plan(
                data, backup.Paths(self.store), self.engine, self.rows.taken_keys()
            )
            before = backup.replace(changes)
        except backup.RestoreError as error:
            self._show(Adw.Toast(title=str(error), timeout=0))
            self._reload()
            return
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            self._cannot_restore(error)
            return
        self._reload()
        toast = Adw.Toast(title=_("Restored from {}.").format(name), button_label=_("_Undo"))
        toast.connect("button-clicked", lambda _t: self.undo_restore(before, changes))
        self._show(toast)

    def undo_restore(self, before, restored):
        try:
            if not backup.undo(before, restored):
                text = _("The settings changed after the restore, so nothing was undone.")
                self._show(Adw.Toast(title=text, timeout=0))
        except OSError as error:
            self._show(Adw.Toast(title=_("Cannot save: {}").format(reason(error)), timeout=0))
        self._reload()

    def _cannot_restore(self, error):
        text = _("Cannot restore: {}. Nothing was changed.").format(reason(error).rstrip("."))
        self._show(Adw.Toast(title=text, timeout=0))

    def _reload(self):
        try:
            self.store.reload(self.engine)
        except (OSError, subprocess.SubprocessError) as error:
            self._show(Adw.Toast(title=_("Cannot read the settings: {}").format(reason(error))))
        self.refresh()


def reason(error):
    return getattr(error, "strerror", None) or str(error)


def error_window(app, message):
    window = Adw.ApplicationWindow(
        application=app, title="Ngó Sen", default_width=520, default_height=360
    )
    page = Adw.StatusPage(
        icon_name="dialog-error-symbolic", title=_("Cannot read the settings"), description=message
    )
    view = Adw.ToolbarView(content=page)
    view.add_top_bar(Adw.HeaderBar())
    window.set_content(view)
    return window


def load(engine):
    """The store and the names to offer, or a message saying why the engine did not answer."""
    try:
        names = {"InputMethod": engine.input_methods(), "OutputCharset": engine.charsets()}
        return Store(engine), names, None
    except (OSError, subprocess.SubprocessError) as error:
        return None, None, str(error)


def requested_page(argv):
    """The NAME of --page NAME, as the fcitx5 settings take it."""
    if "--page" in argv[:-1]:
        return argv[argv.index("--page") + 1]
    return None


def main(engine_path, localedir, version):
    i18n.setup(localedir)
    # A second launch hands its arguments to the open window, which then shows the page asked for.
    app = Adw.Application(application_id=APP_ID, flags=Gio.ApplicationFlags.HANDLES_COMMAND_LINE)

    def activate(app):
        window = app.get_active_window()
        if window is None:
            brand.install_css()
            engine = Engine(os.environ.get("NGOSEN_WL_DAEMON", engine_path))
            store, names, error = load(engine)
            window = (
                error_window(app, error)
                if error
                else Window(app, store, names, version, engine=engine)
            )
        window.present()
        return window

    def command_line(app, command_line):
        window = activate(app)
        page = requested_page(command_line.get_arguments())
        if page is not None and isinstance(window, Window):
            window.show_page(page)
        return 0

    app.connect("activate", activate)
    app.connect("command-line", command_line)
    return app.run(sys.argv)
