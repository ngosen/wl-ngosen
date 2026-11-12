#!/bin/bash
# Runs the installed wl-ngosen the way a user starts it, so a dependency the package forgot shows
# up here: the daemon answers, and the settings window appears under Xvfb.
#
#   packaging/check-installed.sh
set -euo pipefail

# Inside Xvfb: a window that never maps passes a plain timeout, so wait for the window itself.
if [ "${1-}" = --inside-xvfb ]; then
    wl-ngosen-settings &
    app=$!
    for _ in $(seq 60); do
        # Matched by class: under the C locale xwininfo cannot print the Vietnamese title.
        id=$(xwininfo -root -tree | sed -n 's/^ *\(0x[0-9a-f]*\) .*("wl-ngosen-settings" "wl-ngosen-settings").*/\1/p' | head -1)
        if [ -n "$id" ] && xwininfo -id "$id" | grep -q 'Map State: IsViewable'; then
            kill "$app"
            echo "the settings window is open"
            exit 0
        fi
        kill -0 "$app" 2> /dev/null || break
        sleep 0.5
    done
    kill "$app" 2> /dev/null || true
    echo "the settings window never appeared" >&2
    exit 1
fi

wl-ngosen --version
wl-ngosen --print-settings > /dev/null

log=$(mktemp)
status=0
GDK_BACKEND=x11 xvfb-run -a "$0" --inside-xvfb > "$log" 2>&1 || status=$?
cat "$log"
if [ "$status" != 0 ] || grep -q Traceback "$log"; then
    echo "the settings window did not start cleanly" >&2
    exit 1
fi
echo "wl-ngosen and its settings window run"
