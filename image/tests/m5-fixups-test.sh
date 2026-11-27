#!/usr/bin/env bash
# Tests for m5-fixups (image/stage-zeroos/files/apps/m5-fixups): what ZeroOS
# does around a few of M5Stack's apps. No device: a folder stands in for /,
# and apt-get, apt-mark, dpkg-query, systemctl and swapoff are stand-ins.
#   image/tests/m5-fixups-test.sh
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
FIX=$ROOT/image/stage-zeroos/files/apps/m5-fixups
T=$(mktemp -d)
trap 'rm -rf -- "${T:?}"' EXIT
fails=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fails=$((fails + 1)); }

mkdir -p "$T/bin"
# dpkg-query: installed is what $T/installed lists, one package a line.
cat > "$T/bin/dpkg-query" <<STUB
#!/bin/sh
for a; do p=\$a; done
grep -qxF -- "\$p" "$T/installed" && echo "ii "
exit 0
STUB
# apt-get install <p>...: fails if $T/no-such lists one of them; else
# "installs" them, and game data puts its WAD where Debian's package does.
cat > "$T/bin/apt-get" <<STUB
#!/bin/sh
echo "apt-get \$*" >> "$T/log"
case " \$* " in
    *" install "*)
        for p; do case \$p in -*|*=*|install) continue ;; esac
            grep -qxF -- "\$p" "$T/no-such" 2>/dev/null && exit 100
        done
        for p; do case \$p in -*|*=*|install) continue ;; esac
            echo "\$p" >> "$T/installed"
            case \$p in freedoom|doom-wad-shareware) mkdir -p "$T/r/usr/share/games/doom"; : > "$T/r/usr/share/games/doom/\$p.wad" ;; esac
        done ;;
    *" autoremove "*)
        # What apt-mark made automatic goes.
        [ -f "$T/auto" ] && grep -vxFf "$T/auto" "$T/installed" > "$T/installed.new" && mv "$T/installed.new" "$T/installed"
        : > "$T/auto" ;;
esac
exit 0
STUB
cat > "$T/bin/apt-mark" <<STUB
#!/bin/sh
echo "apt-mark \$*" >> "$T/log"
[ "\$1" = auto ] && echo "\$2" >> "$T/auto"
exit 0
STUB
# The user ZeroShell runs as.
printf '#!/bin/sh\necho "pi:x:1000:1000:,,,:/home/pi:/bin/bash"\n' > "$T/bin/getent"
for c in systemctl swapoff; do
    printf '#!/bin/sh\necho "%s $*" >> "%s/log"\nexit 0\n' "$c" "$T" > "$T/bin/$c"
done
chmod +x "$T/bin"/*

fix() { PATH="$T/bin:$PATH" ZEROOS_FIXUPS_ROOT=$T/r ZEROOS_APPS_STATE=$T/state "$FIX" "$@" 2>>"$T/err"; }
reset() { rm -rf "$T/r" "$T/state" "$T/log" "$T/auto" "$T/no-such"; mkdir -p "$T/r/etc" "$T/state"; : > "$T/installed"; : > "$T/log"; }
logged() { grep -q -- "$1" "$T/log"; }

# --- DOOM Zero: a launcher with no game in it --------------------------------
reset
echo doom-zero-dev0 > "$T/installed"
fix installed doom-zero-dev0
logged 'apt-get install -y --no-install-recommends .* doom-wad-shareware$' && ! logged freedoom && ! logged 'apt-get update' &&
    ok "doom: installing it brings the shareware episode" || bad "doom: $(cat "$T/log")"
[ "$(cat "$T/state/m5-extras/doom-zero-dev0")" = doom-wad-shareware ] &&
    ok "doom: what it brought is written down" || bad "doom: extras note"
: > "$T/log"
fix installed doom-zero-dev0
! logged 'apt-get install' && ok "doom: an update installs nothing twice" || bad "doom: installed again"
fix removed doom-zero-dev0
logged 'apt-mark auto doom-wad-shareware' && [ ! -e "$T/state/m5-extras" ] &&
    ok "doom: removing it lets the game data go" || bad "doom: removal: $(cat "$T/log")"

# Game data that was already there: nothing is added, and nothing taken off.
reset
printf 'doom-zero-dev0\nfreedoom\n' > "$T/installed"
mkdir -p "$T/r/usr/share/games/doom"; : > "$T/r/usr/share/games/doom/freedoom1.wad"
fix installed doom-zero-dev0
fix removed doom-zero-dev0
[ ! -s "$T/log" ] && ok "doom: game data that was there before is enough, and stays" || bad "doom: $(cat "$T/log")"
# The same for WAD files someone copied to the launcher's own folder.
reset
echo doom-zero-dev0 > "$T/installed"
mkdir -p "$T/r/home/pi/.local/share/doom-zero-dev0/wads"; : > "$T/r/home/pi/.local/share/doom-zero-dev0/wads/DOOM2.WAD"
fix installed doom-zero-dev0
fix sweep --apt
[ ! -s "$T/log" ] && ok "doom: someone's own WAD files are enough" || bad "doom: $(cat "$T/log")"

# No non-free section in apt's sources: Freedoom, with the small engine in
# the same step (or apt picks dsda-doom for Freedoom's "some Doom engine").
reset
echo doom-zero-dev0 > "$T/installed"
echo doom-wad-shareware > "$T/no-such"
fix installed doom-zero-dev0
logged 'apt-get install -y --no-install-recommends .* freedoom chocolate-doom$' &&
    [ "$(grep -c 'apt-get update' "$T/log")" = 1 ] &&
    [ "$(tr '\n' ' ' < "$T/state/m5-extras/doom-zero-dev0")" = "freedoom chocolate-doom " ] &&
    ok "doom: without the shareware package (package lists fetched once to be sure), Freedoom and chocolate-doom in one step" || bad "doom: no shareware: $(cat "$T/log")"
fix removed doom-zero-dev0
logged 'apt-mark auto freedoom' && logged 'apt-mark auto chocolate-doom' && ok "doom: ...and both go with it" || bad "doom: fallback removal"
# Nothing to be had at all (offline): logged, not an error.
reset
echo doom-zero-dev0 > "$T/installed"
printf 'doom-wad-shareware\nfreedoom\n' > "$T/no-such"
fix installed doom-zero-dev0 && [ ! -e "$T/state/m5-extras/doom-zero-dev0" ] && grep -q 'copy WAD files' "$T/err" &&
    ok "doom: no game data to be had is logged, and fails nothing" || bad "doom: offline"

# Installed before ZeroOS knew to bring a game: the sweep catches up, when it may use apt.
reset
echo doom-zero-dev0 > "$T/installed"
fix sweep
! logged 'apt-get' && ok "doom: a sweep from a package script leaves apt alone" || bad "doom: apt from postinst"
fix sweep --apt
logged 'install .* doom-wad-shareware$' && ok "doom: sweep --apt brings the game to one installed earlier" || bad "doom: sweep"
: > "$T/log"
fix sweep --apt
! logged 'apt-get install' && ok "doom: ...once" || bad "doom: sweep installed again"
reset
fix sweep --apt
! logged 'apt-get install' && ok "doom: nothing for a device without it" || bad "doom: installed without the app"

# --- RaspyJack: what its install script leaves ---------------------------------
RJ_UNITS="raspyjack-device raspyjack-webui raspyjack-caddy-autoconfig raspyjack-provision"
leftovers() {   # the device after "apt-get purge raspyjack"
    local u
    mkdir -p "$T/r/etc/systemd/system/multi-user.target.wants" "$T/r/usr/local/sbin" "$T/r/var/lib/raspyjack" \
        "$T/r/etc/apt/sources.list.d" "$T/r/usr/share/keyrings" "$T/r/etc/kismet" "$T/r/etc/caddy/certs" "$T/r/var" \
        "$T/r/root" "$T/r/usr/share/APPLaunch/apps/raspyjack/loot"
    for u in $RJ_UNITS; do
        echo "[Service]" > "$T/r/etc/systemd/system/$u.service"
        ln -s "../$u.service" "$T/r/etc/systemd/system/multi-user.target.wants/$u.service"
    done
    echo "[Service]" > "$T/r/etc/systemd/system/zeroshell.service"
    : > "$T/r/usr/local/sbin/raspyjack-caddy-autoconfig.sh"
    : > "$T/r/var/swap.raspyjack"
    printf 'PARTUUID=1-02 / ext4 defaults 0 1\n/var/swap.raspyjack none swap sw 0 0\n/var/swap.mine none swap sw 0 0\n' > "$T/r/etc/fstab"
    : > "$T/r/var/lib/raspyjack/provision.done"
    echo "deb [signed-by=/usr/share/keyrings/kismet-archive-keyring.gpg] https://www.kismetwireless.net/repos/apt/release/trixie trixie main" \
        > "$T/r/etc/apt/sources.list.d/kismet.list"
    : > "$T/r/usr/share/keyrings/kismet-archive-keyring.gpg"
    : > "$T/r/etc/kismet/kismet_raspyjack.conf"
    : > "$T/r/etc/caddy/certs/raspyjack.crt"; : > "$T/r/etc/caddy/certs/raspyjack.key"
    : > "$T/r/usr/share/APPLaunch/apps/raspyjack/loot/found.txt"
    ln -s /usr/share/APPLaunch/apps/raspyjack "$T/r/root/Raspyjack"
    printf 'kismet\nkismet-core\ncaddy\n' > "$T/installed"
}
gone() { local f; for f in "$@"; do [ ! -e "$T/r/$f" ] && [ ! -L "$T/r/$f" ] || { echo "     still there: /$f"; return 1; }; done; }

# Removing it in Apps: its services stop before the package goes...
reset; leftovers
echo raspyjack >> "$T/installed"
: > "$T/r/usr/share/APPLaunch/apps/raspyjack/raspyjack.py"
fix removing raspyjack
logged 'systemctl disable --now raspyjack-provision.service' && logged 'systemctl disable --now raspyjack-webui.service' &&
    ok "raspyjack: its services are stopped before the package goes" || bad "raspyjack removing: $(cat "$T/log")"
# ...and while it is installed, nothing is cleaned up.
fix removed raspyjack
fix sweep --apt
[ -e "$T/r/var/swap.raspyjack" ] && [ -e "$T/r/etc/systemd/system/raspyjack-device.service" ] &&
    ok "raspyjack: nothing is touched while it is installed" || bad "raspyjack: cleaned up under an installed app"
# The package is gone (zeroos-apps purged it; apt takes Caddy with it later).
grep -vx raspyjack "$T/installed" > "$T/i" && mv "$T/i" "$T/installed"
rm "$T/r/usr/share/APPLaunch/apps/raspyjack/raspyjack.py"
: > "$T/log"
fix removed raspyjack
gone etc/systemd/system/raspyjack-device.service etc/systemd/system/raspyjack-webui.service \
    etc/systemd/system/raspyjack-caddy-autoconfig.service etc/systemd/system/raspyjack-provision.service \
    etc/systemd/system/multi-user.target.wants/raspyjack-device.service usr/local/sbin/raspyjack-caddy-autoconfig.sh &&
    logged 'systemctl disable --now raspyjack-device.service' && logged 'systemctl daemon-reload' &&
    ok "raspyjack: boot services gone" || bad "raspyjack: services left"
[ -e "$T/r/etc/systemd/system/zeroshell.service" ] && ok "raspyjack: other services untouched" || bad "raspyjack: removed another service"
gone var/swap.raspyjack && logged "swapoff $T/r/var/swap.raspyjack" && ! grep -q swap.raspyjack "$T/r/etc/fstab" &&
    [ "$(wc -l < "$T/r/etc/fstab")" = 2 ] && grep -q '^/var/swap.mine ' "$T/r/etc/fstab" &&
    ok "raspyjack: swap file and its fstab line gone, the rest of fstab kept" || bad "raspyjack: swap: $(cat "$T/r/etc/fstab")"
gone etc/apt/sources.list.d/kismet.list usr/share/keyrings/kismet-archive-keyring.gpg etc/kismet var/lib/raspyjack &&
    logged 'apt-mark auto kismet' && logged 'apt-get autoremove --purge -y' && ! grep -qx kismet "$T/installed" &&
    ok "raspyjack: Kismet's repository and package gone" || bad "raspyjack: kismet: $(cat "$T/log")"
[ -e "$T/r/etc/caddy/certs/raspyjack.crt" ] && ok "raspyjack: a Caddy that is still installed keeps its files" || bad "raspyjack: took Caddy's files"
[ -e "$T/r/usr/share/APPLaunch/apps/raspyjack/loot/found.txt" ] && [ -L "$T/r/root/Raspyjack" ] &&
    ok "raspyjack: what it collected is kept" || bad "raspyjack: deleted its data"
: > "$T/log"
fix sweep --apt
[ ! -s "$T/log" ] && ok "raspyjack: a second pass finds nothing to do" || bad "raspyjack: second pass: $(cat "$T/log")"

# Removed before this ZeroOS (the device in the report): the update's own
# sweep, from the package script, clears the boot services and the swap...
reset; leftovers
grep -vx caddy "$T/installed" > "$T/i" && mv "$T/i" "$T/installed"
rm -rf "$T/r/usr/share/APPLaunch/apps/raspyjack"   # nothing collected: dpkg removed the folder
fix sweep
gone etc/systemd/system/raspyjack-device.service etc/systemd/system/raspyjack-provision.service var/swap.raspyjack \
    etc/caddy root/Raspyjack && ! grep -q swap.raspyjack "$T/r/etc/fstab" &&
    ok "raspyjack: an update clears services, swap and dead links of one removed earlier" || bad "raspyjack: sweep"
! logged 'apt-get' && ! logged 'apt-mark' && [ -e "$T/r/etc/kismet/kismet_raspyjack.conf" ] &&
    [ -e "$T/r/etc/apt/sources.list.d/kismet.list" ] &&
    ok "raspyjack: ...without apt; Kismet waits" || bad "raspyjack: apt from a package script: $(cat "$T/log")"
# ...and "Update all" in Apps finishes it.
fix sweep --apt
gone etc/kismet etc/apt/sources.list.d/kismet.list && logged 'apt-mark auto kismet' && ! grep -qx kismet "$T/installed" &&
    ok "raspyjack: Update all takes Kismet off" || bad "raspyjack: sweep --apt: $(cat "$T/log")"

# A Kismet that RaspyJack didn't install (no settings file of its own) stays.
reset; leftovers
rm "$T/r/etc/kismet/kismet_raspyjack.conf"
fix sweep --apt
! logged 'apt-mark auto kismet' && [ -e "$T/r/etc/apt/sources.list.d/kismet.list" ] && grep -q '^kismet$' "$T/installed" &&
    ok "raspyjack: a Kismet that was there before stays" || bad "raspyjack: took off someone's Kismet"

# RaspyJack set up by hand (its own installer, in /root/Raspyjack): not ours.
reset; leftovers
rm "$T/r/root/Raspyjack"; mkdir -p "$T/r/root/Raspyjack"; : > "$T/r/root/Raspyjack/raspyjack.py"
fix sweep --apt
[ -e "$T/r/var/swap.raspyjack" ] && [ -e "$T/r/etc/systemd/system/raspyjack-device.service" ] && [ ! -s "$T/log" ] &&
    ok "raspyjack: a copy installed by hand is left alone" || bad "raspyjack: cleaned up a hand install"

# --- everything else ------------------------------------------------------------
reset
fix installed hello-zero && fix removing hello-zero && fix removed hello-zero && fix sweep && [ ! -s "$T/log" ] &&
    ok "other apps: nothing is done" || bad "other apps: $(cat "$T/log")"
fix 2>/dev/null && bad "usage: no error" || ok "usage: an unknown call is an error"
grep -q 'apps/m5-fixups"' "$ROOT/image/stage-zeroos/payload.sh" && grep -q 'm5-fixups sweep' "$ROOT/image/stage-zeroos/package.sh" &&
    ok "packaged, and run when ZeroOS updates" || bad "not in payload.sh or package.sh"

echo
[ "$fails" = 0 ] && echo "all passed" || { echo "$fails failed"; exit 1; }
