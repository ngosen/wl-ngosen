#!/bin/bash
# Writes wl-ngosen-<version>.tar.gz with the ngosen submodule inside, for the RPM builds.
#
#   packaging/make-source-tarball.sh <output-dir>
set -euo pipefail

out=${1:?usage: make-source-tarball.sh <output-dir>}
cd "$(dirname "$0")/.."
version=$(sed -n 's/^project(wl-ngosen VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
name=wl-ngosen-$version
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

git archive --prefix="$name/" -o "$work/top.tar" HEAD
git -C ngosen archive --prefix="$name/ngosen/" -o "$work/core.tar" HEAD
tar --concatenate -f "$work/top.tar" "$work/core.tar"
mkdir -p "$out"
gzip -n -9 < "$work/top.tar" > "$out/$name.tar.gz"
echo "$out/$name.tar.gz"
