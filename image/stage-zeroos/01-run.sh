#!/bin/bash -e
# ZeroOS stage: ZeroShell + system tuning for the Cardputer Zero.
# Each tweak cites the problem it fixes (see docs/HARDWARE.md, "Known issues").
# Files that an update can change are in payload.sh; this script does the rest.

# ZeroOS's own files go on as one package, zeroos_<version>_arm64.deb: the
# same package zeroos-update installs later (docs/UPDATE.md). payload.sh
# lists what is in it. A copy stays on the image so the first update has
# something to roll back to.
./package.sh files/zeroshell "${ROOTFS_DIR}/tmp/zeroos-pkg" "@ZEROOS_VERSION@"
install -D -m 0644 "${ROOTFS_DIR}"/tmp/zeroos-pkg/zeroos_*.deb "${ROOTFS_DIR}/var/lib/zeroos/update/current.deb"
# The folders the apps save into (Notes, Recordings, Sync...). The first user
# is uid 1000 on pi-gen; a build where it isn't would leave them root-owned.
for d in Videos Notes Documents Recordings; do
    install -d -o 1000 -g 1000 "${ROOTFS_DIR}/home/${FIRST_USER_NAME}/$d"
done
# Console font for the Terminal app: Terminus 6x12 gives 53 x 14 on 320 x 170.
CONSOLE_SETUP="${ROOTFS_DIR}/etc/default/console-setup"
if [ -f "$CONSOLE_SETUP" ]; then
    sed -i -e 's/^FONTFACE=.*/FONTFACE="Terminus"/' -e 's/^FONTSIZE=.*/FONTSIZE="6x12"/' "$CONSOLE_SETUP"
    grep -q '^FONTSIZE="6x12"' "$CONSOLE_SETUP" ||
        echo "zeroos: WARNING: console font not set (no FONTSIZE line); the Terminal app will be the wrong size"
else
    echo "zeroos: WARNING: no /etc/default/console-setup; the Terminal app will be the wrong size"
fi
# Audio: ZeroShell is a system service, so nothing logs pi in. Lingering starts
# pi's user session (PipeWire, WirePlumber) at boot; without it volume and mute
# have no sound server to talk to. M5's image gets this from its launcher.
install -d -m 0755 "${ROOTFS_DIR}/var/lib/systemd/linger"
touch "${ROOTFS_DIR}/var/lib/systemd/linger/${FIRST_USER_NAME}"
# LCD driver: M5's panel-mipi-dbi-m.ko and the kernel's own panel-mipi-dbi.ko
# both register an SPI driver called "panel-mipi-dbi-spi", so only the first
# one loaded works. Only M5's matches this panel. Loading the kernel's one
# first (we used to, from the initramfs) left the LCD with no driver: black
# screen. Like M5, load nothing early; udev loads M5's driver by its alias.
# The zeroos package blacklists the kernel's one (modprobe-zeroos-panel.conf),
# so it never gets in first.

CONFIG_TXT="${ROOTFS_DIR}/boot/firmware/config.txt"
CMDLINE_TXT="${ROOTFS_DIR}/boot/firmware/cmdline.txt"

# Smaller GPU split: no desktop, so give RAM back to Linux (owner report: gpu_mem=32 works).
grep -q '^gpu_mem=' "$CONFIG_TXT" && sed -i 's/^gpu_mem=.*/gpu_mem=32/' "$CONFIG_TXT" || echo 'gpu_mem=32' >> "$CONFIG_TXT"

# Faster boot (docs/PERFORMANCE.md, "Boot"):
#   boot_delay=0: the firmware otherwise waits 1 s before loading the kernel.
#   disable_splash=1: skip the firmware's rainbow screen (there is no logo now).
#   display_auto_detect=0: no DSI panel on this board; don't probe for one.
set_config() {
    grep -q "^$1=" "$CONFIG_TXT" && sed -i "s/^$1=.*/$1=$2/" "$CONFIG_TXT" || echo "$1=$2" >> "$CONFIG_TXT"
}
set_config boot_delay 0
set_config disable_splash 1
set_config display_auto_detect 0

# Wi-Fi regulatory domain (M5 ships AE); quiet boot without Plymouth.
sed -i -e 's/cfg80211.ieee80211_regdom=[A-Z]*//' -e 's/ splash//' -e 's/ plymouth.ignore-serial-consoles//' "$CMDLINE_TXT"
sed -i '1 s/$/ cfg80211.ieee80211_regdom=@WIFI_COUNTRY@/' "$CMDLINE_TXT"
# The UART belongs to GPS modules, not a serial console.
sed -i -e 's/console=serial0,[0-9]* *//' -e 's/console=ttyS0,[0-9]* *//' -e 's/console=ttyAMA0,[0-9]* *//' "$CMDLINE_TXT"
# Boot text on the LCD: M5 hides it (quiet, fbcon=map:off) behind their logo.
# We have no Plymouth and no logo, so leave the console on: it takes the first
# framebuffer, which is the LCD. Without it the LCD is black until ZeroShell
# starts, minutes on a first boot. 90-zeroos.udev covers a monitor at boot.
sed -i -e 's/ *fbcon=map:[^ ]*//' -e 's/ quiet//' "$CMDLINE_TXT"
grep -q '^enable_uart=' "$CONFIG_TXT" && sed -i 's/^enable_uart=.*/enable_uart=1/' "$CONFIG_TXT" || echo 'enable_uart=1' >> "$CONFIG_TXT"
# Boot at full CPU speed. ZeroShell sets the governor for the chosen power
# profile as soon as it starts (ui/power.cpp), so this only lasts the boot.
sed -i -e 's/ *cpufreq.default_governor=[^ ]*//' -e '1 s/$/ cpufreq.default_governor=performance/' "$CMDLINE_TXT"
# The LCD as fb0, as on M5's system. The firmware's boot framebuffer
# (simplefb, 720x480, nothing shows it) otherwise takes fb0, the LCD gets fb1,
# and vc4 removes fb0 again: M5Stack's apps that read
# /sys/class/graphics/fb0 then stop at launch. The package's postinst adds
# the same for updated devices.
grep -q 'simplefb_driver_init' "$CMDLINE_TXT" || sed -i '1 s/$/ initcall_blacklist=simplefb_driver_init/' "$CMDLINE_TXT"

# Keyboard symbol layer system-wide, not only under M5's launcher (owner report).
KMAP=/usr/share/keymaps/tca8418_keypad_m5stack_keymap.map
if [ -f "${ROOTFS_DIR}${KMAP}" ]; then
    sed -i '/^KMAP=/d' "${ROOTFS_DIR}/etc/default/keyboard"
    echo "KMAP=\"${KMAP}\"" >> "${ROOTFS_DIR}/etc/default/keyboard"
fi

# Apps to ship ready-installed (image/build.sh, ZEROOS_APPS): the same
# packages the Apps app downloads (docs/APPS.md).
install -d "${ROOTFS_DIR}/tmp/zeroos-apps"
cp files/apps-preinstall/*.deb "${ROOTFS_DIR}/tmp/zeroos-apps/" 2>/dev/null || true

on_chroot << 'CHROOT'
set -e
# The zeroos package: ZeroShell, its services and settings (built above).
dpkg -i /tmp/zeroos-pkg/zeroos_*.deb
rm -rf /tmp/zeroos-pkg
echo "zeroos: package installed: $(dpkg-query -W -f '${Version}' zeroos)"
# Apps (docs/APPS.md): everything but the base apps is installed from the
# Apps app. The few chosen for this image (ZEROOS_APPS) go on here, with
# what they depend on, exactly as zeroos-apps would put them on.
for deb in /tmp/zeroos-apps/*.deb; do
    [ -e "$deb" ] || continue
    ZEROOS_APPS_ZEROSHELL=/usr/bin/zeroshell zeroos-apps install-file "$deb"
    echo "zeroos: app installed: $(dpkg-deb -f "$deb" Package) $(dpkg-deb -f "$deb" Version)"
done
rm -rf /tmp/zeroos-apps
# arm64 only: M5 enables armhf too, which doubles apt indexes (owner report).
dpkg --remove-architecture armhf 2>/dev/null || true
# nexmon shadows the onboard Wi-Fi driver and has no BCM43439 support.
apt-get purge -y brcmfmac-nexmon-dkms 2>/dev/null || true
apt-get purge -y plymouth 2>/dev/null || true

# ZeroShell owns tty1's screen; stop keys leaking into a login prompt behind it.
systemctl mask getty@tty1.service
systemctl enable zeroshell.service NetworkManager.service bluetooth.service earlyoom.service
systemctl enable zeroos-cloud-init-once.service
systemctl set-default multi-user.target

# Lean base: switch off what a pocket computer doesn't need running.
# Each is only touched if installed, so a changed base image doesn't break the build.
disable_if_present() {
    for u in "$@"; do
        if [ -e "/usr/lib/systemd/system/$u" ] || [ -e "/lib/systemd/system/$u" ]; then
            systemctl disable "$u" 2>/dev/null || true
            echo "zeroos: disabled $u"
        fi
    done
}
#   apt-daily*, man-db: daily jobs that spike CPU, RAM and SD writes on battery
#   e2scrub_all: weekly filesystem scrub (needs LVM; does nothing useful here)
#   NetworkManager-wait-online: holds up boot until Wi-Fi connects
#   rpi-eeprom-update: the CM0 has no bootloader EEPROM
#   rpi-connect / rpi-connect-wayvnc: Raspberry Pi's remote access (Tailscale replaces it)
#   ModemManager: no modem
#   triggerhappy: hotkey daemon; ZeroShell reads the keyboard itself
#   rsyslog: a second copy of every log line on the card (journald keeps one)
#   dphys-swapfile: old swap file service; rpi-swap does zram + file swap now
#   systemd-networkd-wait-online: NetworkManager runs the network, not networkd
#   rpcbind: only old NFS (v3) needs it, and its socket still starts it then
#   raspi-config: only resets the CPU governor at boot; ZeroShell sets it
#   bluealsa, bluealsa-aplay: a second Bluetooth audio stack (BlueALSA, from
#     the base image). It takes a phone's music before PipeWire can, then
#     can't play it (bluealsa-aplay runs as root and can't reach the user's
#     PipeWire: "Host is down"), so the music is lost. PipeWire does
#     Bluetooth audio here (docs/CAR.md).
disable_if_present apt-daily.timer apt-daily-upgrade.timer man-db.timer e2scrub_all.timer \
    e2scrub_reap.service NetworkManager-wait-online.service rpi-eeprom-update.service \
    ModemManager.service triggerhappy.service triggerhappy.socket rsyslog.service \
    dphys-swapfile.service systemd-networkd-wait-online.service \
    rpcbind.service raspi-config.service bluealsa.service bluealsa-aplay.service
systemctl --global disable rpi-connect.service rpi-connect-wayvnc.service 2>/dev/null || true
# adbd (in M5's package list) is enabled by its package: at every boot it sets
# up a USB gadget and offers a root shell to whatever the USB port is plugged
# into, with no pairing. M5 turn it off in the script we drop; do the same.
systemctl disable adbd.service 2>/dev/null || true
rm -f /etc/systemd/system/multi-user.target.wants/adbd.service

# Every image has the same password, so the first login (SSH, the Terminal
# app or a console) asks for a new one. The marker holds the password hash
# the image was built with; password-check removes it, and the sshd setting,
# once pi's password is anything else. Not an expired password (chage -d 0):
# the Terminal, Dock and pi's user session start through PAM, which refuses
# an expired account.
install -d -m 0755 /var/lib/zeroos
( umask 077; printf '%s %s\n' pi "$(awk -F: '$1 == "pi" {print $2}' /etc/shadow)" > /var/lib/zeroos/default-password )
grep -q '^pi [^ ]' /var/lib/zeroos/default-password || { echo "zeroos: ERROR: no password hash for pi" >&2; exit 1; }
# While the default is set, SSH takes keys only (see the file for why).
install -m 0644 /usr/share/zeroos/sshd-default-password.conf \
    /etc/ssh/sshd_config.d/10-zeroos-default-password.conf
grep -q '^Include /etc/ssh/sshd_config.d/\*.conf' /etc/ssh/sshd_config ||
    echo "zeroos: WARNING: sshd_config has no sshd_config.d include; SSH won't ask for a new password"
systemctl enable zeroos-password-check.service zeroos-password-check.path

groupadd -f spi   # /dev/spidev* stays root-only without it (owner report)
groupadd -f bluetooth   # normally made by bluez; zeroshell.service names it
for g in video input audio netdev i2c gpio spi dialout adm bluetooth; do   # adm: read service logs; bluetooth: pair devices
    getent group "$g" >/dev/null && usermod -aG "$g" pi
done
# The initramfs picks up modprobe.d (the panel blacklist). It has always been
# allowed to fail here (qemu chroot), but say so loudly in the build log.
update-initramfs -u -k all || echo "zeroos: WARNING: update-initramfs failed; the image keeps its old initramfs"
CHROOT
