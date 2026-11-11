#!/bin/bash
# Builds the source RPM of a published release from its own tarball, for COPR.
#
#   packaging/copr-srpm.sh <release tag> <output-dir>
set -euo pipefail

tag=${1:?usage: copr-srpm.sh <release tag> <output-dir>}
out=${2:?usage: copr-srpm.sh <release tag> <output-dir>}
cd "$(dirname "$0")/.."
sources=$(mktemp -d)
trap 'rm -rf "$sources"' EXIT

gh release download "$tag" --repo ngosen/wl-ngosen --pattern 'wl-ngosen-*.tar.gz' --pattern SHA256SUMS --dir "$sources"
(cd "$sources" && sha256sum --check --ignore-missing SHA256SUMS)
mkdir -p "$out"
rpmbuild -bs --define "_sourcedir $sources" --define "_srcrpmdir $(realpath "$out")" packaging/rpm/wl-ngosen.spec
