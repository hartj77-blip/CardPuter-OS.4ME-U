#!/usr/bin/env bash
# Lay out every file ZeroOS owns under <dest>. This is what an update can
# change; package.sh packs it into the zeroos .deb. What only the image
# build does (Debian packages, boot settings, the initramfs, the user's
# folders) stays in 01-run.sh.
# Each tweak cites the problem it fixes (see docs/HARDWARE.md, "Known issues").
#
# Usage: payload.sh <files-dir> <dest> <version> <zeroshell-binary>
set -euo pipefail
umask 022
F=$1 D=$2 VERSION=$3 BIN=$4

install -d "$D/usr/bin" "$D/usr/lib/zeroos" "$D/etc/zeroos/dock" "$D/etc/systemd/system" \
           "$D/etc/polkit-1/rules.d" "$D/etc/udev/rules.d" "$D/etc/sysctl.d" "$D/etc/modules-load.d" \
           "$D/etc/rpi/swap.conf.d" "$D/etc/systemd/zram-generator.conf.d" "$D/etc/systemd/journald.conf.d"

install -m 0755 "$BIN"                         "$D/usr/bin/zeroshell"
echo "ZeroOS $VERSION (built $(date -u +%Y-%m-%d))" > "$D/etc/zeroos-release"
install -m 0644 "$F/zeroshell.service"         "$D/etc/systemd/system/zeroshell.service"
install -m 0644 "$F/90-zeroos.rules"           "$D/etc/polkit-1/rules.d/90-zeroos.rules"
install -m 0644 "$F/90-zeroos.udev"            "$D/etc/udev/rules.d/90-zeroos.rules"
install -m 0644 "$F/swap.conf"                 "$D/etc/rpi/swap.conf.d/80-zeroos.conf"
install -m 0644 "$F/zram-zeroos.conf"          "$D/etc/systemd/zram-generator.conf.d/30-zeroos.conf"
install -m 0644 "$F/90-zeroos-memory.conf"     "$D/etc/sysctl.d/90-zeroos-memory.conf"
install -m 0644 "$F/journald.conf"             "$D/etc/systemd/journald.conf.d/90-zeroos.conf"
install -m 0644 "$F/zeroos-cloud-init-once.service" "$D/etc/systemd/system/"
install -m 0755 "$F/zeroos-measure"            "$D/usr/bin/zeroos-measure"
install -m 0755 "$F/zeroos-power-perms"        "$D/usr/lib/zeroos/power-perms"
# Dock mode: labwc desktop on the HDMI monitor (docs/DOCK.md).
install -m 0644 "$F/dock/rc.xml" "$F/dock/menu.xml" "$F/dock/autostart" "$F/dock/environment" \
                "$F/dock/waybar.json" "$F/dock/waybar.css" "$D/etc/zeroos/dock/"
install -m 0755 "$F/dock/dock-session"         "$D/usr/lib/zeroos/dock-session"
install -m 0755 "$F/dock/zeroos-dock" "$F/dock/zeroos-browser" "$F/dock/zeroos-video" "$D/usr/bin/"
install -m 0644 "$F/dock/zeroos-dock.service"  "$D/etc/systemd/system/"
# Remote mode: Terminal app (tmux on tty3), Tailscale helpers (docs/REMOTE.md).
install -m 0644 "$F/remote/zeroos-terminal.service" "$F/remote/zeroos-taildrop.service" \
                                               "$D/etc/systemd/system/"
install -m 0755 "$F/remote/terminal-session"   "$D/usr/lib/zeroos/terminal-session"
install -m 0644 "$F/remote/tmux.conf"          "$D/etc/zeroos/tmux.conf"
install -D -m 0644 "$F/remote/tailscaled-zeroos.conf" \
                   "$D/etc/systemd/system/tailscaled.service.d/zeroos.conf"
# Sync: Syncthing for Notes, Documents and Recordings (docs/SYNC.md).
install -m 0755 "$F/sync/zeroos-sync"          "$D/usr/bin/zeroos-sync"
install -D -m 0644 "$F/sync/syncthing-zeroos.conf" \
                   "$D/etc/systemd/system/syncthing@.service.d/zeroos.conf"
# Car: USB stick mode and Bluetooth audio (docs/CAR.md).
install -m 0755 "$F/car/zeroos-usb-stick"      "$D/usr/lib/zeroos/usb-stick"
install -m 0644 "$F/car/zeroos-usb-stick.service" "$D/etc/systemd/system/"
# uinput: BlueZ turns the car's play/skip buttons into key presses with it.
printf 'uinput\nlibcomposite\n' >              "$D/etc/modules-load.d/zeroos-car.conf"
# Expansion: LoRa on SPI0 CE1 (GPS: see the overrides below).
install -D -m 0644 "$F/expansion/meshtasticd-cap-lora-1262.yaml" \
                   "$D/etc/meshtasticd/config.d/zeroos-cap-lora-1262.yaml"
install -D -m 0644 "$F/expansion/meshtasticd-zeroos.conf" \
                   "$D/etc/systemd/system/meshtasticd.service.d/zeroos.conf"
# Tethering through a phone, over USB or Bluetooth (docs/TETHER.md).
install -m 0755 "$F/tether/zeroos-tether"      "$D/usr/bin/zeroos-tether"
# Updates over the network or from a PC (docs/UPDATE.md).
install -m 0755 "$F/update/zeroos-update"      "$D/usr/bin/zeroos-update"
install -m 0644 "$F/update/zeroos-update.service" "$F/update/zeroos-update-check.service" \
                                               "$D/etc/systemd/system/"
install -m 0644 "$F/update/update.conf"        "$D/etc/zeroos/update.conf"
# First-boot volume: WirePlumber's default is close to silent on this speaker (M5 bug #262).
install -D -m 0644 "$F/50-zeroos-default-volume.conf" \
                   "$D/etc/wireplumber/wireplumber.conf.d/50-zeroos-default-volume.conf"
# /dev/i2c-* for the Grove port and i2c-tools (M5 loads this in a script we drop).
echo i2c-dev >                                 "$D/etc/modules-load.d/zeroos.conf"
# ZeroShell restores brightness itself. systemd-backlight would save the level at
# shutdown (0 if the screen was off) and put it back at boot; M5 masks it too.
ln -sf /dev/null "$D/etc/systemd/system/systemd-backlight@backlight:backlight.service"
install -D -m 0644 "$F/nm-wifi-powersave.conf" "$D/etc/NetworkManager/conf.d/90-zeroos-powersave.conf"
# Console-setup blanks the LCD on boot (race); unblank afterwards (owner report).
install -d "$D/etc/systemd/system/console-setup.service.d"
cat > "$D/etc/systemd/system/console-setup.service.d/zeroos-unblank.conf" <<'UNIT'
[Service]
ExecStartPost=-/bin/sh -c 'for b in /sys/class/graphics/fb*/blank; do echo 1 > $b; echo 0 > $b; done'
UNIT
# Swap on a first boot: rpi-resize.service (grow the card) ends with
# "systemctl disable", which reloads systemd. If that lands while
# rpi-setup-loop@var-swap is starting, its step that makes the swap file's
# /dev/disk/by-backingfile link is lost and the unit waits for the link with
# no time limit: the boot stops there. (QEMU first boots: hung once, no swap
# three times. M5's image doesn't resize on first boot, so it never races.)
# Start after the resize, and never wait more than 30 s.
install -d "$D/etc/systemd/system/rpi-setup-loop@.service.d"
cat > "$D/etc/systemd/system/rpi-setup-loop@.service.d/zeroos.conf" <<'UNIT'
[Unit]
After=rpi-resize.service

[Service]
TimeoutStartSec=30
UNIT
# LCD driver: never load the kernel's panel-mipi-dbi; it takes the driver name
# M5's panel-mipi-dbi-m needs, and the LCD stays black (see 01-run.sh).
install -D -m 0644 "$F/modprobe-zeroos-panel.conf" "$D/etc/modprobe.d/zeroos-panel.conf"
# M5's boot logo (fb_load.service, from the LCD driver) holds the LCD's DRM
# device until M5's launcher starts, and waits forever for /dev/dri/card1. We
# ship neither that launcher nor a logo (boot text is on the LCD instead), so
# switch it off; held open, nothing ZeroShell draws reaches the screen.
ln -sf /dev/null "$D/etc/systemd/system/fb_load.service"

# IR Remote's code database: Flipper-IRDB, built by tools/irdb/build_irdb.py
# (docs/IR.md). image/package.sh and image/build.sh build it; the image's
# copy travels in files/irdb.
IRDB=${ZEROOS_IRDB:-$F/irdb}
if [ -f "$IRDB/index.tsv" ]; then
    install -d "$D/usr/share/zeroos/ir"
    cp -r "$IRDB/." "$D/usr/share/zeroos/ir/"
    find "$D/usr/share/zeroos/ir" -type d -exec chmod 0755 {} + -o -type f -exec chmod 0644 {} +
else
    echo "warning: no IR code database at $IRDB; the IR Remote app will only have learned remotes" >&2
fi

# Overrides: settings files that belong to another Debian package, so ours
# can't ship them in place. The package's postinst copies this tree over /.
#   earlyoom: what to kill first when memory runs out (docs/PERFORMANCE.md)
#   gpsd: GPS on the UART (G14/G15, EXT and Grove)
O="$D/usr/share/zeroos/overrides"
install -D -m 0644 "$F/earlyoom"               "$O/etc/default/earlyoom"
install -D -m 0644 "$F/expansion/gpsd.default" "$O/etc/default/gpsd"
