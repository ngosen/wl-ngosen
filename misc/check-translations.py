#!/usr/bin/env python3
"""Fails when a string of the settings window or the tray menu has no Vietnamese, or when a string both editions
use is translated differently here than in the fcitx5 edition.

    misc/check-translations.py [--print-missing]
"""

import ast
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCES = sorted((ROOT / "settings" / "ngosen_settings").glob("*.py"))
ENGINE_SOURCES = sorted((ROOT / "src").glob("*.cpp")) + sorted(
    (ROOT / "src" / "shared").glob("*.cpp")
)
CPP_STRING = re.compile(r'\bN?_\("((?:[^"\\]|\\.)*)"\)')
# The core asks the host to translate its status lines, so they come from our catalog.
CORE_SOURCES = sorted((ROOT / "ngosen" / "src" / "core").glob("*.cpp"))
CORE_STRING = re.compile(r'\btranslate\("((?:[^"\\]|\\.)*)"\)')
# This edition has no emoji picker, so its paging label never appears.
CORE_UNSHOWN = {"Page "}
OURS = ROOT / "po" / "vi.po"
FULL_EDITION = ROOT / "ngosen" / "po" / "vi.po"


def used_strings():
    found = {}
    for path in SOURCES:
        for node in ast.walk(ast.parse(path.read_text(encoding="utf-8"))):
            if (
                isinstance(node, ast.Call)
                and isinstance(node.func, ast.Name)
                and node.func.id in ("_", "N_")
                and node.args
                and isinstance(node.args[0], ast.Constant)
                and isinstance(node.args[0].value, str)
            ):
                found.setdefault(node.args[0].value, f"{path.relative_to(ROOT)}:{node.lineno}")
    for path in ENGINE_SOURCES:
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            for match in CPP_STRING.finditer(line):
                text = ast.literal_eval(f'"{match.group(1)}"')
                found.setdefault(text, f"{path.relative_to(ROOT)}:{number}")
    for path in CORE_SOURCES:
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            for match in CORE_STRING.finditer(line):
                text = ast.literal_eval(f'"{match.group(1)}"')
                if text not in CORE_UNSHOWN:
                    found.setdefault(text, f"{path.relative_to(ROOT)}:{number}")
    return found


def placeholders(text):
    return sorted(re.findall(r"\{\w*\}", text))


def read_po(path):
    """msgid → msgstr, skipping the header and fuzzy entries."""
    catalog = {}
    for block in path.read_text(encoding="utf-8").split("\n\n"):
        if "#, fuzzy" in block:
            continue
        fields, current = {}, None
        for line in block.splitlines():
            if line.startswith(("msgid ", "msgstr ")):
                current, _, rest = line.partition(" ")
                fields[current] = ast.literal_eval(rest)
            elif line.startswith('"') and current:
                fields[current] += ast.literal_eval(line)
        if fields.get("msgid"):
            catalog[fields["msgid"]] = fields.get("msgstr", "")
    return catalog


def main():
    used = used_strings()
    ours = read_po(OURS)
    full = read_po(FULL_EDITION)
    errors = []
    for msgid, where in used.items():
        if not ours.get(msgid):
            errors.append(f"{where}: no Vietnamese for {msgid!r}")
        elif placeholders(msgid) != placeholders(ours[msgid]):
            errors.append(f"{where}: {ours[msgid]!r} does not have the placeholders of {msgid!r}")
        elif msgid in full and full[msgid] != ours[msgid]:
            errors.append(
                f"{where}: {msgid!r} is {ours[msgid]!r} here but {full[msgid]!r} in the fcitx5 edition"
            )
    if "--print-missing" in sys.argv:
        for msgid in used:
            if not ours.get(msgid):
                print(f"msgid {msgid!r}\nmsgstr {full.get(msgid, '')!r}\n")
        return 0
    for error in errors:
        print(error)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
