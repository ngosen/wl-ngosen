# AGENTS.md

Rules for anyone (human or coding agent) changing this repository. The typing logic lives in the
`ngosen` submodule ([ngosen/ngosen](https://github.com/ngosen/ngosen)); its `AGENTS.md` applies to
anything sent there, and the rules below follow it. Outside the code, fcitx5-lotus is named only in the
README credit line and the credit line in the settings About dialog.

Most of this work is done with an AI coding agent. That is allowed, but the output has to read like a
careful human wrote it, and the person sending it must be able to explain every line.

## What this repository is

Ngó Sen for Wayland compositors that offer input-method-v2 (niri, Sway, Hyprland, labwc, MangoWC),
without fcitx5 or IBus. A small daemon takes the keyboard grab, types through the core, forwards the
keys it does not take through a virtual keyboard, learns the focused app from
wlr-foreign-toplevel-management, and shows a tray icon with the typing modes. It shares
`~/.config/ngosen` and `~/.local/state/ngosen` with the IBus edition, so both read and write the same
files the same way.

The config, app modes, typing log and settings window are copied from
[ibus-ngosen](https://github.com/ngosen/ibus-ngosen) at the commit in `COPIED_FROM`. Change them in
both repositories, or not at all, until they move beside the core.

This edition is experimental. X11 and Xwayland apps, GNOME and KDE are out of scope.

## Code

- **English only** for identifiers, comments, log strings and commit messages. User-facing strings are
  translatable, with Vietnamese in `po/vi.po`.
- **Comments say why, not what or how.** Most comments are one line; a block of more than 3 lines needs a
  real reason.
- **No measurements, dates or history in code.** They belong in the commit message or PR. Refer to a real
  issue as `#123`, and to one in the core repository as `ngosen/ngosen#123`.
- **Typing logic goes into the core, not here.** This repository turns Wayland events into
  `ngosen::Host`, `ngosen::KeyPress` and `ngosen::EngineResources` (headers in `ngosen/src/core/`). A
  change in how words are typed is a PR to ngosen/ngosen.
- **The submodule points at a released `ngosen-*` tag** on `main` and in every release. A branch may pin
  an untagged commit while a core change waits for its release; CI refuses it on the way to `main`.
- **Follow the wire contract in `docs/wire-contract.md`.** No request to the compositor after
  `deactivate`; release pressed virtual keys before the grab; never forward a keymap of size 0.
- **No "lotus" in names.** `misc/check-new-names.sh` enforces this in CI.
- **Match surrounding code.** clang-format (`.clang-format`), ruff for `settings/`, `camelCase`
  functions, `snake_case_` members, functions under ~50 lines. Run clang-format before pushing; CI fails
  on any diff.
- **Build as C++17.** Ubuntu 24.04 is the oldest system supported.
- **A bug fix comes with a test that fails without it.** Check this by undoing the fix and watching that
  test, and only that test, fail.
- **Reuse one build directory.** Rebuild in `build/`; a new directory per experiment wears out the SSD.

## Testing on a real compositor

`ctest` runs the core tests and a test compositor (`test/compositor/`) that drives the daemon over a
real Wayland socket. How text reaches real apps still differs per compositor and toolkit, so a change
in how the daemon commits, deletes, follows focus or forwards keys is checked on niri, Sway and
Hyprland with a GTK4 editor, Firefox and Chrome. Write down before each run which runs should fail and
which should pass, and keep the daemon log, the typing log and a `WAYLAND_DEBUG=1` capture of every
run.

## Commits

- Conventional Commits, imperative subject of at most 72 characters.
- The body says what was wrong for the user and why this fixes it. Numbers go here, one line each, with
  how they were measured.
- One logical change per commit; every commit builds and passes `ctest`.
- No AI trailer in commits. AI assistance is disclosed in PR descriptions and comments, which end with the
  line `Assisted-by: AI`.

## Pull requests

Every change goes through a PR. Features go into `next`; a fix for a bug in the current release goes
into `patch`. Never merge into `main` directly; `main` changes only through a release PR
([RELEASING.md](RELEASING.md)). Each PR adds a line under `## [Chưa phát hành]` in
[CHANGELOG.md](CHANGELOG.md): what changed for the user, with the PR number, in Vietnamese.
