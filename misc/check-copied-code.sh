#!/bin/bash
# Lists the files in COPIED_FROM that ibus-ngosen next changed since the recorded commit, so the
# change can be brought over. Exits 1 when there is one.
#
#   misc/check-copied-code.sh [ibus-ngosen checkout]
set -euo pipefail
cd "$(dirname "$0")/.."

commit=$(awk '$1 == "commit" { print $2 }' COPIED_FROM)
files=$(awk '/^(src|test)\// { print $1 }' COPIED_FROM)
repo=${1:-}
if [ -z "$repo" ]; then
    repo=$(mktemp -d)
    trap 'rm -rf "$repo"' EXIT
    git clone -q --filter=blob:none --branch next https://github.com/ngosen/ibus-ngosen.git "$repo"
fi
# shellcheck disable=SC2086
changed=$(git -C "$repo" diff --name-only "$commit" origin/next -- $files)
if [ -n "$changed" ]; then
    echo "ibus-ngosen next changed these copied files since $commit:"
    echo "$changed" | sed 's/^/  /'
    exit 1
fi
echo "The copied files match ibus-ngosen next."
