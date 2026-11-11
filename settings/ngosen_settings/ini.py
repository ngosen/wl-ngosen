# SPDX-FileCopyrightText: 2026 Ngó Sen contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""The ini dialect of fcitx5, as src/ngosen-ibus-config.cpp reads it."""

ESCAPES = {"\\": "\\", '"': '"', "n": "\n", "f": "\f", "r": "\r", "t": "\t", "v": "\v"}
ESCAPED = {char: "\\" + letter for letter, char in ESCAPES.items()}
NEEDS_QUOTES = set(' \f\r\t\v"\\\n')
SPACE = " \t\r\f\v"


def unescape(value):
    """The value fcitx5 reads, or None when it drops the line."""
    if len(value) < 2 or value[0] != '"' or value[-1] != '"':
        return value
    out = []
    chars = iter(value[1:-1])
    for char in chars:
        if char == '"':
            return None
        if char != "\\":
            out.append(char)
            continue
        letter = next(chars, None)
        if letter not in ESCAPES:
            return None
        out.append(ESCAPES[letter])
    return "".join(out)


def escape(value):
    if not NEEDS_QUOTES.intersection(value):
        return value
    return '"' + "".join(ESCAPED.get(char, char) for char in value) + '"'


def lines(text):
    """Lines as std::getline sees them; str.splitlines() also splits on U+2028 and others."""
    parts = text.split("\n")
    return parts[:-1] if parts and parts[-1] == "" else parts


def parse(text):
    """Keys before the first section, and each section's keys."""
    top, sections = {}, {}
    current = top
    for line in lines(text):
        line = line.strip(SPACE)
        if not line or line.startswith("#"):
            continue
        if line.startswith("[") and line.endswith("]"):
            current = sections.setdefault(line[1:-1], {})
            continue
        key, equal, raw = line.partition("=")
        if not equal:
            continue
        value = unescape(raw)
        if value is not None:
            current[key] = value
    return top, sections


def read_list(sections, name):
    """Key/Value pairs of [name/0], [name/1]…; fcitx5 stops at the first gap."""
    entries = []
    while (section := sections.get(f"{name}/{len(entries)}")) is not None:
        entries.append((section.get("Key", ""), section.get("Value", "")))
    return entries


def list_text(name, entries):
    return "".join(
        f"[{name}/{i}]\nKey={escape(key)}\nValue={escape(value)}\n\n"
        for i, (key, value) in enumerate(entries)
    )


def replace_list(text, name, entries):
    """text with the list name holding entries, keeping every line outside the list as it was."""
    kept, at, inside = [], None, False
    for row in lines(text):
        stripped = row.strip(SPACE)
        if stripped.startswith("[") and stripped.endswith("]"):
            inside = stripped[1:-1].startswith(name + "/")
            if inside and at is None:
                at = len(kept)
        if not inside:
            kept.append(row)
    if at is None:
        at = len(kept)
        if kept and kept[-1].strip(SPACE) and entries:
            kept.append("")
            at += 1
    kept[at:at] = lines(list_text(name, entries))
    return "".join(row + "\n" for row in kept)


def set_value(text, key, value):
    """text with key set to value, keeping every other line as it was."""
    rows = lines(text)
    first_section = next(
        (i for i, row in enumerate(rows) if row.strip(SPACE).startswith("[")), len(rows)
    )
    new_row = f"{key}={escape(value)}"
    found = False
    for i in range(first_section):
        if rows[i].strip(SPACE).partition("=")[0] == key and "=" in rows[i]:
            rows[i] = new_row
            found = True
    if not found:
        rows.insert(first_section, new_row)
    return "\n".join(rows) + "\n"
