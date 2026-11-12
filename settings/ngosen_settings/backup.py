# SPDX-FileCopyrightText: 2026 Ngó Sen contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Backups in the JSON of the fcitx5 edition, so either edition restores the other's."""

import json
import os
import tempfile
from datetime import datetime

from . import ini, keys
from .i18n import N_, _
from .store import KEYMAP_LIST, MACRO_LIST, read_words

COMPONENTS = ("config", "macros", "keymaps", "rules", "switches", "dictionary")
# The fcitx5 edition keeps these as Mode, VietnameseOn and indexed key lists.
# Key lists fcitx5 writes as {"0": …}; the engine keeps them as one line split at spaces.
KEY_LISTS = ("ModeMenuKey", "CycleModeKey")
IBUS_ONLY = ("DirectTyping", "ToggleKey", *KEY_LISTS)
# fcitx5 names its modes OFF, Sen, Preedit and Emoji Picker; OFF types with Sen when Vietnamese is on.
DIRECT_TYPING = {"off": "True", "sen": "True", "preedit": "False", "emoji picker": "False"}
# The engine types English for 0, with Sen for 1, 2, 3, 4 and 8 and underlined for 5, as fcitx5 reads them.
APP_MODES = {"0", "1", "2", "3", "4", "5", "8"}
APP_MODES_HEADER = "# The typing mode picked for each app: 0 = Off, 2 = Sen, 5 = Preedit\n"
SWITCHES = "Switches"
NEEDS_MODIFIER = N_("A switch key here needs Ctrl, Alt, Shift or Super with another key.")


class RestoreError(OSError):
    """A restore failed and some files could not be put back."""

    def __init__(self, paths):
        super().__init__(_("These files could not be put back: {}").format(", ".join(paths)))
        self.paths = paths


def english_mark():
    base = os.environ.get("XDG_STATE_HOME") or os.path.expanduser("~/.local/state")
    return os.path.join(base, "ngosen", "english")


class Paths:
    """Every file a restore writes. The marker is there while typing English."""

    def __init__(self, store):
        self.settings = store.settings_file
        self.macros = store.macro_file
        self.keymaps = store.keymap_file
        self.dictionary = store.dictionary_file
        self.bundled_dictionary = store.bundled_dictionary
        folder = os.path.dirname(store.settings_file)
        self.app_modes = os.path.join(folder, "ngosen-app-rules.conf")
        self.app_switches = os.path.join(folder, "ngosen-app-switch.conf")
        self.english = english_mark()


def read_bytes(path):
    try:
        with open(path, "rb") as f:
            return f.read()
    except FileNotFoundError:
        return None


def put(path, data):
    """Writes data through a temporary file, or removes the file when data is None."""
    if data is None:
        try:
            os.unlink(path)
        except FileNotFoundError:
            pass
        return
    folder = os.path.dirname(path)
    # The state folder also holds typing logs, which only the user may read.
    os.makedirs(folder, mode=0o700, exist_ok=True)
    fd, temp = tempfile.mkstemp(dir=folder, prefix=os.path.basename(path) + ".")
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        os.replace(temp, path)
    except BaseException:
        os.unlink(temp)
        raise


def replace(changes, write=put):
    """Writes changes ({path: bytes or None}) and returns what the files held before.

    A failure puts back the files already written before it raises. Each file is replaced whole, but
    a crash between two files leaves some old and some new.
    """
    before = {path: read_bytes(path) for path in changes}
    written = []
    try:
        for path, data in changes.items():
            if data == before[path]:
                continue
            written.append(path)
            write(path, data)
    except Exception as error:
        failed = []
        for path in written:
            try:
                write(path, before[path])
            except OSError:
                failed.append(path)
        if failed:
            raise RestoreError(failed) from error
        raise
    return before


def undo(before, restored, write=put):
    """Puts back the files a restore replaced, only while they still hold what it wrote.

    Returns False and writes nothing when something changed one of them since.
    """
    if any(read_bytes(path) != restored[path] for path in before):
        return False
    replace(before, write)
    return True


def _app_rules(path):
    """The lines of the app modes file as fcitx5 backs up its rules, Off and Emoji included."""
    text = (read_bytes(path) or b"").decode("utf-8", "surrogateescape")
    rules = []
    for line in text.splitlines():
        app, equals, mode = line.partition("=")
        if equals and not line.startswith("#"):
            rules.append({"App": app.strip(), "Mode": mode.strip()})
    return rules


def _app_switches(path):
    """The language the toggle key left each app in, as fcitx5 backs up its switches."""
    # A name that is not UTF-8 matches no app, and would stop the backup from being written.
    sections = ini.parse((read_bytes(path) or b"").decode("utf-8", "replace"))[1]
    switches = []
    while (values := sections.get(f"{SWITCHES}/{len(switches)}")) is not None:
        switches.append({"App": values.get("App", ""), "Vietnamese": values.get("Vietnamese", "")})
    return switches


def _entries(path, name):
    text = read_bytes(path)
    entries = ini.read_list(ini.parse((text or b"").decode("utf-8"))[1], name)
    return [{"Key": key, "Value": value} for key, value in entries]


def export(engine, paths, target):
    """Writes the backup to target; raises OSError or ValueError."""
    values = engine.settings()
    config = {key: value for key, value in values.items() if key not in IBUS_ONLY}
    config["Mode"] = "Sen" if values.get("DirectTyping") == "True" else "Preedit"
    for name in ("ToggleKey", *KEY_LISTS):
        config[name] = {str(i): key for i, key in enumerate(values.get(name, "").split())}
    config["VietnameseOn"] = "False" if os.path.exists(paths.english) else "True"
    backup = {
        "config": config,
        "macros": _entries(paths.macros, MACRO_LIST),
        "keymaps": _entries(paths.keymaps, KEYMAP_LIST),
    }
    if os.path.exists(paths.app_modes):
        backup["rules"] = _app_rules(paths.app_modes)
    # Always, as fcitx5 does, so a restore there drops the switches its rules would lose to.
    backup["switches"] = _app_switches(paths.app_switches)
    # The list in effect, so restoring brings back the packaged one over a later own list.
    dictionary = read_bytes(paths.dictionary)
    if dictionary is None:
        dictionary = read_bytes(paths.bundled_dictionary)
    if dictionary is not None:
        backup["dictionary"] = dictionary.decode("utf-8")
    backup["meta"] = {
        "version": 1,
        "edition": "wl",
        "timestamp": datetime.now().isoformat(),
        "components": [k for k in COMPONENTS if k in backup],
    }
    put(target, json.dumps(backup, indent=2, ensure_ascii=False).encode("utf-8"))


def load(path):
    """The backup in path; raises OSError or ValueError when it is not one."""
    with open(path, encoding="utf-8") as f:
        backup = json.load(f)
    if not isinstance(backup, dict) or not any(k in backup for k in COMPONENTS):
        raise ValueError(_("No settings found in the file."))
    if not isinstance(backup.get("config", {}), dict):
        raise ValueError(_("The settings in the file are damaged."))
    for name in ("macros", "keymaps", "rules", "switches"):
        items = backup.get(name, [])
        if not isinstance(items, list) or not all(isinstance(item, dict) for item in items):
            raise ValueError(_("The lists in the file are damaged."))
    for name, fields in (
        ("macros", ("Key", "Value")),
        ("keymaps", ("Key", "Value")),
        ("rules", ("App", "Mode")),
        ("switches", ("App",)),
    ):
        for item in backup.get(name, []):
            if not all(isinstance(item.get(k, ""), str) for k in fields):
                raise ValueError(_("The lists in the file are damaged."))
    if not isinstance(backup.get("dictionary", ""), str):
        raise ValueError(_("The dictionary in the file is damaged."))
    try:
        # JSON can hold half of a surrogate pair, which no UTF-8 file can.
        json.dumps(backup, ensure_ascii=False).encode("utf-8")
    except UnicodeEncodeError:
        raise ValueError(_("The file holds text that is not valid Unicode.")) from None
    return backup


def _text(value):
    """A value as the settings file holds it; fcitx5 gives strings, a hand-made file may not."""
    if isinstance(value, bool):
        return "True" if value else "False"
    if isinstance(value, str):
        return value
    raise ValueError(_("The settings in the file are damaged."))


def _key_list(value):
    """fcitx5 gives a key list as {"0": …, "1": …}; fcitx5 stops at the first gap."""
    if value == "":
        return []
    if isinstance(value, str):
        return value.split()
    if isinstance(value, list):
        found = value
    elif isinstance(value, dict):
        found = []
        while str(len(found)) in value:
            found.append(value[str(len(found))])
    else:
        raise ValueError(_("The settings in the file are damaged."))
    if not all(isinstance(key, str) for key in found):
        raise ValueError(_("The settings in the file are damaged."))
    return found


def _shown(names):
    return ", ".join(keys.label(name) or name for name in names)


def _toggle_key(value, engine, taken, notes):
    """The ToggleKey line to write, or None to keep the current one."""
    wanted = _key_list(value)
    if not wanted:
        return ""
    # The setting is one line split at spaces, so a key with a space cannot be kept.
    candidates = [key for key in wanted if key and not any(c.isspace() for c in key)]
    usable = engine.usable_toggle_keys(candidates) if candidates else set()
    kept, dropped = [], {}
    for key in wanted:
        # The rules of the key dialog: no GNOME shortcut, no Shift with a key that types.
        reason = NEEDS_MODIFIER if key not in usable else keys.problem(keys.parse(key), taken)[0]
        if reason:
            dropped.setdefault(reason, []).append(key)
        else:
            kept.append(key)
    for reason, names in dropped.items():
        notes.append(
            _("These switch keys in the backup are left out: {}. {}").format(
                _shown(names), _(reason)
            )
        )
    if not kept:
        notes.append(_("The current switch key stays."))
        return None
    return " ".join(kept)


def _direct_typing(mode, notes):
    name = _text(mode)
    value = DIRECT_TYPING.get(name.lower())
    if value is None:
        notes.append(
            _(
                "The backup types in a mode this edition does not know ({}), so the current one stays."
            ).format(name)
        )
    elif name.lower() == "emoji picker":
        notes.append(_("This edition has no emoji picker, so words are underlined instead."))
    return value


def _settings(config, text, known, engine, taken, notes):
    for key in known:
        if key not in IBUS_ONLY and key in config:
            text = ini.set_value(text, key, _text(config[key]))
    if "Mode" in config:
        direct = _direct_typing(config["Mode"], notes)
        if direct is not None:
            text = ini.set_value(text, "DirectTyping", direct)
    if "ToggleKey" in config:
        toggle = _toggle_key(config["ToggleKey"], engine, taken, notes)
        if toggle is not None:
            text = ini.set_value(text, "ToggleKey", toggle)
    for name in KEY_LISTS:
        if name not in config:
            continue
        # The engine skips a key it cannot use; a key with a space would split the line.
        wanted = [
            key for key in _key_list(config[name]) if key and not any(c.isspace() for c in key)
        ]
        # An empty list adds no line to a file that never had the key.
        if wanted or name in ini.parse(text)[0]:
            text = ini.set_value(text, name, " ".join(wanted))
    return text.encode("utf-8", "surrogateescape")


def _english(config, paths, notes):
    """The marker's new content: b"" for English, None for Vietnamese."""
    value = _text(config["VietnameseOn"])
    if value not in ("True", "False"):
        raise ValueError(_("The settings in the file are damaged."))
    if value == "False" and not os.path.exists(paths.english):
        notes.append(_("The backup was made while typing English, so typing switches to English."))
    return None if value == "True" else b""


def _list_file(text, name, items):
    """text with the list replaced and every other section kept, as Store.set_macros does."""
    _top, sections = ini.parse(text)
    kept = "".join(
        f"[{section}]\n" + "".join(f"{k}={ini.escape(v)}\n" for k, v in values.items()) + "\n"
        for section, values in sections.items()
        if not section.startswith(name + "/")
    )
    entries = [(item.get("Key", ""), item.get("Value", "")) for item in items]
    return (kept + ini.list_text(name, entries)).encode("utf-8", "surrogateescape")


def _app_modes_file(current, rules, notes):
    """The app modes file holding rules, kept in full so a later fcitx5 restore gets them back."""
    lines = []
    for rule in rules:
        app, mode = rule.get("App", "").strip(), rule.get("Mode", "").strip()
        # A line break or "=" would turn one rule into another.
        if app and not any(c in app + mode for c in "=\n\r") and not app.startswith("#"):
            lines.append(f"{app}={mode}\n")
    if any(line.rstrip("\n").partition("=")[2] not in APP_MODES for line in lines):
        notes.append(_("Rules that pick emoji for an app are kept but do nothing here."))
    if current is None and not lines:
        return None
    return (APP_MODES_HEADER + "".join(lines)).encode("utf-8", "surrogateescape")


def _app_switches_file(current, switches):
    """The switches file as the engine writes it; an app name with "=" or a line break is left out."""
    kept = []
    for switch in switches:
        app = switch.get("App", "").strip()
        vietnamese = switch.get("Vietnamese")
        value = _text(vietnamese) if isinstance(vietnamese, (str, bool)) else ""
        if app and value in ("True", "False") and not any(c in app for c in "=\n\r["):
            kept.append(f"App={ini.escape(app)}\nVietnamese={value}\n")
    if current is None and not kept:
        return None
    sections = "".join(f"[{SWITCHES}/{i}]\n{entry}\n" for i, entry in enumerate(kept))
    return f"[{SWITCHES}]\n{sections}".encode("utf-8", "surrogateescape")


def _keymap_file(text, items):
    """text with the keymap replaced as the Keymap page writes it."""
    entries = [(item.get("Key", ""), item.get("Value", "")) for item in items]
    return ini.replace_list(text, KEYMAP_LIST, entries).encode("utf-8", "surrogateescape")


def _current(path):
    data = read_bytes(path)
    return None if data is None else data.decode("utf-8", "surrogateescape")


def plan(backup, paths, engine, taken=frozenset()):
    """The files to write for backup and what the user should know first. Writes nothing.

    taken holds GNOME's shortcuts as keys.gnome_chords() gives them, which a switch key may not use.

    Raises ValueError for a damaged backup, and OSError or SubprocessError when a file or the
    engine cannot be read.
    """
    notes = []
    changes = {}
    config = backup.get("config", {})
    if "config" in backup:
        known = list(engine.settings())
        changes[paths.settings] = _settings(
            config, _current(paths.settings) or "", known, engine, taken, notes
        )
    for name, path, write_list in (
        ("macros", paths.macros, lambda text, items: _list_file(text, MACRO_LIST, items)),
        ("keymaps", paths.keymaps, _keymap_file),
    ):
        if name in backup:
            current = _current(path)
            data = write_list(current or "", backup[name])
            # An empty list needs no new file.
            changes[path] = None if current is None and not data else data
    if "dictionary" in backup:
        text = backup["dictionary"]
        words = [w for w in (line.strip() for line in text.split("\n")) if w]
        # As Store.set_words: a copy of the packaged list would hide later updates to it.
        bundled = sorted(words) == sorted(read_words(paths.bundled_dictionary))
        changes[paths.dictionary] = None if bundled else text.encode("utf-8")
    if "rules" in backup:
        changes[paths.app_modes] = _app_modes_file(
            _current(paths.app_modes), backup["rules"], notes
        )
    # A backup from before switches existed: today's switches would override its rules.
    switches = backup.get("switches", [] if "rules" in backup else None)
    if switches is not None:
        changes[paths.app_switches] = _app_switches_file(_current(paths.app_switches), switches)
    # Last, so the running engine switches language only once the rest is written.
    if "VietnameseOn" in config:
        changes[paths.english] = _english(config, paths, notes)
    return changes, notes
