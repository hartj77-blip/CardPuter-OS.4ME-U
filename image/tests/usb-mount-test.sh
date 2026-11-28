#!/bin/bash
# zeroos-usb-mount with a stand-in systemd-mount (no USB, no root).
set -eu
H=$(cd "$(dirname "$0")/../stage-zeroos/files/usb" && pwd)/zeroos-usb-mount
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
printf '#!/bin/sh\necho "$@" > %s/args\n' "$T" > "$T/sm"; chmod +x "$T/sm"
export ZEROOS_SYSTEMD_MOUNT=$T/sm ZEROOS_MEDIA_DIR=$T/media
mkdir -p "$T/media"
fail() { echo "FAIL: $*"; exit 1; }

ID_FS_TYPE=vfat ID_FS_LABEL="MY STICK/../x" "$H" add /dev/sda1
grep -q -- "--options=nosuid,nodev,noexec,uid=1000,gid=1000,umask=0022 /dev/sda1 $T/media/MY_STICK_.._x" "$T/args" || fail "vfat: $(cat "$T/args")"
rm "$T/args"
ID_FS_TYPE=ext4 ID_FS_LABEL="" "$H" add /dev/sdb1
grep -q -- "--options=nosuid,nodev,noexec /dev/sdb1 $T/media/sdb1" "$T/args" || fail "ext4: $(cat "$T/args")"
rm "$T/args"
ID_FS_TYPE=crypto_LUKS "$H" add /dev/sdc1
[ ! -e "$T/args" ] || fail "LUKS must be left alone"
ID_FS_TYPE=vfat ID_FS_LABEL=".hidden" "$H" add /dev/sdd1
grep -q "$T/media/hidden\$" "$T/args" || fail "leading dot: $(cat "$T/args")"
"$H" add /etc/passwd && fail "only /dev nodes" || true
mkdir "$T/media/old"; "$H" clean; [ ! -d "$T/media/old" ] || fail "clean"
echo "usb-mount tests passed"
