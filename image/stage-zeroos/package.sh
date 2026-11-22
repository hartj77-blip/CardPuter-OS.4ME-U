#!/usr/bin/env bash
# Pack the ZeroOS payload (payload.sh) into zeroos_<version>_<arch>.deb and
# write zeroos-update.manifest beside it. Called by image/package.sh and by
# the image build (01-run.sh), which installs the same package.
#
# Usage: package.sh <zeroshell-binary> <out-dir> <version>
# Env:   ZEROOS_ARCH (default arm64)
set -euo pipefail
umask 022
HERE=$(cd "$(dirname "$0")" && pwd)
BIN=$1 OUT=$2 VERSION=$3
ARCH=${ZEROOS_ARCH:-arm64}
# Installable apps (docs/APPS.md) depend on zeroos-app-abi-<n>: the app ABI
# of this ZeroShell (ZS_APP_ABI in shell/src/ui/app.h; zeroshell --app-abi).
# The image build has no shell/ next to it: image/build.sh writes app-abi.
if [ -n "${ZEROOS_APP_ABI:-}" ]; then ABI=$ZEROOS_APP_ABI
elif [ -f "$HERE/app-abi" ]; then ABI=$(cat "$HERE/app-abi")
else ABI=$(sed -n 's/^#define ZS_APP_ABI \([0-9]*\).*/\1/p' "$HERE/../../shell/src/ui/app.h"); fi
[[ $ABI =~ ^[0-9]+$ ]] || { echo "error: can't tell the app ABI (shell/src/ui/app.h)" >&2; exit 1; }

case "$VERSION" in
    [0-9]*) ;;
    *) echo "error: version '$VERSION' must start with a digit (see image/version.sh)" >&2; exit 1 ;;
esac
[ -f "$BIN" ] || { echo "error: zeroshell binary not found at $BIN" >&2; exit 1; }

STAGE=$(mktemp -d)
chmod 0755 "$STAGE"   # becomes "/" in the package; mktemp makes it 0700
trap 'rm -rf "$STAGE"' EXIT
"$HERE/payload.sh" "$HERE/files" "$STAGE" "$VERSION" "$BIN"

mkdir -p "$STAGE/DEBIAN"
cat > "$STAGE/DEBIAN/control" <<CONTROL
Package: zeroos
Version: $VERSION
Architecture: $ARCH
Maintainer: ZeroOS <noreply@github.com>
Section: misc
Priority: optional
Installed-Size: $(du -sk --exclude=DEBIAN "$STAGE" | cut -f1)
Provides: zeroos-app-abi-$ABI
Homepage: https://github.com/hartj77-blip/cardputer-zero
Description: ZeroOS for the Cardputer Zero
 ZeroShell (the home screen and apps), its services and system settings.
 Installed by the image and by zeroos-update.
CONTROL
# Files that another package also ships (/etc/default/gpsd, earlyoom) can't be
# in ours (or be dpkg conffiles); they travel under /usr/share/zeroos/overrides
# and are copied here. Like a conffile, a copy someone has edited is kept: we
# remember the checksum of what we last wrote, and only replace a file that
# still has it. The new version then goes next to it as <file>.zeroos-new.
cat > "$STAGE/DEBIAN/postinst" <<'POSTINST'
#!/bin/sh
set -e
ROOT=${ZEROOS_POSTINST_ROOT:-}   # tests only
SUMS=$ROOT/var/lib/zeroos/overrides.sums
mkdir -p "$(dirname "$SUMS")"
touch "$SUMS"
changed=""
cd "$ROOT/usr/share/zeroos/overrides"
for f in $(find . -type f | sed 's|^\./||' | sort); do
    dst=$ROOT/$f
    new=$(sha256sum < "$f" | cut -d' ' -f1)
    was=$(awk -v p="/$f" '$2 == p {print $1}' "$SUMS")
    have=""
    [ -f "$dst" ] && have=$(sha256sum < "$dst" | cut -d' ' -f1)
    if [ "$have" = "$new" ]; then
        rm -f "$dst.zeroos-new"
    elif [ -z "$have" ] || [ "$have" = "$was" ]; then
        install -D -m 0644 "$f" "$dst"
        rm -f "$dst.zeroos-new"
        changed="$changed /$f"
    elif [ -z "$was" ]; then
        # First time we track it (image build, or an update from an older
        # zeroos that always overwrote): keep their copy as a backup.
        cp -p "$dst" "$dst.zeroos-old"
        install -D -m 0644 "$f" "$dst"
        echo "zeroos: replaced /$f (the old one is /$f.zeroos-old)"
        changed="$changed /$f"
    else
        install -D -m 0644 "$f" "$dst.zeroos-new"
        echo "zeroos: kept your edited /$f; ZeroOS's new version is /$f.zeroos-new"
        continue
    fi
    { grep -v " /$f\$" "$SUMS" || true; echo "$new /$f"; } > "$SUMS.tmp"
    mv -f "$SUMS.tmp" "$SUMS"
done
if [ -n "$ROOT" ]; then echo "changed:$changed"; exit 0; fi
if [ -d /run/systemd/system ]; then
    systemctl daemon-reload || true
    udevadm control --reload || true
    # New settings take effect now, not at the next reboot.
    case $changed in *etc/default/earlyoom*) systemctl try-restart earlyoom.service || true ;; esac
fi
POSTINST
chmod 0755 "$STAGE/DEBIAN/postinst"
# The files people edit: an update leaves their copies alone.
printf '%s\n' /etc/zeroos/update.conf /etc/zeroos/apps.sources > "$STAGE/DEBIAN/conffiles"

mkdir -p "$OUT"
DEB="zeroos_${VERSION}_${ARCH}.deb"
dpkg-deb --root-owner-group -Zxz --build "$STAGE" "$OUT/$DEB" >/dev/null
{
    echo "version=$VERSION"
    echo "file=$DEB"
    echo "sha256=$(sha256sum "$OUT/$DEB" | cut -d' ' -f1)"
    echo "size=$(stat -c %s "$OUT/$DEB")"
} > "$OUT/zeroos-update.manifest"
echo "$OUT/$DEB"
