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
install -D -m 0644 "$F/zeroos-memory.tmpfiles" "$D/etc/tmpfiles.d/zeroos-memory.conf"
install -m 0644 "$F/journald.conf"             "$D/etc/systemd/journald.conf.d/90-zeroos.conf"
install -m 0644 "$F/zeroos-cloud-init-once.service" "$D/etc/systemd/system/"
install -m 0644 "$F/zeroos-mic-gain.service"   "$D/etc/systemd/system/"
install -d "$D/etc/systemd/system/sound.target.wants"
ln -sf ../zeroos-mic-gain.service "$D/etc/systemd/system/sound.target.wants/zeroos-mic-gain.service"
install -m 0755 "$F/zeroos-measure"            "$D/usr/bin/zeroos-measure"
install -m 0755 "$F/zeroos-power-perms"        "$D/usr/lib/zeroos/power-perms"
# BlueZ options for Bluetooth LE gamepads; the postinst applies them.
install -m 0755 "$F/bluetooth/zeroos-bluetooth-conf" "$D/usr/lib/zeroos/bluetooth-conf"
# Dock mode: labwc desktop on the HDMI monitor (docs/DOCK.md).
install -m 0644 "$F/dock/rc.xml" "$F/dock/menu.xml" "$F/dock/autostart" "$F/dock/environment" \
                "$F/dock/waybar.json" "$F/dock/waybar.css" "$D/etc/zeroos/dock/"
install -m 0755 "$F/dock/dock-session"         "$D/usr/lib/zeroos/dock-session"
install -m 0755 "$F/dock/dock-resolution" "$F/dock/vt-switch" "$F/dock/dock-keys" "$D/usr/lib/zeroos/"
install -m 0644 "$F/dock/zeroos-dock-keys.service" "$D/etc/systemd/system/"
install -m 0755 "$F/dock/zeroos-dock" "$F/dock/zeroos-browser" "$F/dock/zeroos-video" "$D/usr/bin/"
install -m 0644 "$F/dock/zeroos-dock.service"  "$D/etc/systemd/system/"
# Terminal app: tmux on tty3 (docs/REMOTE.md).
install -m 0644 "$F/remote/zeroos-terminal.service" "$D/etc/systemd/system/"
# tty3 belongs to the Terminal. Switching to it made logind start a login
# prompt there too (autovt), which hung up the Terminal's session at once.
ln -sf /dev/null "$D/etc/systemd/system/autovt@tty3.service"
ln -sf /dev/null "$D/etc/systemd/system/getty@tty3.service"
install -m 0755 "$F/remote/terminal-session"   "$D/usr/lib/zeroos/terminal-session"
install -m 0755 "$F/remote/console-on-lcd"     "$D/usr/lib/zeroos/console-on-lcd"
install -m 0644 "$F/remote/tmux.conf"          "$D/etc/zeroos/tmux.conf"
# Everything else (Remote, Sync, Car, GPS, Mesh, Tether...) is an installable
# app now, with its own files: image/apps/<id>.pkg (docs/APPS.md).
# The Apps app: zeroos-apps installs and removes them.
install -m 0755 "$F/apps/zeroos-apps"          "$D/usr/bin/zeroos-apps"
install -D -m 0644 "$F/apps/apt-lock-wait.conf"   "$D/etc/apt/apt.conf.d/90zeroos-lock-wait"
install -m 0755 "$F/apps/m5-fb0-link"         "$D/usr/lib/zeroos/m5-fb0-link"
install -m 0755 "$F/apps/m5-registry"         "$D/usr/lib/zeroos/m5-registry"
install -m 0644 "$F/apps/zeroos-apps@.service" "$D/etc/systemd/system/"
install -m 0644 "$F/apps/apps.sources"         "$D/etc/zeroos/apps.sources"
install -d "$D/usr/lib/zeroos/apps"
# Signatures on updates and app lists (docs/UPDATE.md, "Signatures"): the
# checker, which key signs what, and the public keys (none until one is made
# with tools/signing-key.sh; until then lists are taken unsigned, as before).
# The release key is keys/zeroos-release.pem.
install -m 0755 "$F/keys/zeroos-verify"        "$D/usr/lib/zeroos/verify"
install -D -m 0644 "$F/keys/signers"           "$D/usr/share/zeroos/keys/signers"
for k in "$F"/keys/*.pem; do
    [ -f "$k" ] && install -m 0644 "$k" "$D/usr/share/zeroos/keys/"
done
# Updates over the network or from a PC (docs/UPDATE.md).
install -m 0755 "$F/update/zeroos-update"      "$D/usr/bin/zeroos-update"
install -m 0644 "$F/update/zeroos-update.service" "$F/update/zeroos-update-check.service" \
                                               "$D/etc/systemd/system/"
install -m 0644 "$F/update/update.conf"        "$D/etc/zeroos/update.conf"
# First login: choose a new password while the image's default is still set.
# The marker and the sshd setting that switch this on are made by the image
# build only (01-run.sh), so an update never turns it on for a device.
install -m 0755 "$F/password/first-password" "$F/password/password-check" "$D/usr/lib/zeroos/"
install -m 0644 "$F/password/zeroos-password-check.service" "$F/password/zeroos-password-check.path" \
                                               "$D/etc/systemd/system/"
install -D -m 0644 "$F/password/profile.sh"    "$D/etc/profile.d/zeroos-password.sh"
install -D -m 0644 "$F/password/sshd-default-password.conf" \
                   "$D/usr/share/zeroos/sshd-default-password.conf"
# First-boot volume: WirePlumber's default is close to silent on this speaker (M5 bug #262).
install -D -m 0644 "$F/50-zeroos-default-volume.conf" \
                   "$D/etc/wireplumber/wireplumber.conf.d/50-zeroos-default-volume.conf"
# /dev/i2c-* for the Grove port and i2c-tools (M5 loads this in a script we drop).
echo i2c-dev >                                 "$D/etc/modules-load.d/zeroos.conf"
# LCD early (docs/PERFORMANCE.md, "Boot"): the home screen can't draw until
# the LCD driver is up, and left to udev that was about 15 s into the boot,
# queued behind every other device. Load its chain first, in order: the I2C
# bus, M5's IO expander on it (it drives the backlight), the backlight, the
# SPI bus, then M5's panel driver (by name, never the kernel's: see
# modprobe-zeroos-panel.conf). A name this board lacks is skipped with a log
# line and udev loads the rest as before.
printf '%s\n' i2c_bcm2835 m5ioe1 pwm_bl_m5stack spi_bcm2835 panel_mipi_dbi_m \
                                             > "$D/etc/modules-load.d/zeroos-lcd.conf"
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

# (IR Remote's code database travels with the IR Remote app: image/apps/ir.pkg.)

# Overrides: settings files that belong to another Debian package, so ours
# can't ship them in place. The package's postinst copies this tree over /.
#   earlyoom: what to kill first when memory runs out (docs/PERFORMANCE.md)
# (The GPS app carries its own: image/apps/gps.pkg.)
O="$D/usr/share/zeroos/overrides"
install -D -m 0644 "$F/earlyoom"               "$O/etc/default/earlyoom"
