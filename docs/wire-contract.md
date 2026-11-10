# Wire contract

What the daemon sends to the compositor for each `ngosen::Host` call, and what it does with each
protocol event. Protocols: `zwp_input_method_v2` (im), its keyboard grab (grab),
`zwp_virtual_keyboard_v1` (vk), `zwlr_foreign_toplevel_handle_v1` (toplevel). Where fcitx5's own
input-method-v2 frontend (`waylandimserverv2.cpp`) does the same, this edition reports frontend
`wayland_v2` so the core treats both alike.

## Start and stop

| Situation | Daemon |
| --- | --- |
| No `wl_seat`, `zwp_input_method_manager_v2` or `zwp_virtual_keyboard_manager_v1` | Log and exit 0 (GNOME, KDE, or a compositor without input methods). |
| More than one seat | Use the first; log the others. |
| Start | Create im and vk for the seat. The grab is taken on the first `activate`. |
| Grab `keymap` | Keep the fd and size; give the same keymap to vk. Size 0 is logged and ignored. |
| Key before any keymap | Logged and dropped. |
| A keymap that cannot be read, compiled or handed to vk | Log and exit 1. |
| im `unavailable` | Release pressed vk keys, send the grab's modifiers on vk, destroy grab, im and vk, notify once, exit 1. |
| A global removed mid-session | As `unavailable`. |
| SIGINT/SIGTERM | As `unavailable`, exit 0. |

## Events of `zwp_input_method_v2`

Events are double-buffered and applied on `done`; the daemon counts `done` events as the serial for
`commit`.

| Event | Daemon |
| --- | --- |
| `activate` | Pending: active, fresh field (no surrounding text, no content type, cause `input_method`). |
| `deactivate` | Pending: inactive. The last of `activate`/`deactivate` before `done` wins. |
| `surrounding_text` | Pending text, cursor, anchor (bytes). |
| `text_change_cause` | Pending cause. |
| `content_type` | Pending hint and purpose. |
| `done` | Apply. Became active: take the grab (releasing an old one first), focus the core. Became inactive: release pressed vk keys, restore modifiers, release the grab, unfocus the core. Still active with new text: give it to the core. The cause is not acted on, as in fcitx5: GTK 4 keeps showing its preedit after a change it made, so a reset would wipe the word. |

## Field

| `Field` member | Value |
| --- | --- |
| `frontend` | `wayland_v2` |
| `program`, `reportedProgram` | app_id of the activated toplevel; empty when none (layer-shell launchers) |
| `surroundingText` | the app sent `surrounding_text` before the first `done` after `activate` |
| `preedit`, `formattedPreedit` | true |
| `password` | hint `hidden_text` or `sensitive_data`, or purpose `password` or `pin` |
| `terminal` | purpose `terminal` |
| `url` | purpose `url` |
| `sendsNoKeys`, `keyEventOrderFix`, `preeditFallback` | false |

The core branches on `wayland_v2` in `forwardsBackspaces`, `dropsCommitsWithoutFocus`,
`deletesThroughSurroundingFirst`, `keepsOldCopyAfterEdit`, `countsDeletionOnCopy` and the backspace
wait in `ngosen-state-replace.cpp`. Each assumes what this daemon also does: backspaces go through a
virtual keyboard apart from the commit, and nothing reaches an unfocused field.

## Host calls

Nothing is sent while inactive or after a pending `deactivate` was read; the call is dropped.

| Host call | Wire |
| --- | --- |
| `commitText` | gathered for im `commit_string` |
| `showPreedit` / `clearPreedit` + `refreshPreedit` | gathered for im `set_preedit_string`, cursor at the end in bytes |
| `deleteSurrounding(offset, size)` | as fcitx5: only a range that holds the cursor (`offset <= 0 <= offset + size`) can be sent; characters → bytes against the last surrounding text, gathered for `delete_surrounding_text(before, after)`. Anything else, or no valid surrounding text, is dropped and logged. |
| `forwardKey`, `forwardKeyPress` | vk `key` with the evdev code; modifiers as below |
| `pressSystemKeys` | false |
| `surrounding` | last applied text; invalid on bad UTF-8, a cursor or anchor out of range, or no `surrounding_text` since `activate` |
| `hasFocus` | active after the last `done` |
| `takeSelection` | empty |
| `startTimer` | a timerfd on the main loop |
| candidates, panel, status | no-ops (no candidate list in this edition) |
| `keyText` | xkbcommon `xkb_keysym_to_utf8` |

What is gathered goes in one `commit(serial)` after each event or timer, and before any vk key, so a
commit reaches the field before the key that follows it. The order within it is the protocol's:
`delete_surrounding_text`, `commit_string`, `set_preedit_string`. The preedit is sent with every
commit while there is one, since a commit without it clears it.

Before each commit the daemon reads pending events without blocking, so the serial is the latest; if a
`deactivate` arrived, nothing is committed, and what was gathered is dropped.

On focus out the core drops the word being typed: a commit after `deactivate` would be stale, and the
word must not reach the next field. A password or PIN purpose skips the core; keys go to vk as typed.

## Keys

- Every grab key goes to the core. A key the core does not take is sent on vk with the same evdev code
  and timestamp; vk keeps the grab's keymap, resent whenever the grab sends a new one.
- Modifiers: each vk `modifiers` request carries the latest grab state (depressed, latched, locked,
  group). A key the core sends with Shift gets Shift OR-ed onto that state, and the grab's state is
  sent again right after the key.
- Repeat: the grab sends a held key once, so the daemon repeats it from the grab's `repeat_info`, as
  fcitx5 does. Each repeat first sends the key's release on vk, then goes to the core marked as a
  repeat; one the core does not take is pressed again on vk. The app's own repeat never starts. Only
  keys the keymap marks as repeating repeat; rate 0 turns repeat off. The repeat ends on the key's
  release, another key's press, or focus out.
- Every vk press is matched by a release. Pressed vk keys are released on deactivate, `unavailable`
  and exit, before the grab is released.
- A key the core sends that is not in the keymap (`keyText` but no keycode) is committed as text.
- Dead keys and Compose: the grab delivers raw keys; xkbcommon compose state turns them into the
  keysym the core gets. AltGr and layout groups come from the grab's group.

## App names

`zwlr_foreign_toplevel_manager_v1`: the toplevel with `activated` in its last `done` names the app.
The app is read again at every key, because `activated` can arrive after the im `activate`.
