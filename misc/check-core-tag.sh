#!/bin/bash
# Fails unless the ngosen submodule points at a released ngosen tag, so a release never ships typing
# logic that users of fcitx5-ngosen have not had.
#
#   misc/check-core-tag.sh
set -euo pipefail
cd "$(dirname "$0")/.."

commit=$(git rev-parse HEAD:ngosen)
url=$(git config -f .gitmodules submodule.ngosen.url)
# Annotated tags list the commit on their "^{}" line, lightweight tags on their own line.
tags=$(git ls-remote --tags "$url" 'ngosen-*' |
    awk -v c="$commit" '$1 == c { sub("refs/tags/", "", $2); sub("\\^\\{\\}$", "", $2); print $2 }' |
    sort -u)
if [ -z "$tags" ]; then
    echo "The ngosen submodule points at $commit, which no ngosen-* tag names."
    exit 1
fi
echo "The ngosen submodule points at $tags."
