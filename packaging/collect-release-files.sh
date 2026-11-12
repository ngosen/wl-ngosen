#!/bin/bash
# Picks the packages a release publishes out of the CI artifacts and names them as the shared
# package repositories of ngosen/ngosen expect. Development targets (Debian unstable, Fedora
# Rawhide) are built by CI but not published. Debian testing is published because Kali Rolling
# follows it.
#
#   packaging/collect-release-files.sh <artifacts-dir> <output-dir>
set -euo pipefail

src=${1:?usage: collect-release-files.sh <artifacts-dir> <output-dir>}
out=${2:?usage: collect-release-files.sh <artifacts-dir> <output-dir>}
mkdir -p "$out"

# Prints the single file under $1 matching $2; anything else means the build changed shape.
only() {
    local found
    found=$(find "$1" -type f -name "$2" | sort)
    if [ "$(printf '%s\n' "$found" | grep -c .)" != 1 ]; then
        echo "expected exactly one $2 under $1, found: ${found:-none}" >&2
        exit 1
    fi
    printf '%s\n' "$found"
}

# No bookworm: Debian 12's libadwaita is older than the settings window needs.
for codename in trixie forky noble resolute stonking; do
    for arch in amd64 arm64; do
        dir=$src/deb-$codename
        [ "$arch" = amd64 ] || dir=$dir-$arch
        deb=$(only "$dir" "wl-ngosen_*_$arch.deb")
        cp "$deb" "$out/$(basename "$deb" "_$arch.deb")_${codename}_$arch.deb"
    done
done

# .github/package-matrix.json decides which Fedora releases are built; a numbered one is published.
for dir in "$src"/rpm-fedora-[0-9]*; do
    case "$dir" in
        *-arm64) arch=aarch64 ;;
        *) arch=x86_64 ;;
    esac
    cp "$(only "$dir" "wl-ngosen-[0-9]*.$arch.rpm")" "$out/"
done

# openSUSE names its RPMs without a dist tag, so the name says which distribution it is for.
rpm=$(only "$src/rpm-opensuse-tumbleweed" 'wl-ngosen-[0-9]*.x86_64.rpm')
cp "$rpm" "$out/$(basename "$rpm" .x86_64.rpm).opensuse-tumbleweed.x86_64.rpm"

cp "$(only "$src/arch-pkg" 'wl-ngosen-[0-9]*-x86_64.pkg.tar.zst')" "$out/"
cp "$(only "$src/source" 'wl-ngosen-[0-9]*.tar.gz')" "$out/"

# Written outside the directory first, so a rerun never lists the checksum file in itself.
sums=$(mktemp)
(cd "$out" && rm -f SHA256SUMS && sha256sum -- * > "$sums")
mv "$sums" "$out/SHA256SUMS"
chmod 644 "$out/SHA256SUMS"
