#!/usr/bin/env bash
# Tests for installable apps (docs/APPS.md), with no device: builds real app
# packages with image/apps/make-app-deb.sh, serves them from folders that
# stand in for GitHub releases, and drives zeroos-apps. apt-get, apt-mark,
# dpkg-query and systemctl are stand-ins (stubs on PATH), so nothing on this
# machine is installed.
#   image/tests/apps-test.sh
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
T=$(mktemp -d)
trap 'rm -rf -- "${T:?}"' EXIT
fails=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fails=$((fails + 1)); }
expect() {   # expect <name> <key> <value>: a line of the status file
    if grep -qx "$2=$3" "$T/run/apps.status"; then ok "$1"; else bad "$1 (wanted $2=$3)"; sed 's/^/     /' "$T/run/apps.status"; fi
}
ABI=$(sed -n 's/^#define ZS_APP_ABI \([0-9]*\).*/\1/p' "$ROOT/shell/src/ui/app.h")

# --- packages --------------------------------------------------------------
mkdir -p "$T/mods" "$T/pkgs" "$T/a" "$T/b" "$T/keys"
echo "not really a module" > "$T/mods/notes.so"
echo "not really a module" > "$T/mods/gps.so"
echo key > "$T/keys/tailscale.gpg"
cat > "$T/pkgs/notes.pkg" <<'EOF'
NAME="Notes"
SUMMARY="Quick text notes"
EOF
cat > "$T/pkgs/gps.pkg" <<'EOF'
NAME="GPS"
SUMMARY="Where you are"
DEPENDS="gpsd (>= 3), gpsd-clients"
ENABLE="gpsd.socket"
payload() {
    install -D -m 0644 /dev/null "$D/usr/share/zeroos/app-overrides/gps/etc/default/gpsd"
    install -D -m 0644 "$KEYS/tailscale.gpg" "$D/usr/share/keyrings/zeroos-app-gps.gpg"
    install -d "$D/etc/apt/sources.list.d"
    echo "deb [signed-by=/usr/share/keyrings/zeroos-app-gps.gpg] https://example.invalid trixie main" \
        > "$D/etc/apt/sources.list.d/zeroos-app-gps.list"
}
EOF
mk() {   # mk <server> <id> <version>: build into a server folder, add to its index
    KEYS=$T/keys ZEROOS_APP_ABI=$ABI ZEROOS_ARCH=arm64 \
        "$ROOT/image/apps/make-app-deb.sh" "$T/pkgs/$2.pkg" "$T/$1" "$3" "$T/mods/$2.so" >> "$T/$1/zeroos-apps.index"
}
mk a notes 1.0.0
mk a gps 1.0.0
DEB=$T/a/zeroos-app-gps_1.0.0_arm64.deb
[ "$(dpkg-deb -f "$DEB" Package)" = zeroos-app-gps ] && ok "package is zeroos-app-<id>" || bad "package name"
dpkg-deb -f "$DEB" Depends | grep -q "^zeroos, zeroos-app-abi-$ABI, gpsd (>= 3), gpsd-clients$" &&
    ok "package depends on ZeroOS's app ABI and its extras" || bad "Depends: $(dpkg-deb -f "$DEB" Depends)"
dpkg-deb -c "$DEB" | grep -q './usr/lib/zeroos/apps/gps.so$' && ok "package holds the module" || bad "no module"
dpkg-deb -e "$DEB" "$T/ctl"
sh -n "$T/ctl/postinst" && sh -n "$T/ctl/prerm" && ok "maintainer scripts parse" || bad "maintainer scripts"
grep -q 'enable --now "$u"' "$T/ctl/postinst" && grep -q 'for u in gpsd.socket' "$T/ctl/postinst" &&
    ok "postinst enables the app's units" || bad "postinst units"
grep -qx 'needs=gpsd, gpsd-clients' "$T/a/zeroos-apps.index" && ok "index names the extras, without versions" || bad "needs="

# Server b: a newer notes (but b is the second source: a's wins), and
# entries that must be dropped.
mk b notes 2.0.0
cat >> "$T/b/zeroos-apps.index" <<EOF
app=evil
name=Bad checksum
version=1.0
abi=$ABI
file=zeroos-app-evil_1.0_arm64.deb
sha256=nothex
size=1

app=future
name=Built for another ZeroShell
version=1.0
abi=$((ABI + 1))
file=zeroos-app-future_1.0_arm64.deb
sha256=$(printf '0%.0s' {1..64})
size=1

app=pc
name=For a PC
version=1.0
abi=$ABI
file=zeroos-app-pc_1.0_amd64.deb
sha256=$(printf '0%.0s' {1..64})
size=1

app=sneaky
name=File for another app
version=1.0
abi=$ABI
file=zeroos-app-notes_1.0_arm64.deb
sha256=$(printf '0%.0s' {1..64})
size=1
EOF

# --- stubs -----------------------------------------------------------------
mkdir -p "$T/bin" "$T/run" "$T/state" "$T/cache" "$T/root"
cat > "$T/bin/id" <<'STUB'
#!/bin/sh
[ "$1" = -u ] && { echo 0; exit 0; }
exec /usr/bin/id "$@"
STUB
cat > "$T/bin/dpkg" <<'STUB'
#!/bin/sh
case " $* " in *" --print-architecture "*) echo arm64; exit 0 ;; esac
# --configure -a fails while $BAD_CONFIGURE exists; "apt-get -f install" mends it.
case " $* " in *" --configure "*) [ -e "$BAD_CONFIGURE" ] && exit 1; exit 0 ;; esac
case " $* " in *" --remove "*) echo "dpkg $*" >> "$APT_LOG"; exit 0 ;; esac
exec /usr/bin/dpkg "$@"
STUB
# apt-get: log; "install" fails while $T/apt-fail exists.
cat > "$T/bin/apt-get" <<STUB
#!/bin/sh
echo "apt-get \$*" >> "$T/apt.log"
# -s (a dry run): would take off the package named in $T/apt-remv, if any.
case " \$* " in *" -s "*) [ -e "$T/apt-remv" ] && echo "Remv \$(cat "$T/apt-remv") [1.0]"; exit 0 ;; esac
case " \$* " in *" -f install "*) rm -f "$T/bad-configure"; exit 0 ;; esac
case " \$* " in *" install "*) [ -e "$T/apt-fail" ] && exit 100 ;; esac
exit 0
STUB
cat > "$T/bin/apt-mark" <<STUB
#!/bin/sh
echo "apt-mark \$*" >> "$T/apt.log"
STUB
# dpkg-query: what $T/installed lists ("<package> <version>" lines; a third
# word stands for the package's Depends).
cat > "$T/bin/dpkg-query" <<STUB
#!/bin/sh
# -L <package>: an M5Stack app's files (its launcher entry).
[ "\$1" = -L ] && { echo /usr/share/APPLaunch; echo "/usr/share/APPLaunch/applications/\$2.desktop"; exit 0; }
for a; do pat=\$a; done
case \$pat in *'*') pat=\${pat%\\*} ;; esac
while read -r p v; do
    case \$p in "\$pat"*) case "\$*" in *Package*) echo "ii \$p \$v" ;; *) echo "ii \$v" ;; esac ;; esac
done < "$T/installed"
STUB
printf '#!/bin/sh\necho %s\n' "$ABI" > "$T/zeroshell"
chmod +x "$T/bin"/* "$T/zeroshell"
: > "$T/installed"
printf '# test\nfile://%s/a\nfile://%s/b\n' "$T" "$T" > "$T/sources"

apps() {
    PATH="$T/bin:$PATH" APT_LOG=$T/apt.log BAD_CONFIGURE=$T/bad-configure ZEROOS_APPS_SOURCES=$T/sources ZEROOS_APPS_STATE=$T/state ZEROOS_APPS_CACHE=$T/cache \
        ZEROOS_APPS_RUN=$T/run ZEROOS_APPS_ZEROSHELL=$T/zeroshell ZEROOS_APPS_ROOT=$T/root \
        ZEROOS_APPS_ALLOW_FILE=1 ZEROOS_APPS_ARCH=arm64 \
        ZEROOS_APPS_M5_REGISTRY=$ROOT/image/stage-zeroos/files/apps/m5-registry \
        "$ROOT/image/stage-zeroos/files/apps/zeroos-apps" "$@" 2>>"$T/log"
}
cat_has() { grep -qx "$1" "$T/state/catalog"; }

# --- refresh -----------------------------------------------------------------
apps run refresh
expect "refresh: done" state done
cat_has app=notes && cat_has app=gps && ok "refresh: apps listed" || bad "refresh: apps missing"
[ "$(grep -c '^app=notes$' "$T/state/catalog")" = 1 ] && cat_has version=1.0.0 && ! cat_has version=2.0.0 &&
    ok "refresh: the first source wins" || bad "refresh: duplicate handling"
cat_has "source=file://$T/a" && ok "refresh: notes where it came from" || bad "refresh: no source="
for x in evil future pc sneaky; do
    cat_has "app=$x" && bad "refresh: kept bad entry $x" || ok "refresh: dropped bad entry $x"
done
apps list | grep -q '^gps  *1.0.0' && bad "list: gps shown installed" || ok "list: nothing installed yet"
echo "zeroos-app-notes 2.0.0" > "$T/installed"
: > "$T/apt.log"
apps run install:notes || true
expect "install, older than what's installed: failed" state failed
grep -q "apt-get install " "$T/apt.log" && bad "install, older: apt ran" || ok "install, older: nothing installed"
: > "$T/installed"

printf 'file://%s/nowhere\n' "$T" > "$T/sources.off"
PATH="$T/bin:$PATH" ZEROOS_APPS_SOURCES=$T/sources.off ZEROOS_APPS_STATE=$T/state ZEROOS_APPS_CACHE=$T/cache \
    ZEROOS_APPS_RUN=$T/run ZEROOS_APPS_ZEROSHELL=$T/zeroshell ZEROOS_APPS_ALLOW_FILE=1 ZEROOS_APPS_ARCH=arm64 \
    "$ROOT/image/stage-zeroos/files/apps/zeroos-apps" run refresh 2>>"$T/log" || true
expect "refresh, nothing reachable: offline" state offline
cat_has app=notes && ok "refresh, offline: old catalog kept" || bad "refresh, offline: catalog lost"

# --- install -----------------------------------------------------------------
apps run install:gps
expect "install: done" state done
expect "install: names the app" app gps
grep -q "apt-get install .*$T/cache/zeroos-app-gps_1.0.0_arm64.deb" "$T/apt.log" &&
    ok "install: apt installs the downloaded package" || bad "install: apt not asked: $(cat "$T/apt.log")"
[ -f "$T/root/etc/apt/sources.list.d/zeroos-app-gps.list" ] && [ -f "$T/root/usr/share/keyrings/zeroos-app-gps.gpg" ] &&
    ok "install: the app's own apt repository goes on first" || bad "install: apt source not put in place"
grep -q 'apt-get update' "$T/apt.log" && ok "install: apt lists refreshed for it" || bad "install: no apt-get update"
[ ! -e "$T/cache/zeroos-app-gps_1.0.0_arm64.deb" ] && ok "install: download cleaned up" || bad "install: download left"
: > "$T/apt.log"

apps run install:notes
grep -q 'apt-get update' "$T/apt.log" && bad "install without a repository: needless apt-get update" ||
    ok "install without a repository: no extra apt-get update"
: > "$T/apt.log"

cp "$T/a/zeroos-app-notes_1.0.0_arm64.deb" "$T/notes.good"
echo junk >> "$T/a/zeroos-app-notes_1.0.0_arm64.deb"
apps run install:notes || true
expect "install, damaged download: failed" state failed
grep -q 'damaged' "$T/run/apps.status" && ok "install, damaged download: says so" || bad "damaged message"
grep -q 'apt-get install' "$T/apt.log" && bad "install, damaged download: still installed" || ok "install, damaged download: nothing installed"
cp "$T/notes.good" "$T/a/zeroos-app-notes_1.0.0_arm64.deb"

# A new release replaces the files the saved catalog names (a 404 on the
# device): the install fetches the lists again and gets the new version.
cp -r "$T/a" "$T/a.old"
rm -f "$T/a"/zeroos-app-notes_* "$T/a/zeroos-apps.index"
mk a notes 1.1.0
mk a gps 1.0.0
cat_has version=1.0.0 && ok "new release: catalog is still the old one" || bad "new release: setup"
: > "$T/apt.log"
apps run install:notes
expect "install after a new release: done" state done
grep -q "apt-get install .*zeroos-app-notes_1.1.0_arm64.deb" "$T/apt.log" &&
    ok "install after a new release: gets the new version" || bad "install after a new release: $(cat "$T/apt.log")"
rm -rf "$T/a"; mv "$T/a.old" "$T/a"
apps run refresh
: > "$T/apt.log"

touch "$T/apt-fail"
apps run install:notes || true
expect "install, apt fails: failed" state failed
rm -f "$T/apt-fail"

apps run install:nosuchapp || true
expect "install, unknown app: failed" state failed
apps run 'install:../../etc' || true
expect "install, bad name: failed" state failed

# --- remove ------------------------------------------------------------------
: > "$T/apt.log"
echo "zeroos-app-gps 1.0.0" > "$T/installed"
apps run remove:gps
expect "remove: done" state done
grep -q 'apt-get purge -y zeroos-app-gps' "$T/apt.log" && ok "remove: purges the package" || bad "remove: no purge"
grep -q 'apt-get autoremove --purge -y' "$T/apt.log" && ok "remove: takes its extras away" || bad "remove: no autoremove"
: > "$T/installed"
: > "$T/apt.log"
apps run remove:gps
grep -q purge "$T/apt.log" && bad "remove, not installed: still ran apt" || ok "remove, not installed: nothing to do"

# --- upgrade -----------------------------------------------------------------
printf 'file://%s/b\n' "$T" > "$T/sources"   # b offers notes 2.0.0
echo "zeroos-app-notes 1.0.0" > "$T/installed"
: > "$T/apt.log"
apps run upgrade
expect "upgrade: done" state done
grep -q "zeroos-app-notes_2.0.0_arm64.deb" "$T/apt.log" && ok "upgrade: installs the newer version" || bad "upgrade: $(cat "$T/apt.log")"
echo "zeroos-app-notes 2.0.0" > "$T/installed"
: > "$T/apt.log"
apps run upgrade
grep -q 'apt-get install' "$T/apt.log" && bad "upgrade, up to date: installed anyway" || ok "upgrade, up to date: nothing to do"
# Apps that used to be built in (the zeroos package lists them in restore):
# upgrade installs the ones that aren't installed and are on offer, once.
printf 'file://%s/a\n' "$T" > "$T/sources"   # a offers notes and gps 1.0.0
echo "zeroos-app-notes 1.0.0" > "$T/installed"
echo "notes gps nowhere" > "$T/state/restore"
: > "$T/apt.log"
apps run upgrade
expect "upgrade, restore: done" state done
grep -q "zeroos-app-gps_1.0.0_arm64.deb" "$T/apt.log" && ok "upgrade, restore: installs what was built in" || bad "upgrade, restore: $(cat "$T/apt.log")"
grep -q "zeroos-app-notes_" "$T/apt.log" && bad "upgrade, restore: reinstalled an installed app" || ok "upgrade, restore: leaves installed apps alone"
[ -e "$T/state/restore" ] && bad "upgrade, restore: list left behind" || ok "upgrade, restore: only once"
grep -q 'apt-get autoremove -y' "$T/apt.log" && ok "upgrade, restore: old extras cleared" || bad "upgrade, restore: no autoremove"
# Not purged: that would wipe what they keep (Tailscale's login).
grep -q 'apt-get autoremove --purge' "$T/apt.log" && bad "upgrade, restore: purged old extras" || ok "upgrade, restore: keeps old extras' settings"
# Apps built for an older app ABI block apt until they are gone: an install
# takes them off first and lists them for update-all to put back.
printf 'zeroos-app-notes 1.0.0 zeroos-app-abi-0\nzeroos-app-old 1.0.0 zeroos-app-abi-0\n' > "$T/installed"
: > "$T/apt.log"
apps install gps
expect "old app ABI: install still works" state done
grep -q 'dpkg --remove zeroos-app-notes' "$T/apt.log" && grep -q 'dpkg --remove zeroos-app-old' "$T/apt.log" &&
    ok "old app ABI: old builds taken off" || bad "old app ABI: $(cat "$T/apt.log")"
[ "$(sed -n '/dpkg --remove/=' "$T/apt.log" | tail -1)" -lt "$(sed -n '/apt-get install/=' "$T/apt.log" | head -1)" ] &&
    ok "old app ABI: taken off before apt runs" || bad "old app ABI: order"
grep -qx notes "$T/state/restore" && ok "old app ABI: listed for update-all" || bad "old app ABI: no restore list"
: > "$T/installed"
: > "$T/apt.log"
apps run upgrade
grep -q "zeroos-app-notes_1.0.0_arm64.deb" "$T/apt.log" && ok "old app ABI: update-all puts the new build back" || bad "old app ABI, upgrade: $(cat "$T/apt.log")"
printf 'zeroos-app-notes 1.0.0 zeroos-app-abi-%s\n' "$ABI" > "$T/installed"
: > "$T/apt.log"
apps install gps
grep -q 'dpkg --remove' "$T/apt.log" && bad "current app ABI: app taken off" || ok "current app ABI: apps left alone"
rm -f "$T/state/restore"
# A renamed app: its package takes over the old one's, and update-all swaps
# it in for whoever has the old one.
mkdir -p "$T/c"
echo "not really a module" > "$T/mods/ai.so"
printf 'NAME="AI"\nSUMMARY="Chat"\nREPLACES="zclaw"\n' > "$T/pkgs/ai.pkg"
mk c ai 1.0.0
DEB=$T/c/zeroos-app-ai_1.0.0_arm64.deb
[ "$(dpkg-deb -f "$DEB" Replaces)" = zeroos-app-zclaw ] && [ "$(dpkg-deb -f "$DEB" Conflicts)" = zeroos-app-zclaw ] &&
    ok "renamed app: package replaces the old one" || bad "renamed app: Replaces/Conflicts"
grep -qx 'replaces=zclaw' "$T/c/zeroos-apps.index" && ok "renamed app: index names the old id" || bad "renamed app: replaces="
printf 'file://%s/c\n' "$T" > "$T/sources"
printf 'zeroos-app-zclaw 1.0.0 zeroos-app-abi-%s\n' "$ABI" > "$T/installed"
mkdir -p "$T/state/extras"; echo something > "$T/state/extras/zclaw"
: > "$T/apt.log"
apps run upgrade
expect "renamed app, upgrade: done" state done
grep -q "zeroos-app-ai_1.0.0_arm64.deb" "$T/apt.log" && ok "renamed app, upgrade: installs the new one" || bad "renamed app: $(cat "$T/apt.log")"
[ -e "$T/state/extras/ai" ] && [ ! -e "$T/state/extras/zclaw" ] && ok "renamed app: its extras move over" || bad "renamed app: extras"
rm -rf "$T/state/extras"
: > "$T/installed"
: > "$T/apt.log"
apps run upgrade
grep -q 'apt-get install' "$T/apt.log" && bad "renamed app, old one not installed: installed anyway" || ok "renamed app, old one not installed: nothing to do"
printf 'file://%s/a\n' "$T" > "$T/sources"
# A package left half installed: repaired first, with nobody typing.
: > "$T/installed"
: > "$T/apt.log"
touch "$T/bad-configure"
apps install gps
expect "half-installed package: repaired, install works" state done
grep -q 'apt-get -f install' "$T/apt.log" && ok "half-installed package: apt repair ran" || bad "half-installed package: no repair"

# --- sources -----------------------------------------------------------------
apps run add-source:someone/cardputer-apps || true
grep -qx 'someone/cardputer-apps' "$T/sources" && ok "add-source: added" || bad "add-source"
apps run add-source:https://github.com/other/apps.git || true
grep -qx 'other/apps' "$T/sources" && ok "add-source: a GitHub address becomes owner/repo" || bad "add-source: github url"
n=$(wc -l < "$T/sources")
apps run add-source:someone/cardputer-apps || true
[ "$(wc -l < "$T/sources")" = "$n" ] && ok "add-source: no duplicates" || bad "add-source: duplicated"
apps run 'add-source:not a repo; rm -rf /' || true
expect "add-source, junk: failed" state failed
grep -q 'rm -rf' "$T/sources" && bad "add-source, junk: written" || ok "add-source, junk: not written"
apps run remove-source:someone/cardputer-apps || true
grep -qx 'someone/cardputer-apps' "$T/sources" && bad "remove-source" || ok "remove-source: removed"
apps sources | grep -qx 'other/apps' && ok "sources: lists them" || bad "sources"
printf '# mine\nfile://%s/a\n\nfile://%s/b\nother/apps\n' "$T" "$T" > "$T/sources"
# Signatures (zeroos-verify): a source ZeroOS has a key for must sign its index.
K=$T/keys-sig
mkdir -p "$K"
openssl genpkey -algorithm ed25519 -out "$T/apps-key.pem" 2>/dev/null
openssl pkey -in "$T/apps-key.pem" -pubout -out "$K/apps.pem" 2>/dev/null
echo "file://$T/a apps" > "$K/signers"
signed_apps() { ZEROOS_VERIFY=$ROOT/image/stage-zeroos/files/keys/zeroos-verify ZEROOS_KEYS_DIRS=$K apps "$@"; }
signed_apps run refresh
cat_has "source=file://$T/a" && bad "signed source: unsigned index used" || ok "signed source: unsigned index skipped"
cat_has "source=file://$T/b" && ok "unsigned source with no key: still used" || bad "unsigned source b dropped"
openssl pkeyutl -sign -inkey "$T/apps-key.pem" -rawin -in "$T/a/zeroos-apps.index" -out "$T/a/zeroos-apps.index.sig"
signed_apps run refresh
cat_has "source=file://$T/a" && ok "signed source: signed index used" || bad "signed source: signed index not used"
echo "# changed" >> "$T/a/zeroos-apps.index"
signed_apps run refresh
cat_has "source=file://$T/a" && bad "signed source: changed index used" || ok "signed source: changed index skipped"
sed -i '$d' "$T/a/zeroos-apps.index"
rm -f "$T/a/zeroos-apps.index.sig"
apps run refresh

apps run source-up:other/apps || true
[ "$(apps sources | tr '\n' ' ')" = "file://$T/a other/apps file://$T/b " ] && ok "source-up: moved above the one before" ||
    bad "source-up: $(apps sources | tr '\n' ' ')"
head -1 "$T/sources" | grep -qx '# mine' && ok "source-up: comments stay" || bad "source-up: comment moved"
apps run source-down:file://$T/a || true
[ "$(apps sources | tr '\n' ' ')" = "other/apps file://$T/a file://$T/b " ] && ok "source-down: moved below the next" ||
    bad "source-down: $(apps sources | tr '\n' ' ')"
apps run source-up:other/apps || true
[ "$(apps sources | head -1)" = other/apps ] && ok "source-up: the first stays first" || bad "source-up: top moved"
apps run source-up:nobody/here || true
expect "source-up, not a source: failed" state failed

# --- M5Stack's app store (registry.json) --------------------------------------
m5deb() {   # m5deb <package> <version> <arch> [control line]: an app made for M5's launcher, in $T/m5
    local d=$T/m5build/$1
    rm -rf "$d"
    mkdir -p "$d/DEBIAN" "$d/usr/share/APPLaunch/applications" "$d/usr/share/APPLaunch/bin"
    printf 'Package: %s\nVersion: %s\nArchitecture: %s\nMaintainer: t <t@t>\nDescription: t\n' "$1" "$2" "$3" > "$d/DEBIAN/control"
    [ -n "${4:-}" ] && echo "$4" >> "$d/DEBIAN/control"
    printf '[Desktop Entry]\nName=Hello\nExec=/usr/share/APPLaunch/bin/hello\nType=Application\n' \
        > "$d/usr/share/APPLaunch/applications/$1.desktop"
    dpkg-deb -b --root-owner-group "$d" "$T/m5/$1_$2_$3.deb" >/dev/null
}
m5entry() {   # m5entry <package> <version> <arch> [sha256]: one app in registry.json
    local f=$T/m5/$1_$2_$3.deb sha=${4:-}
    [ -n "$sha" ] || sha=$(sha256sum "$f" | cut -d' ' -f1)
    printf '{"title":"%s","summary":"Says hello","version":"%s","review":{"status":"approved"},' "$1" "$2"
    printf '"permissions":{"background_service":true},"depends":"libc6 (>= 2.34), python3",'
    printf '"download":{"type":"deb","package":"%s","url":"file://%s","sha256":"%s","size":%s}}' \
        "$1" "$f" "$sha" "$(stat -c %s "$f" 2>/dev/null || echo 1)"
}
mkdir -p "$T/m5"
m5deb hello-zero 1.0.0 arm64
m5deb pc-only 1.0.0 amd64
m5deb broken 1.0.0 arm64
printf '{"apps":[%s,%s,%s,%s]}' "$(m5entry hello-zero 1.0.0 arm64)" "$(m5entry pc-only 1.0.0 amd64)" \
    "$(m5entry broken 1.0.0 arm64 nothex)" \
    '{"title":"x","version":"1","download":{"package":"evil;rm","url":"https://e/x_1_arm64.deb","sha256":"'"$(printf '0%.0s' {1..64})"'","size":1}}' \
    > "$T/m5/registry.json"
printf 'file://%s/a\nfile://%s/m5/registry.json\n' "$T" "$T" > "$T/sources"
: > "$T/installed"
: > "$T/apt.log"
apps run refresh
expect "m5 store: refresh done" state done
cat_has app=m5-hello-zero && cat_has kind=m5 && cat_has package=hello-zero && cat_has app=notes &&
    ok "m5 store: its apps join the catalog as m5-<package>" || bad "m5 store: not in the catalog"
cat_has "needs=python3" && cat_has "warn=Runs in the background" && ok "m5 store: extras and warnings carried over" ||
    bad "m5 store: needs/warn"
for x in m5-pc-only m5-broken m5-evil-rm; do
    cat_has "app=$x" && bad "m5 store: kept bad entry $x" || ok "m5 store: dropped bad entry $x"
done
apps run install:m5-hello-zero
expect "m5 install: done" state done
grep -q "apt-get install .*$T/cache/hello-zero_1.0.0_arm64.deb" "$T/apt.log" &&
    ok "m5 install: apt installs the downloaded package" || bad "m5 install: $(cat "$T/apt.log")"
grep -qx 'package=hello-zero' "$T/state/m5/m5-hello-zero" &&
    grep -qx 'desktop=/usr/share/APPLaunch/applications/hello-zero.desktop' "$T/state/m5/m5-hello-zero" &&
    ok "m5 install: notes the package and its launcher entry for ZeroShell" || bad "m5 install: note"
echo "hello-zero 1.0.0" >> "$T/installed"
apps list > "$T/list"; grep -qE '^m5-hello-zero +1\.0\.0' "$T/list" && ok "m5 list: shown installed" || bad "m5 list: $(cat "$T/list")"
# A newer version in the store: Update all takes it.
m5deb hello-zero 1.1.0 arm64
printf '{"apps":[%s]}' "$(m5entry hello-zero 1.1.0 arm64)" > "$T/m5/registry.json"
: > "$T/apt.log"
apps run upgrade
grep -q "apt-get install .*hello-zero_1.1.0_arm64.deb" "$T/apt.log" && ok "m5 upgrade: newer version installed" ||
    bad "m5 upgrade: $(cat "$T/apt.log")"
: > "$T/apt.log"
apps run remove:m5-hello-zero
expect "m5 remove: done" state done
grep -q 'apt-get purge -y hello-zero$' "$T/apt.log" && ok "m5 remove: purges its package" || bad "m5 remove: $(cat "$T/apt.log")"
[ ! -e "$T/state/m5/m5-hello-zero" ] && ok "m5 remove: note gone" || bad "m5 remove: note left"
: > "$T/installed"
# A package of M5's copied over by hand.
apps install-file "$T/m5/hello-zero_1.0.0_arm64.deb"
expect "m5 install-file: done" state done
grep -qx 'package=hello-zero' "$T/state/m5/m5-hello-zero" && ok "m5 install-file: noted as m5-hello-zero" ||
    bad "m5 install-file: note"
grep -qx m5stack "$ROOT/image/stage-zeroos/files/apps/apps.sources" && ok "m5 store: a source out of the box" ||
    bad "m5 store: not in apps.sources"
# M5 moved its list to cardputer.cc; the old address redirects to plain http,
# which curl refuses, so it must be read as the new one.
(
    eval "$(sed -n -e '/^M5_STORE=/p' -e '/^M5_STORE_OLD=/p' -e '/^source_url() {/,/^}/p' "$ROOT/image/stage-zeroos/files/apps/zeroos-apps")"
    new=https://cardputer.cc/generated/registry.json
    [ "$(source_url m5stack)" = "$new" ] &&
        [ "$(source_url https://cardputerzero.github.io/generated/registry.json)" = "$new" ] &&
        [ "$(source_url https://cardputerzero.github.io/generated/registry.json/)" = "$new" ]
) && ok "m5 store: m5stack and the old address both read as cardputer.cc" || bad "m5 store: address"

# M5Stack's catalog isn't ours: it can't replace what's already on the device.
echo "openssh-server 1:10.0" >> "$T/installed"
m5deb openssh-server 99.0 arm64
m5deb sneaky-cam 1.0.0 arm64 "Conflicts: zero-cam, openssh-server (<< 99)"
m5deb grabby 1.0.0 arm64
m5deb unreviewed 1.0.0 arm64
printf '{"apps":[%s,%s,%s,%s]}' "$(m5entry openssh-server 99.0 arm64)" "$(m5entry sneaky-cam 1.0.0 arm64)" \
    "$(m5entry grabby 1.0.0 arm64)" "$(m5entry unreviewed 1.0.0 arm64 | sed 's/"review":{"status":"approved"},//')" \
    > "$T/m5/registry.json"
apps run refresh
cat_has app=m5-unreviewed && bad "m5 store: kept an app with no review status" ||
    ok "m5 store: an app with no review status is left out"
: > "$T/apt.log"
apps run install:m5-openssh-server || true
expect "m5 install, same name as a system package: failed" state failed
grep -q "would replace openssh-server" "$T/run/apps.status" && ok "  ...because it would replace openssh-server" || bad "  wrong reason: $(cat "$T/run/apps.status")"
apps run install:m5-sneaky-cam || true
expect "m5 install, conflicts with a system package: failed" state failed
grep -q "would replace openssh-server" "$T/run/apps.status" && ok "  ...because it would replace openssh-server" || bad "  wrong reason: $(cat "$T/run/apps.status")"
echo openssh-server > "$T/apt-remv"
apps run install:m5-grabby || true
expect "m5 install, apt would remove a system package: failed" state failed
grep -q "would remove openssh-server" "$T/run/apps.status" && ok "  ...because it would remove openssh-server" || bad "  wrong reason: $(cat "$T/run/apps.status")"
rm -f "$T/apt-remv"
grep -q "apt-get install " "$T/apt.log" && bad "m5 guard: apt ran: $(cat "$T/apt.log")" ||
    ok "m5 guard: nothing was installed"
echo hello-zero > "$T/apt-remv"   # another M5 app (noted by install-file above) may go
apps run install:m5-grabby
expect "m5 install that replaces another M5 app: done" state done
rm -f "$T/apt-remv"

[ "$fails" = 0 ] && echo "all passed" || { echo "$fails failed"; sed 's/^/log: /' "$T/log" | tail -30; exit 1; }
