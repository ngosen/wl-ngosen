#!/bin/bash
# Runs the installed wl-ngosen the way a user starts it, so a dependency the package forgot shows
# up here: the daemon answers, and the settings window stays open under Xvfb until it is stopped.
#
#   packaging/check-installed.sh
set -euo pipefail

wl-ngosen --version
wl-ngosen --print-settings > /dev/null

log=$(mktemp)
status=0
GDK_BACKEND=x11 timeout 15 xvfb-run -a wl-ngosen-settings > "$log" 2>&1 || status=$?
cat "$log"
# 124 is timeout stopping a window that was still open; anything else means it never came up.
if [ "$status" != 124 ]; then
    echo "the settings window exited with $status" >&2
    exit 1
fi
if grep -q Traceback "$log"; then
    echo "the settings window raised an exception" >&2
    exit 1
fi
echo "wl-ngosen and its settings window run"
