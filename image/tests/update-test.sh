#!/usr/bin/env bash
# Tests for the update path, with no device: builds a real package from the
# payload, then drives zeroos-update against a folder standing in for the
# update server. dpkg -i and systemctl are stand-ins (stubs on PATH), so
# nothing on this machine is installed or restarted.
#   image/tests/update-test.sh
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
ARCH=$(dpkg --print-architecture)
fails=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fails=$((fails + 1)); }
expect() {   # expect <name> <key> <value>: a line of the status file
    if grep -qx "$2=$3" "$T/run/update.status"; then ok "$1"; else bad "$1 (wanted $2=$3)"; sed 's/^/     /' "$T/run/update.status"; fi
}

# --- the package ---------------------------------------------------------
printf '#!/bin/sh\necho fake zeroshell\n' > "$T/zeroshell"
chmod +x "$T/zeroshell"
build() { ZEROOS_ARCH=$ARCH "$ROOT/image/stage-zeroos/package.sh" "$T/zeroshell" "$T/server-$1" "$1" >/dev/null; }
build 1.0.0; build 1.1.0
DEB=$T/server-1.1.0/zeroos_1.1.0_$ARCH.deb
[ "$(dpkg-deb -f "$DEB" Package)" = zeroos ] && ok "package is called zeroos" || bad "package name"
dpkg-deb -x "$DEB" "$T/x"
for f in usr/bin/zeroshell usr/bin/zeroos-update etc/systemd/system/zeroshell.service \
         etc/systemd/system/zeroos-update.service etc/zeroos/update.conf etc/zeroos-release \
         usr/share/zeroos/overrides/etc/default/earlyoom usr/bin/zeroos-apps \
         etc/systemd/system/zeroos-apps@.service etc/zeroos/apps.sources; do
    [ -e "$T/x/$f" ] && ok "package has /$f" || bad "package is missing /$f"
done
# Files other Debian packages own must not be in ours (dpkg would refuse).
for f in etc/default/gpsd etc/default/earlyoom; do
    [ ! -e "$T/x/$f" ] && ok "package leaves /$f to its owner" || bad "package ships /$f"
done
grep -q '^ZeroOS 1.1.0 ' "$T/x/etc/zeroos-release" && ok "release file names the version" || bad "release file"
# Every file in files/ is in the payload, or in an installable app (image/apps).
missing=$(cd "$ROOT/image/stage-zeroos/files" && find . -type f ! -name 'initramfs-*' | while read -r f; do
    grep -q "$(basename "$f")" "$ROOT/image/stage-zeroos/payload.sh" "$ROOT"/image/apps/*.pkg || echo "$f"; done)
[ -z "$missing" ] && ok "every file in files/ is in the payload or an app" || bad "not in payload.sh or an app: $missing"
# Installable apps depend on the app ABI this package provides.
abi=$(sed -n 's/^#define ZS_APP_ABI \([0-9]*\).*/\1/p' "$ROOT/shell/src/ui/app.h")
dpkg-deb -f "$DEB" Provides | grep -qx "zeroos-app-abi-$abi" && ok "package provides zeroos-app-abi-$abi" || bad "Provides"
grep -qx /etc/zeroos/apps.sources <(dpkg-deb --ctrl-tarfile "$DEB" | tar -xO ./conffiles) &&
    ok "apps.sources is kept when edited" || bad "apps.sources is not a conffile"

# --- postinst: /etc/default overrides, kept when edited ------------------
R=$T/root
mkdir -p "$R/usr/share/zeroos" "$R/etc/default" "$T/ctl"
cp -r "$T/x/usr/share/zeroos/overrides" "$R/usr/share/zeroos/"
dpkg-deb -e "$DEB" "$T/ctl"
postinst() { ZEROOS_POSTINST_ROOT=$R sh "$T/ctl/postinst"; }
# A second override (gpsd's moved to the GPS app), to test one file left alone.
echo "ours" > "$R/usr/share/zeroos/overrides/etc/default/zz-test"
echo "theirs" > "$R/etc/default/earlyoom"
postinst > "$T/pi.out"
cmp -s "$R/etc/default/earlyoom" "$R/usr/share/zeroos/overrides/etc/default/earlyoom" && ok "postinst: first run installs ours" || bad "postinst: first run"
grep -qx theirs "$R/etc/default/earlyoom.zeroos-old" && ok "postinst: first run backs up the old one" || bad "postinst: no backup"
[ -f "$R/etc/default/zz-test" ] && ok "postinst: missing file installed" || bad "postinst: missing file"
postinst > "$T/pi.out"
grep -qx "changed:" "$T/pi.out" && ok "postinst: unchanged run changes nothing" || bad "postinst: unchanged run: $(cat "$T/pi.out")"
echo "# mine" >> "$R/etc/default/earlyoom"
echo "# new" >> "$R/usr/share/zeroos/overrides/etc/default/earlyoom"
echo "# new" >> "$R/usr/share/zeroos/overrides/etc/default/zz-test"
postinst > "$T/pi.out"
tail -1 "$R/etc/default/earlyoom" | grep -qx "# mine" && ok "postinst: edited file kept" || bad "postinst: edited file overwritten"
tail -1 "$R/etc/default/earlyoom.zeroos-new" | grep -qx "# new" && ok "postinst: new version alongside" || bad "postinst: no .zeroos-new"
tail -1 "$R/etc/default/zz-test" | grep -qx "# new" && ok "postinst: unedited file updated" || bad "postinst: unedited file not updated"
# Terminal, Files and Dock became apps: an update brings them back (once);
# a new install (the image) preinstalls them instead.
A=$R/var/lib/zeroos/apps
ZEROOS_POSTINST_ROOT=$R sh "$T/ctl/postinst" configure > /dev/null
[ ! -e "$A/restore" ] && [ -e "$A/.restored-builtins" ] && ok "postinst, new install: nothing to restore" || bad "postinst, new install: restore list"
rm -f "$A/.restored-builtins"
ZEROOS_POSTINST_ROOT=$R sh "$T/ctl/postinst" configure 0.1.0 > /dev/null
grep -qx "terminal files dock" "$A/restore" 2>/dev/null && ok "postinst, update: lists the former built-in apps" || bad "postinst, update: no restore list"
rm -f "$A/restore"
ZEROOS_POSTINST_ROOT=$R sh "$T/ctl/postinst" configure 0.2.0 > /dev/null
[ ! -e "$A/restore" ] && ok "postinst, next update: not again" || bad "postinst, next update: listed them again"

# --- the Terminal's console goes on the LCD ---------------------------------
# Whichever framebuffer the LCD is (fb1 with a monitor at boot), and with or
# without con2fbmap on the card.
C=$ROOT/image/stage-zeroos/files/remote/console-on-lcd
FB=$T/fbroot
mkdir -p "$FB/dev" "$FB/sys/class/graphics/fb0/device/of_node" "$FB/sys/class/graphics/fb1/device/of_node" "$T/fbbin"
printf 'brcm,bcm2835-hdmi\0' > "$FB/sys/class/graphics/fb0/device/of_node/compatible"
printf 'sitronix,st7789v\0panel-mipi-dbi-spi\0' > "$FB/sys/class/graphics/fb1/device/of_node/compatible"
printf '#!/bin/sh\necho "$*" >> "%s/con2fb.log"\n' "$T" > "$T/fbbin/con2fbmap"
chmod +x "$T/fbbin/con2fbmap"
ln -s fb1 "$FB/dev/fb_lcd"
PATH="$T/fbbin:$PATH" ZEROOS_FB_ROOT=$FB sh "$C" 3
grep -qx "3 1" "$T/con2fb.log" && ok "console-on-lcd: follows /dev/fb_lcd" || bad "console-on-lcd: $(cat "$T/con2fb.log" 2>&1)"
rm -f "$FB/dev/fb_lcd" "$T/con2fb.log"
PATH="$T/fbbin:$PATH" ZEROOS_FB_ROOT=$FB sh "$C" 3 x 1
[ "$(tr '\n' ',' < "$T/con2fb.log")" = "3 1,1 1," ] && ok "console-on-lcd: finds the panel with no /dev/fb_lcd" || bad "console-on-lcd, no link: $(cat "$T/con2fb.log" 2>&1)"
mkdir -p "$FB/sys/class/vtconsole/vtcon0" "$FB/sys/class/vtconsole/vtcon1"
echo "(S) dummy device" > "$FB/sys/class/vtconsole/vtcon0/name"; echo 1 > "$FB/sys/class/vtconsole/vtcon0/bind"
echo "(M) frame buffer device" > "$FB/sys/class/vtconsole/vtcon1/name"; echo 0 > "$FB/sys/class/vtconsole/vtcon1/bind"
PATH="$T/fbbin:$PATH" ZEROOS_FB_ROOT=$FB sh "$C" 3
[ "$(cat "$FB/sys/class/vtconsole/vtcon1/bind")" = 1 ] && [ "$(cat "$FB/sys/class/vtconsole/vtcon0/bind")" = 1 ] &&
    ok "console-on-lcd: binds the framebuffer console" || bad "console-on-lcd: framebuffer console not bound"
rm -rf "$FB/sys/class/graphics/fb1" "$FB/sys/class/vtconsole" "$T/con2fb.log"
PATH="$T/fbbin:$PATH" ZEROOS_FB_ROOT=$FB sh "$C" 3 2>/dev/null
[ $? = 0 ] && [ ! -e "$T/con2fb.log" ] && ok "console-on-lcd: no LCD found is not an error" || bad "console-on-lcd: no LCD"
grep -q '^ExecStartPre=-+/usr/lib/zeroos/console-on-lcd 3$' "$ROOT/image/stage-zeroos/files/remote/zeroos-terminal.service" &&
    [ -x "$T/x/usr/lib/zeroos/console-on-lcd" ] &&
    ok "console-on-lcd: in the package and run before the Terminal" || bad "console-on-lcd: not packaged or not run"

# --- the updater ---------------------------------------------------------
mkdir -p "$T/bin" "$T/run" "$T/state"
# dpkg: "install" = note the version (or fail for the version in
# $T/bad-install); --configure -a does nothing; the rest is the real dpkg.
cat > "$T/bin/dpkg" <<STUB
#!/bin/sh
case " \$* " in *" --configure "*) exit 0 ;; esac
case " \$* " in *" -i "*)
    for deb; do :; done
    v=\$(dpkg-deb -f "\$deb" Version)
    grep -qsx "\$v" "$T/bad-install" && exit 1
    echo "ZeroOS \$v (test)" > "$T/release"
    echo "\$v" >> "$T/installed.log"
    exit 0 ;;
esac
exec /usr/bin/dpkg "\$@"
STUB
# systemctl: ZeroShell stays up unless the version in $T/broken is installed.
cat > "$T/bin/systemctl" <<STUB
#!/bin/sh
case "\$*" in
    *is-enabled*|*restart*) exit 0 ;;
    *MainPID*) if grep -qsx "\$(awk '{print \$2}' "$T/release")" "$T/broken"; then date +%N; else echo 4242; fi ;;
    *is-active*) ! grep -qsx "\$(awk '{print \$2}' "$T/release")" "$T/broken" ;;
esac
STUB
cat > "$T/bin/id" <<'STUB'
#!/bin/sh
[ "$1" = -u ] && { echo 0; exit 0; }
exec /usr/bin/id "$@"
STUB
chmod +x "$T/bin"/*
update() {
    PATH="$T/bin:$PATH" ZEROOS_UPDATE_CONF=$T/update.conf ZEROOS_UPDATE_STATE=$T/state ZEROOS_UPDATE_RUN=$T/run \
        ZEROOS_RELEASE=$T/release ZEROOS_UPDATE_SETTLE=0 \
        "$ROOT/image/stage-zeroos/files/update/zeroos-update" "$@" 2>>"$T/log"
}
serve() { echo "UPDATE_URL=file://$T/server-$1" > "$T/update.conf"; }
echo "ZeroOS 1.0.0 (test)" > "$T/release"
cp "$T/server-1.0.0/zeroos_1.0.0_$ARCH.deb" "$T/state/current.deb"

serve 1.0.0; update check
expect "same version: up to date" state up-to-date

serve 1.1.0; update check
expect "newer version: available" state available
expect "newer version: named" latest 1.1.0

echo "UPDATE_URL=file://$T/nowhere" > "$T/update.conf"; update check || true
expect "no server: offline" state offline

# Plain http only to your own network (the manifest's checksum proves nothing
# about who sent it).
echo "UPDATE_URL=http://updates.example.com" > "$T/update.conf"; update check || true
expect "http to the internet: refused" state failed
echo "UPDATE_URL=http://192.168.1.20:8000" > "$T/update.conf"; update check || true
expect "http on the LAN: allowed" state offline
printf 'UPDATE_URL=http://updates.example.com\nALLOW_HTTP=1\n' > "$T/update.conf"; update check || true
expect "http with ALLOW_HTTP=1: allowed" state offline

serve 1.1.0
cp "$T/server-1.1.0/zeroos-update.manifest" "$T/manifest.good"
sed -i 's/^sha256=.*/sha256=0000000000000000000000000000000000000000000000000000000000000000/' "$T/server-1.1.0/zeroos-update.manifest"
update install || true
expect "wrong checksum: refused" state failed
grep -q '^ZeroOS 1.0.0' "$T/release" && ok "wrong checksum: nothing installed" || bad "wrong checksum installed anyway"
printf 'version=1.1.0; rm -rf /\nfile=../../etc/passwd\nsha256=x\nsize=1\n' > "$T/server-1.1.0/zeroos-update.manifest"
update check || true
expect "odd manifest: refused" state failed
cp "$T/manifest.good" "$T/server-1.1.0/zeroos-update.manifest"

update install
expect "install: done" state done
expect "install: now on 1.1.0" current 1.1.0
cmp -s "$T/state/previous.deb" "$T/server-1.0.0/zeroos_1.0.0_$ARCH.deb" && ok "install: old package kept" || bad "old package not kept"

update rollback
expect "rollback: done" state done
grep -q '^ZeroOS 1.0.0' "$T/release" && ok "rollback: back on 1.0.0" || bad "rollback did not go back"

# A version whose ZeroShell keeps crashing is taken off again.
build 1.2.0; serve 1.2.0; echo 1.2.0 > "$T/broken"
update install || true
expect "crashing version: rolled back" state rolled-back
grep -q '^ZeroOS 1.0.0' "$T/release" && ok "crashing version: old one is back" || bad "crashing version left installed"
rm "$T/broken"

# A package dpkg won't install: the old one stays, and the message says so.
build 1.3.0; serve 1.3.0; echo 1.3.0 > "$T/bad-install"
update install || true
expect "install fails: failed" state failed
grep -q '^message=Could not install 1.3.0. 1.0.0 is still on' "$T/run/update.status" &&
    ok "install fails: says the old version is still on" || bad "install fails: message"
echo "1.3.0
1.0.0" > "$T/bad-install"
update install || true
grep -q 'putting 1.0.0 back failed too' "$T/run/update.status" &&
    ok "install and put-back fail: says so" || bad "install and put-back fail: message"
rm "$T/bad-install"

# A package pushed from a PC, and one that is not ours.
update install-file "$T/server-1.2.0/zeroos_1.2.0_$ARCH.deb"
expect "install-file: done" current 1.2.0
mkdir -p "$T/other/DEBIAN"
printf 'Package: other\nVersion: 9\nArchitecture: all\nMaintainer: x\nDescription: x\n' > "$T/other/DEBIAN/control"
dpkg-deb --build "$T/other" "$T/other.deb" >/dev/null
update install-file "$T/other.deb" || true
expect "foreign package: refused" state failed

# --- versions ------------------------------------------------------------
dpkg --compare-versions 0.1.0 gt "0.0.0+30.gcb85d31" && ok "a release is newer than an untagged build" || bad "version order"
dpkg --compare-versions "0.1.0+3.gabc1234" gt 0.1.0 && ok "a build after a release is newer than it" || bad "version order"
v=$("$ROOT/image/version.sh")
[[ $v =~ ^[0-9][0-9A-Za-z.+~-]*$ ]] && ok "version.sh gives a dpkg version ($v)" || bad "version.sh: $v"

[ "$fails" = 0 ] && echo "all passed" || { echo "$fails failed"; echo "--- log"; cat "$T/log"; exit 1; }
