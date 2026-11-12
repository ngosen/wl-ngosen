#!/bin/bash
# Fails when a tracked file is named "lotus" or mentions it. The project is Ngó Sen; fcitx5-lotus is
# named only in the credit line, which links to its repository.
#
#   misc/check-new-names.sh
set -euo pipefail
cd "$(dirname "$0")/.."

status=0
names=$(git ls-files | grep -i lotus || true)
if [ -n "$names" ]; then
    echo "Files must not be named lotus:"
    echo "$names" | sed 's/^/  /'
    status=1
fi

# git grep stays out of the ngosen submodule, whose legacy names are its own business.
hits=$(git grep -n -i lotus -- . ':!misc/check-new-names.sh' ':!AGENTS.md' |
    grep -v 'github.com/LotusInputMethod/fcitx5-lotus' || true)
if [ -n "$hits" ]; then
    echo "Only the credit line may mention lotus:"
    echo "$hits" | sed 's/^/  /'
    status=1
fi
exit $status
