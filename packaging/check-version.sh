#!/bin/bash
# Fails unless every packaging file carries the given version, e.g. 0.1.0-1. A release tag that
# disagrees with them would publish packages under the wrong version.
#
#   packaging/check-version.sh <version>-<release>
set -euo pipefail

want=${1:?usage: check-version.sh <version>-<release>}
cd "$(dirname "$0")"
fail=0

check() {
    if [ "$2" = "$3" ]; then
        echo "ok        $1: $2"
    else
        echo "MISMATCH  $1: $2 (want $3)"
        fail=1
    fi
}

pkgbuild_version() {
    echo "$(sed -n 's/^pkgver=//p' "$1")-$(sed -n 's/^pkgrel=//p' "$1")"
}

spec_version() {
    echo "$(sed -n 's/^Version: *//p' "$1")-$(sed -n 's/^%global pkgrel *//p' "$1")"
}

# rpmbuild warns about a changelog date whose weekday is wrong, and dpkg does not check at all.
weekday_of() {
    LC_ALL=C date -d "$1" +%a
}

check debian/changelog "$(sed -n '1s/^[^(]*(\([^)]*\)).*/\1/p' debian/changelog)" "$want"
check rpm "$(spec_version rpm/wl-ngosen.spec)" "$want"
check rpm/opensuse "$(spec_version rpm/opensuse/wl-ngosen.spec)" "$want"
check arch/PKGBUILD "$(pkgbuild_version arch/PKGBUILD)" "$want"

deb_date=$(sed -n '/^ -- /{s/.*>  *//;p;q}' debian/changelog)
check "debian/changelog weekday" "${deb_date%%,*}" "$(weekday_of "$(echo "$deb_date" | cut -d' ' -f2-4)")"

for spec in rpm/wl-ngosen.spec rpm/opensuse/wl-ngosen.spec; do
    entry=$(sed -n '/^\* /{p;q}' "$spec")
    check "$spec weekday" "$(echo "$entry" | cut -d' ' -f2)" "$(weekday_of "$(echo "$entry" | cut -d' ' -f3-5)")"
    check "$spec changelog" "$(echo "$entry" | sed -n 's/.* - //p')" "$want"
done

check CMakeLists.txt "$(sed -n 's/^project(wl-ngosen VERSION \([0-9.]*\).*/\1/p' ../CMakeLists.txt)" "${want%-*}"
exit "$fail"
