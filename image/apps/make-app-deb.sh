#!/usr/bin/env bash
# Pack one ZeroOS app (docs/APPS.md) into zeroos-app-<id>_<version>_<arch>.deb
# and print its block for zeroos-apps.index on stdout.
#
# Usage: make-app-deb.sh <app.pkg> <out-dir> <version> <module.so>...
#   <app.pkg>     what the app is and what it brings (see the .pkg files here)
#   <module.so>   the app module(s); each goes to /usr/lib/zeroos/apps/
# Env: ZEROOS_ARCH (default arm64), ZEROOS_APP_ABI (default 1),
#      ZEROOS_FILES: the folder payload() takes files from
# Needs: dpkg-deb (no root).
#
# Other repositories (cardputer-zero-rf) use this too, from their copy of
# this repository.
set -euo pipefail
umask 022
PKG=$1 OUT=$2 VERSION=$3
shift 3
ARCH=${ZEROOS_ARCH:-arm64}
ABI=${ZEROOS_APP_ABI:-1}
F=${ZEROOS_FILES:-$(cd "$(dirname "$0")/../stage-zeroos/files" && pwd)}

ID=$(basename "$PKG" .pkg)
[[ $ID =~ ^[a-z0-9][a-z0-9-]*$ ]] || { echo "error: bad app id '$ID'" >&2; exit 1; }
case "$VERSION" in [0-9]*) ;; *) echo "error: version '$VERSION' must start with a digit" >&2; exit 1 ;; esac
[ $# -gt 0 ] || { echo "error: no module for $ID" >&2; exit 1; }

# What a .pkg sets (it is a bash fragment from this repository).
NAME="" SUMMARY="" DEPENDS="" NEEDS="" ENABLE="" STOP_ON_REMOVE="" DISABLE_DEPS="" HOMEPAGE=""
EXTRA_FILE="" EXTRA_URL="" EXTRA_SHA256=""
payload() { :; }
# shellcheck disable=SC1090
. "$PKG"
[ -n "$NAME" ] && [ -n "$SUMMARY" ] || { echo "error: $PKG needs NAME and SUMMARY" >&2; exit 1; }

STAGE=$(mktemp -d)
chmod 0755 "$STAGE"
trap 'rm -rf -- "${STAGE:?}"' EXIT
D=$STAGE
install -d "$D/usr/lib/zeroos/apps"
for m in "$@"; do
    [ -f "$m" ] || { echo "error: module $m not found" >&2; exit 1; }
    install -m 0644 "$m" "$D/usr/lib/zeroos/apps/$(basename "$m")"
done
payload
[ -n "$(ls -A "$D/usr/lib/zeroos/apps")" ] || { echo "error: $ID has no modules" >&2; exit 1; }

PACKAGE=zeroos-app-$ID
DEB=${PACKAGE}_${VERSION}_${ARCH}.deb
mkdir -p "$STAGE/DEBIAN" "$OUT"
deps="zeroos, zeroos-app-abi-$ABI"
[ -n "$DEPENDS" ] && deps="$deps, $DEPENDS"
cat > "$STAGE/DEBIAN/control" <<CONTROL
Package: $PACKAGE
Version: $VERSION
Architecture: $ARCH
Maintainer: ZeroOS <noreply@github.com>
Section: misc
Priority: optional
Depends: $deps
Installed-Size: $(du -sk --exclude=DEBIAN "$STAGE" | cut -f1)
Homepage: ${HOMEPAGE:-https://github.com/hartj77-blip/cardputer-zero}
Description: $NAME for ZeroOS
 $SUMMARY
 Installed by the Apps app (zeroos-apps).
CONTROL

# Settings files that belong to another package (gpsd's /etc/default/gpsd)
# travel under /usr/share/zeroos/app-overrides/<id>/ and are copied over /,
# keeping the one that was there as <file>.zeroos-old; removing the app
# puts it back.
cat > "$STAGE/DEBIAN/postinst" <<POSTINST
#!/bin/sh
set -e
[ "\$1" = configure ] || exit 0
O=/usr/share/zeroos/app-overrides/$ID
if [ -d "\$O" ]; then
    cd "\$O"
    find . -type f | sed 's|^\./||' | while read -r f; do
        if [ -f "/\$f" ] && [ ! -e "/\$f.zeroos-old" ] && ! cmp -s "\$f" "/\$f"; then cp -p "/\$f" "/\$f.zeroos-old"; fi
        install -D -m 0644 "\$f" "/\$f"
    done
fi
if [ -d /run/systemd/system ]; then
    systemctl daemon-reload || true
    for u in $DISABLE_DEPS; do systemctl disable --now "\$u" 2>/dev/null || true; done
    for u in $ENABLE; do systemctl enable --now "\$u" 2>/dev/null || true; done
else
    for u in $DISABLE_DEPS; do systemctl disable "\$u" 2>/dev/null || true; done
    for u in $ENABLE; do systemctl enable "\$u" 2>/dev/null || true; done
fi
command -v udevadm >/dev/null && udevadm control --reload 2>/dev/null || true
exit 0
POSTINST
cat > "$STAGE/DEBIAN/prerm" <<PRERM
#!/bin/sh
set -e
[ "\$1" = remove ] || exit 0
for u in $ENABLE $STOP_ON_REMOVE; do systemctl disable --now "\$u" 2>/dev/null || true; done
O=/usr/share/zeroos/app-overrides/$ID
if [ -d "\$O" ]; then
    cd "\$O"
    find . -type f | sed 's|^\./||' | while read -r f; do
        if [ -e "/\$f.zeroos-old" ]; then mv -f "/\$f.zeroos-old" "/\$f"; elif cmp -s "\$f" "/\$f"; then rm -f "/\$f"; fi
    done
fi
exit 0
PRERM
chmod 0755 "$STAGE/DEBIAN/postinst" "$STAGE/DEBIAN/prerm"
dpkg-deb --root-owner-group -Zxz --build "$STAGE" "$OUT/$DEB" >/dev/null

# Debian package names, without versions, as a reader would want them.
[ -n "$NEEDS" ] || NEEDS=$(echo "$DEPENDS" | sed -e 's/([^)]*)//g' -e 's/ *| *[^,]*//g' -e 's/  */ /g' -e 's/ ,/,/g' -e 's/^ //;s/ $//')
{
    echo "app=$ID"
    echo "name=$NAME"
    echo "summary=$SUMMARY"
    echo "version=$VERSION"
    echo "abi=$ABI"
    echo "file=$DEB"
    echo "sha256=$(sha256sum "$OUT/$DEB" | cut -d' ' -f1)"
    echo "size=$(stat -c %s "$OUT/$DEB")"
    [ -n "$NEEDS" ] && echo "needs=$NEEDS"
    if [ -n "$EXTRA_FILE" ]; then
        echo "extra-file=$EXTRA_FILE"
        echo "extra-url=$EXTRA_URL"
        echo "extra-sha256=$EXTRA_SHA256"
    fi
    echo
}
