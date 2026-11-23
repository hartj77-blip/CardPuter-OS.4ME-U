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
Homepage: https://github.com/hartj77-blip/CardPuter-OS.4ME-U
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
# Terminal, Files and Dock were built into ZeroShell before; now they are
# apps (docs/APPS.md). A device updated from such a version gets them back:
# the Apps app's update-all (U) installs what this list names, once.
case "${1:-}:${2:-}" in
    configure:?*)
        if [ ! -e "$ROOT/var/lib/zeroos/apps/.restored-builtins" ]; then
            mkdir -p "$ROOT/var/lib/zeroos/apps"
            echo "terminal files dock" > "$ROOT/var/lib/zeroos/apps/restore"
            touch "$ROOT/var/lib/zeroos/apps/.restored-builtins"
        fi ;;
    configure:)
        # A new install (the image): its apps are preinstalled instead.
        mkdir -p "$ROOT/var/lib/zeroos/apps"
        touch "$ROOT/var/lib/zeroos/apps/.restored-builtins" ;;
esac
# The installable apps moved to their own repository (docs/APPS.md), which
# goes first in the list of sources. An update keeps a list that was edited
# on the device, so add the line here, once: taking it out later sticks.
S=$ROOT/etc/zeroos/apps.sources
if [ ! -e "$ROOT/var/lib/zeroos/apps/.apps-repo-added" ]; then
    NEW=hartj77-blip/CardPuter-Apps.4ME-U
    if [ -f "$S" ] && ! sed -e 's/#.*//' -e 's/[[:space:]]//g' "$S" | grep -qixF "$NEW"; then
        awk -v new="$NEW" '
            !done && !/^[[:space:]]*(#|$)/ { print new; done = 1 }
            { print }
            END { if (!done) print new }' "$S" > "$S.zeroos-tmp" &&
            cat "$S.zeroos-tmp" > "$S"
        rm -f "$S.zeroos-tmp"
        echo "zeroos: added $NEW to /etc/zeroos/apps.sources"
    fi
    mkdir -p "$ROOT/var/lib/zeroos/apps"
    touch "$ROOT/var/lib/zeroos/apps/.apps-repo-added"
fi
# M5Stack's app store (docs/APPS.md) goes last in the list, once, the same way.
if [ ! -e "$ROOT/var/lib/zeroos/apps/.m5-store-added" ]; then
    if [ -f "$S" ] && ! sed -e 's/#.*//' -e 's/[[:space:]]//g' "$S" |
            grep -qixE 'm5stack|https://cardputerzero\.github\.io/generated/registry\.json'; then
        printf '%s\n' "# M5Stack's app store, last: community apps made for M5's own launcher." m5stack >> "$S"
        echo "zeroos: added M5Stack's app store to /etc/zeroos/apps.sources"
    fi
    mkdir -p "$ROOT/var/lib/zeroos/apps"
    touch "$ROOT/var/lib/zeroos/apps/.m5-store-added"
fi
# BlueZ options for Bluetooth LE gamepads (Xbox): edited in place, since
# main.conf belongs to the bluez package.
bt_changed=""
if [ -x "$ROOT/usr/lib/zeroos/bluetooth-conf" ]; then
    bt_changed=$("$ROOT/usr/lib/zeroos/bluetooth-conf" "$ROOT/etc/bluetooth/main.conf" 2>/dev/null || true)
fi
[ -n "$bt_changed" ] && changed="$changed /etc/bluetooth/main.conf"
if [ -n "$ROOT" ]; then echo "changed:$changed"; exit 0; fi
if [ -d /run/systemd/system ]; then
    systemctl daemon-reload || true
    udevadm control --reload || true
    # New device permissions (Bluetooth's rfkill) apply now, not at the next reboot.
    udevadm trigger --subsystem-match=misc --sysname-match=rfkill || true
    # Memory tuning (docs/PERFORMANCE.md) applies now, not at the next reboot.
    sysctl -q -p /etc/sysctl.d/90-zeroos-memory.conf || true
    systemd-tmpfiles --create /etc/tmpfiles.d/zeroos-memory.conf 2>/dev/null || true
    # New settings take effect now, not at the next reboot.
    case $changed in *etc/default/earlyoom*) systemctl try-restart earlyoom.service || true ;; esac
    # Once, when the options were first set: connected Bluetooth devices
    # reconnect a moment later.
    case $changed in *etc/bluetooth/main.conf*) systemctl try-restart bluetooth.service || true ;; esac
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
