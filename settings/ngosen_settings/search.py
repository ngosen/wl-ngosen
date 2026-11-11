# SPDX-FileCopyrightText: 2026 Ngó Sen contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Finds settings by the row titles shown on screen."""

import unicodedata

from gi.repository import Adw, Gtk


def fold(text):
    """Lower case without tone marks, so "chinh ta" finds "Chính tả"."""
    text = text.replace("đ", "d").replace("Đ", "D")
    decomposed = unicodedata.normalize("NFD", text)
    return "".join(c for c in decomposed if not unicodedata.combining(c)).casefold()


def find(pages, query):
    """Returns (title, row, page) for each row whose title holds every word of the query.

    A widget with searchable = False is skipped with everything inside it, so lists of the
    user's own entries do not show up as settings.
    """
    words = fold(query).split()
    if not words:
        return []
    found = []
    for page in pages:
        for row in _rows(page):
            title = row.get_title().strip().rstrip(":")
            if title and len(title) <= 80 and all(w in fold(title) for w in words):
                found.append((title, row, page))
    return found


def _rows(widget):
    child = widget.get_first_child()
    while child is not None:
        if child.get_visible() and getattr(child, "searchable", True):
            if isinstance(child, Adw.PreferencesRow):
                yield child
            yield from _rows(child)
        child = child.get_next_sibling()


def focus_when_laid_out(row):
    """Focuses a row and scrolls to it, waiting for a page that was never shown to get its size."""
    row.grab_focus()
    scrolled = row.get_ancestor(Gtk.ScrolledWindow)
    adjustment = scrolled.get_vadjustment() if scrolled else None
    if adjustment is None or adjustment.get_page_size() > 0:
        return

    def laid_out(adjustment):
        if adjustment.get_page_size() == 0:
            return
        adjustment.disconnect(handler)
        root = row.get_root()
        if root is None:
            return
        # A row already holding the focus is not scrolled to again.
        root.set_focus(None)
        row.grab_focus()

    handler = adjustment.connect("changed", laid_out)
