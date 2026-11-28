# Hardware map

Sources: [M5 CardputerZero docs](https://docs.m5stack.com/en/CardputerZero), M5's
[pi-gen fork](https://github.com/CardputerZero/pi-gen) and
[board drivers](https://github.com/m5stack/m5stack-linux-dtoverlays), and early owner reports
([Markus Birth](https://blog.mbirth.uk/2026/11/11/taming-the-cardputerzero.html),
[M5CZ-Kali-Graft](https://github.com/n0xa/M5CZ-Kali-Graft)). Items marked * are absent on the Lite.

| Part | Chip | Linux interface | ZeroOS uses it for |
| --- | --- | --- | --- |
| SoC | RP3A0 (BCM2837), 4x A53 @ 1 GHz, 512 MB | | Everything |
| Display | ST7789V3 320x170, SPI | `/dev/fb_lcd`: fb0, or fb1 with a monitor connected at boot (DRM mipi-dbi + firmware blob) | ZeroShell |
| Backlight | PWM | `/sys/class/backlight/*` | Settings > Brightness |
| Keyboard | TCA8418 (I2C 0x34) | evdev input device. M5's driver handles Shift, Fn and Sym itself (tap = next key only, double tap = lock) and sends a different key code per layer; Ctrl and Alt are plain keys, and ZeroShell makes a tapped Ctrl apply to the next key; `shell/src/platform/keys.cpp` turns those into characters | All input |
| Key LEDs | Shift, Fn, Sym, 3 status | `/sys/class/leds/*` | `Hal::set_led` (modifier state, alerts: Phase 1) |
| Fuel gauge | BQ27220 | `/sys/class/power_supply/bq27220-0/` | Status bar, Settings > Battery & power |
| RTC | RX8130CE | `/dev/rtc0` | Offline time, Clock app; set in Settings > Date & time |
| IMU* | BMI270 + BMM150 | iio (`in_accel_*`, `in_anglvel_*`, `in_magn_*`) | Compass, level |
| IR | TX + RX on GPIO | rc-core: `ir-ctl` sends our own encoded timings and learns raw (docs/IR.md) | IR Remote |
| Audio | ES8389 codec, AW8737A amp, 1 W speaker, mic, TRRS | ALSA/PipeWire; sox `play`/`rec` | Media app (music), Recorder app (voice notes), volume, mute |
| Wi-Fi / BT | BCM43439 2.4 GHz (plus any USB Wi-Fi adapter, see below) | NetworkManager, BlueZ | Settings (Wi-Fi, Bluetooth), Bluetooth audio in the Car app |
| Ethernet | SR9900A (USB) | `eth0` | Status, System app |
| USB | GL852G hub, USB-A, 2x USB-C, Host/Slave switch (Slave: right USB-C is a device port) | `dwc2`, configfs gadgets | USB stick mode in the Car app, see CAR.md |
| Camera* | IMX219 8 MP, CSI | libcamera (RPi fork) | Camera app (M5's CameraApp, started by ZeroShell; QR codes read by ZeroShell through `rpicam-vid`), see CAMERA.md |
| Video | HDMI 1080p30, H.264 enc/dec | vc4 (a DRM card of its own: card0 on M5's image, card1 on ZeroOS, where the LCD's driver starts first and is card0; fb0 only with a monitor connected), V4L2 M2M | Dock mode (labwc on the HDMI card only; mpv hardware H.264), see DOCK.md |
| Storage | microSD | | Root filesystem |
| Expansion | Grove HY2.0 (I2C/UART), 14-pin EXT bus | i2c-dev, spidev, UART | GPS app (gpsd), Mesh app (meshtasticd, Cap LoRa-1262); see EXPANSION.md |

## USB Wi-Fi adapters

A USB Wi-Fi adapter (dongle) can be used in the USB-A port, or in a USB-C port with the switch on
Host. It's handy for 5 GHz (the built-in chip is 2.4 GHz only), an external antenna, or a second
radio. Nothing needs installing for the common chips: ZeroOS uses the Raspberry Pi kernel, which
has the in-kernel USB Wi-Fi drivers (Realtek `rtl8xxxu`/`rtw88`, MediaTek `mt76`/`mt7601u`,
Ralink `rt2800usb`, Atheros `ath9k_htc`), and Raspberry Pi OS's firmware packages. Adapters that
need a driver built from outside the kernel (some newer Realtek ones) are not supported.

Once an adapter is plugged in, **Settings > Wi-Fi** has an **Adapter** row (Left/Right):

| Choice | What scans and joins |
| --- | --- |
| Auto (the default) | Either adapter; NetworkManager picks. A network joined in Auto can reconnect on either. |
| Built-in | Only the built-in chip. |
| USB | Only the USB adapter (with two plugged in: USB and USB 2). |

A network joined with Built-in or USB chosen is tied to that adapter (its profile gets the
adapter's MAC address in `802-11-wireless.mac-address`), so after a restart it reconnects on the
same one. Joining it again in Auto unties it. The choice is saved as `wifi_adapter` in
`~/.config/zeroos/settings.conf`. If the chosen adapter is unplugged, the row says
"USB (not plugged in)" and Wi-Fi works as in Auto. The heading says which adapter you're
connected through, e.g. "Connected to HomeNet (USB)".

From Terminal: `nmcli dev` lists the adapters (wlan0 is the built-in one), and
`nmcli dev wifi connect <name> ifname wlan1` joins on a specific one.

Things to know:

- A dongle draws power from the battery the whole time it's plugged in.
- USB stick mode in the Car app switches the USB port to Slave, which turns the USB-A port off,
  dongle included.
- Built-in or USB is found by looking at the driver in `/sys/class/net/*/device`
  (`hal/wifi_adapters.h`), not by interface name, which can change between restarts.

## Known issues from early owners, and what the image does

| Problem | Fix in `image/stage-zeroos/01-run.sh` |
| --- | --- |
| Symbol layer only correct while M5's launcher runs | Load `tca8418_keypad_m5stack_keymap.map` system-wide via `/etc/default/keyboard` |
| Keys leak to the Linux console behind the UI | Mask `getty@tty1`; ZeroShell conflicts with it |
| LCD black, no driver bound: the kernel's `panel-mipi-dbi` and M5's `panel-mipi-dbi-m` register the same SPI driver name (`panel-mipi-dbi-spi`), so the second one to load fails, and only M5's matches this panel | Nothing display-related is forced into the initramfs (as on M5's image); `/etc/modprobe.d/zeroos-panel.conf` (in the zeroos package) blacklists the kernel's one |
| LCD blanks when console-setup runs | Drop-in that unblanks the framebuffers afterwards |
| LCD stays on M5's logo or blank: `fb_load --keep-open` holds the LCD until M5's launcher starts, which ZeroOS doesn't ship | `fb_load.service` masked |
| LCD black until ZeroShell starts (M5 boots with `quiet fbcon=map:off` behind a logo) | Console left on: boot text shows on the LCD. A udev rule runs `con2fbmap` to move it there when a monitor took fb0 |
| Terminal app shows nothing: its console (tty3) was on another framebuffer (HDMI at boot, or a card from an older image that boots with `fbcon=map:1`) | `console-on-lcd` moves tty3 to the LCD each time the Terminal opens. With no tmux on the card it opens a plain shell |
| ZeroShell started before the LCD driver, or with no LCD, and ran on drawing nowhere | The service waits up to 30 s for the LCD; with no display ZeroShell exits so systemd restarts it; the reason is in the journal |
| First boot can stop forever in `rpi-setup-loop@var-swap`, or come up without swap: `rpi-resize.service` reloads systemd while the swap file's loop device is being set up (QEMU first boots; M5's image doesn't resize on first boot) | The loop setup starts after `rpi-resize.service` and has a 30 s start timeout |
| `adbd` enabled by its package: USB gadget and a root shell with no pairing at every boot | Disabled, as on M5's image |
| A failed boot left nothing to read (logs were in RAM) | Journal kept on the card, capped at 24 MB: `journalctl -D <card>/var/log/journal -b` |
| 512 MB RAM is tight | No desktop, `gpu_mem=32`, zram (zstd, 1.5x RAM) with SD writeback via rpi-swap, earlyoom, trimmed services; see PERFORMANCE.md |
| armhf enabled, apt twice as slow | Remove the armhf architecture |
| nexmon breaks onboard Wi-Fi | Purge `brcmfmac-nexmon-dkms` |
| Wi-Fi region hard-coded to AE | `cfg80211.ieee80211_regdom` from `WIFI_COUNTRY` (default US) |
| `/dev/spidev*` root-only | Create the `spi` group |
| Volume and mute need PipeWire, which only runs in a login session | Linger for `pi`; ZeroShell starts after `user@1000`; `pipewire-alsa` so sox plays through it. After 0.9.1 only PipeWire's sockets start with the session: PipeWire itself starts with the first sound and is stopped when unused (PERFORMANCE.md, "Started when needed") |
| Speaker close to silent on first boot (WirePlumber default 0.064) | Default sink volume 0.216 (60%), as M5 ships |
| Wrong Wi-Fi password stays saved and is retried | ZeroShell deletes the profile when a connect with a typed password fails |
| `/dev/i2c-*` missing (M5 loads `i2c-dev` in a script we drop) | `/etc/modules-load.d/zeroos.conf` |
| systemd-backlight restores the shutdown brightness at boot | Masked, as on M5's image; ZeroShell restores its own setting |
| Kernel upgrade breaks the display and keyboard (modules are built for one kernel) | Inherited from M5's pi-gen: kernel packages pinned and held. Don't `apt-mark unhold` them |
| No suspend or hibernate on the Pi kernel | Not fixable in Phase 0; plan for shutdown, show time left |
| No kernel key repeat (DT lacks autorepeat) | ZeroShell repeats held keys itself; console repeat in Phase 1 |

## To confirm on hardware (Phase 1)

- Exact names under `/sys/class/leds`, `/sys/class/backlight` and the iio devices.
- IMU axis orientation and magnetometer calibration. Motion wake assumes `in_accel_z` reads +1 g lying face up; if face down and pickup act the wrong way round, set `motion.z_sign=-1` in `~/.config/zeroos/settings.conf`.
- Whether IR transmit works with the stock driver (one owner saw no output on a non-stock image).
- USB Wi-Fi adapters (see above): which common chips come up without help, on the USB-A port and on a USB-C port in Host.
- Boot time and idle power with ZeroOS versus stock (stock: about 2.5 W idle).
