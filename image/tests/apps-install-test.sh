#!/usr/bin/env bash
# Install every app for real (docs/APPS.md): run as root in a throwaway
# Debian trixie arm64 system (CI uses a container), with Raspberry Pi's
# repository added as on the device. Serves the packages from a folder that
# stands in for a GitHub release and drives zeroos-apps with the real apt, so
# it catches what apps-test.sh's stand-ins can't: a Debian package that
# doesn't exist or won't install, a missing library, an app's own repository
# (Tailscale, Meshtastic) or extra package (M5Stack's camera program) that
# doesn't work, two apps that clash, and extras left behind on removal.
#
#   image/tests/apps-install-test.sh <apps-dir> <zeroshell> [app-id...]
#     <apps-dir>   zeroos-apps.index and the packages (image/apps/build.sh)
#     <zeroshell>  an arm64 ZeroShell (stands in for the zeroos package)
#
# Never run it on a computer you care about: it installs and removes
# packages system-wide.
set -uo pipefail
APPS=$(realpath "$1")
ZS=$(realpath "$2")
shift 2
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
[ "$(id -u)" = 0 ] || { echo "needs root, in a throwaway system" >&2; exit 1; }
[ -f /.dockerenv ] || [ "${ZEROOS_TEST_SYSTEM_IS_THROWAWAY:-}" = 1 ] ||
    { echo "refusing: not in a container (set ZEROOS_TEST_SYSTEM_IS_THROWAWAY=1 to override)" >&2; exit 1; }
export DEBIAN_FRONTEND=noninteractive
fails=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fails=$((fails + 1)); }

ABI=$("$ZS" --app-abi) || { echo "zeroshell --app-abi failed" >&2; exit 1; }

# --- a stand-in for the zeroos package: ZeroShell and the app ABI ------------
# Its version is newer than any release, so an app that needs a newer ZeroOS
# (Depends: zeroos (>= x.y.z)) installs against this tree's ZeroShell.
P=$(mktemp -d)
install -D -m 0755 "$ZS" "$P/usr/bin/zeroshell"
install -D -m 0755 "$ROOT/image/stage-zeroos/files/apps/zeroos-apps" "$P/usr/bin/zeroos-apps"
mkdir -p "$P/DEBIAN"
cat > "$P/DEBIAN/control" <<EOF
Package: zeroos
Version: 999.0.0-test
Architecture: $(dpkg --print-architecture)
Maintainer: test <noreply@github.com>
Provides: zeroos-app-abi-$ABI
Description: test stand-in for ZeroOS
EOF
dpkg-deb --root-owner-group --build "$P" /tmp/zeroos-test.deb >/dev/null
dpkg -i /tmp/zeroos-test.deb >/dev/null && ok "stand-in zeroos package (app ABI $ABI) installed" ||
    { bad "stand-in zeroos package"; exit 1; }

# --- the store, reading the folder as its one source -----------------------
mkdir -p /etc/zeroos
echo "file://$APPS" > /etc/zeroos/apps.sources
export ZEROOS_APPS_ALLOW_FILE=1
apt-get update -qq >/dev/null 2>&1
# Always on the device (Raspberry Pi OS; zeroos-apps needs curl); here an app's extras would pull them
# in, and apt then keeps them, so they would look left behind.
apt-get install -y -qq --no-install-recommends curl ca-certificates file dbus systemd systemd-sysv libpam-systemd dbus-user-session bsdextrautils libkmod2 sensible-utils >/dev/null 2>&1
zeroos-apps refresh 2>/dev/null && ok "catalog read" || bad "zeroos-apps refresh"
ids=${*:-$(awk -F= '$1 == "app" {print $2}' "$APPS/zeroos-apps.index")}
offered=$(awk -v RS= '{ split($0, l, "\n"); print substr(l[1], 5) }' /var/lib/zeroos/apps/catalog)
for id in $ids; do
    echo "$offered" | grep -qx "$id" || bad "$id is in the index but not offered (wrong ABI or a bad entry?)"
done

pkgs() { dpkg-query -W -f '${db:Status-Abbrev} ${Package}\n' | awk '$1 == "ii" {print $2}' | sort; }
before=$(pkgs)

# libraries a program or module needs that aren't there; modules use
# ZeroShell's own symbols, so only "not found" libraries count.
missing_libs() {
    local f
    for f in "$@"; do
        [ -f "$f" ] || continue
        file -b "$f" | grep -q ELF || continue
        ldd "$f" 2>/dev/null | awk -v f="$f" '/not found/ {print f ": " $1}'
    done
}

for id in $ids; do
    echo "--- $id"
    log=/tmp/install-$id.log
    pre=$(pkgs)
    if zeroos-apps install "$id" > "$log" 2>&1; then
        ok "$id installs with what it depends on"
    else
        bad "$id won't install"
        tail -25 "$log" | sed 's/^/     /'
        continue
    fi
    st=$(dpkg-query -W -f '${db:Status-Abbrev}' "zeroos-app-$id" 2>/dev/null)
    [ "${st% }" = ii ] && ok "$id is installed (dpkg)" || bad "$id: dpkg status '$st'"
    mods=$(dpkg -L "zeroos-app-$id" | grep '^/usr/lib/zeroos/apps/.*\.so$')
    [ -n "$mods" ] && ok "$id: module in /usr/lib/zeroos/apps" || bad "$id: no module"
    # Everything the app and the packages it pulled in put in bin folders.
    bins=$(dpkg -L "zeroos-app-$id" | grep -E '^/usr/s?bin/')
    new=$(comm -13 <(echo "$pre") <(pkgs))
    for pkg in $new; do bins+=$'\n'$(dpkg -L "$pkg" 2>/dev/null | grep -E '^/usr/s?bin/[^/]+$'); done
    # shellcheck disable=SC2086
    nf=$(missing_libs $mods $bins | sort -u)
    [ -z "$nf" ] && ok "$id: every library its programs need is there" ||
        { bad "$id: missing libraries"; echo "$nf" | sed 's/^/     /'; }
    grep -q "^state=done" /run/zeroos/apps.status && ok "$id: the Apps app sees done" ||
        { bad "$id: status"; sed 's/^/     /' /run/zeroos/apps.status; }
done

echo "--- all at once"
# Every app installed side by side (no two clash), then each removed again.
for id in $ids; do
    dpkg-query -W -f '${db:Status-Abbrev}' "zeroos-app-$id" 2>/dev/null | grep -q '^ii' || bad "$id not installed alongside the others"
done
zeroos-apps list 2>/dev/null | sed 's/^/     /'
for id in $ids; do
    if zeroos-apps remove "$id" > "/tmp/remove-$id.log" 2>&1 &&
       ! dpkg-query -W -f '${db:Status-Abbrev}\n' "zeroos-app-$id" 2>/dev/null | grep -q '^ii'; then
        ok "$id removes"
        # The apps still here keep their own extras (an autoremove once took
        # M5Stack's camera program whenever any other app was removed).
        for f in /var/lib/zeroos/apps/extras/*; do
            [ -e "$f" ] || continue
            dpkg-query -W -f '${db:Status-Abbrev}' "$(cat "$f")" 2>/dev/null | grep -q '^ii' ||
                bad "removing $id took $(basename "$f")'s extra $(cat "$f")"
        done
    else
        bad "$id won't remove"; tail -15 "/tmp/remove-$id.log" | sed 's/^/     /'
    fi
done
left=$(comm -13 <(echo "$before") <(pkgs))
[ -z "$left" ] && ok "removing every app takes its extras away" ||
    { bad "left behind after removing every app:"; echo "$left" | sed 's/^/     /'; }
leftfiles=$(find /usr/lib/zeroos/apps -name '*.so' 2>/dev/null)
[ -z "$leftfiles" ] && ok "no modules left" || bad "modules left: $leftfiles"

echo "--- after an update from an older ZeroOS"
# A device that had apps installed, then took a ZeroOS update that changed
# the app ABI (0.1.x to 0.2.0 did): the old builds depend on an ABI nothing
# provides any more, and apt refuses every install while they are there.
# Stand-ins for the old ZeroOS and two old apps, then the update, then the
# real zeroos-apps and the real apt.
# shellcheck disable=SC2086
set -- $ids
one=$1 two=${2:-}
old_pkg() {   # old_pkg <package> <control lines...>: build and install a stand-in
    local d name=$1
    d=$(mktemp -d); shift
    mkdir -p "$d/DEBIAN"
    { printf 'Package: %s\nVersion: 0.0.0-old\nArchitecture: all\nMaintainer: test <noreply@github.com>\n' "$name"
      printf '%s\n' "$@" "Description: test stand-in"; } > "$d/DEBIAN/control"
    dpkg-deb --root-owner-group --build "$d" "/tmp/$name-old.deb" >/dev/null && dpkg -i "/tmp/$name-old.deb" >/dev/null 2>&1
}
dpkg --purge zeroos >/dev/null 2>&1
old_pkg zeroos "Provides: zeroos-app-abi-0" || bad "stand-in old zeroos"
for id in $one $two; do old_pkg "zeroos-app-$id" "Depends: zeroos, zeroos-app-abi-0" || bad "stand-in old $id"; done
dpkg -i /tmp/zeroos-test.deb >/dev/null 2>&1 && ok "ZeroOS updated over old apps" || bad "update over old apps"
rm -f /var/lib/zeroos/apps/restore
if zeroos-apps install "$one" > /tmp/upgrade-install.log 2>&1; then
    ok "after the update: $one installs"
else
    bad "after the update: $one won't install"; tail -25 /tmp/upgrade-install.log | sed 's/^/     /'
fi
if [ -n "$two" ]; then
    zeroos-apps upgrade > /tmp/upgrade-all.log 2>&1 || true
    [ "$(dpkg-query -W -f '${db:Status-Abbrev}${Version}' "zeroos-app-$two" 2>/dev/null)" != "ii 0.0.0-old" ] &&
        dpkg-query -W -f '${db:Status-Abbrev}' "zeroos-app-$two" 2>/dev/null | grep -q '^ii' &&
        ok "after the update: update-all brings $two back" ||
        { bad "after the update: update-all did not bring $two back"; tail -25 /tmp/upgrade-all.log | sed 's/^/     /'; }
fi
for id in $one $two; do zeroos-apps remove "$id" >/dev/null 2>&1 || true; done

if [ "$fails" -gt 0 ]; then echo "apps-install-test: $fails failed"; exit 1; fi
echo "apps-install-test: all passed"
