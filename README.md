# ZeroOS for the Cardputer Zero

ZeroOS is an operating system for the [M5Stack Cardputer Zero](https://shop.m5stack.com/pages/m5-cardputerzero),
the pocket computer with a keyboard, a 320x170 screen and a Raspberry Pi CM0 inside.

Under the hood it's Debian Linux. On top is a home screen built for that tiny screen and keyboard:
no mouse, no touch, everything from the keys. It ships with four apps and you install the rest
from the device when you want them.

This is a community project. It isn't made, endorsed or supported by M5Stack.

- [Download](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/releases/latest)
- [Install](#install) · [Using it](#using-it) · [Apps](#apps) · [Troubleshooting](#troubleshooting)
- [How it works](#how-it-works) · [Building it yourself](#building-it-yourself)
- [Report a problem](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/issues)

## Status

Early days. Everything builds and passes tests in CI and runs in the simulator, but a lot of it
hasn't been tried on real hardware yet, so expect rough edges. If something doesn't work,
[open an issue](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/issues). What's done and
what's planned is in the [roadmap](docs/ROADMAP.md).

## Install

You need a microSD card and a computer to write it from.

1. Download [zeroos-cardputerzero-arm64.img.xz](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/releases/latest/download/zeroos-cardputerzero-arm64.img.xz)
   from the [latest release](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/releases/latest).
   It's about 1.5 GB. There's a `.sha256` file next to it if you want to check the download.
2. Write it to the card with [Raspberry Pi Imager](https://www.raspberrypi.com/software/)
   (Choose OS > Use custom) or [balenaEtcher](https://etcher.balena.io/). Both take the `.xz`
   file as it is, no need to unzip it.
3. Put the card in the Cardputer and switch it on. The first boot takes a few minutes.

The image is built after each release is published, so for about 10 minutes after a new release
the download link can show "not found". Wait a bit, or grab the image from an
[older release](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/releases) and update from
the device afterwards.

### Getting online

- **On the device:** open **Wi-Fi**, pick your network, type the password and press Enter.
- **Before the first boot:** in Raspberry Pi Imager, fill in your Wi-Fi name and password in its
  settings before writing the card. ZeroOS picks them up on first boot.
- **Through your phone:** use the [Tether](docs/TETHER.md) app (Wi-Fi hotspot, USB or Bluetooth).
- **QR code:** scan a Wi-Fi QR code with the [Camera](docs/CAMERA.md) app.

### Logging in

The user is `pi` and the password is `raspberry`. The first time you log in, from the Terminal
app or over SSH (`ssh pi@cardputer.local`), you have to pick a new password. If you added an SSH
key in Raspberry Pi Imager you can skip that and run `passwd` later.

## Using it

Arrow keys move, Enter opens, Esc goes back. The \` key also works as Esc.

| Key | What it does |
| --- | --- |
| Hold Esc | Home screen, from anywhere |
| Ctrl + Space | Taskbar panel: Wi-Fi, Bluetooth, volume, brightness, battery |
| 1 | Home screen (when you're not typing) |
| 2 | Taskbar panel (when you're not typing) |
| 3 to 9 | Pick the 1st to 7th item on screen |
| 0 | Show which number goes with which item |

On the home screen:

| Key | What it does |
| --- | --- |
| F / Z / X / C | Up / Left / Down / Right (no need to hold Fn here) |
| Shift + a letter | Jump to the next app starting with that letter |
| 3 to 9 | Open the dock's apps, top to bottom |
| A / S / D | Mute / volume down / volume up |

Ctrl, Shift, Fn and Sym are sticky: tap one and then press the next key. Apps show their own keys
on screen. For example, in Media Space pauses and N skips, and in IR Remote L learns a button from
another remote. The full list is in [Keys](docs/KEYS.md).

**Installing apps:** open **Apps**, pick one and press Enter. **U** updates everything.
**Updating ZeroOS:** open **Update**. No re-flashing needed, see [Updates](docs/UPDATE.md).

### Battery and memory

The Cardputer Zero has 512 MB of RAM and a small battery, so ZeroOS is set up for that:

- On battery the screen dims after 30 seconds and turns off after a minute. Turn the device face
  down to switch the screen off, pick it up to switch it back on.
- Unused memory is compressed (zram), so bigger apps still fit. If memory does run out, the
  biggest app is closed instead of the whole device locking up.
- Services a pocket computer doesn't need are off, and the HDMI desktop uses no memory until you
  open Dock or plug in a monitor.

Run `zeroos-measure` in a terminal for a memory, CPU and battery report. More in
[Battery and performance](docs/PERFORMANCE.md).

## Apps

### Built in

These four are always there, so a device can always get online, update and install apps.

| App | What it does |
| --- | --- |
| Apps | Browse, install, update and remove apps ([guide](docs/APPS.md)) |
| Settings | Wi-Fi, Bluetooth, brightness, volume, power saving, restart, shut down |
| Update | Install a newer ZeroOS over the network ([guide](docs/UPDATE.md)) |
| Wi-Fi | Find and join networks |

### Installable

Everything else comes from the Apps app. Terminal, Files and Dock are already on the SD card
image, but you can remove them like anything else. The code for these lives in
[CardPuter-Apps.4ME-U](https://github.com/hartj77-blip/CardPuter-Apps.4ME-U).

| App | What it does |
| --- | --- |
| Terminal | Linux terminal on the built-in screen |
| Files | Browse files on the device |
| Notes | Quick text notes |
| Clock | Clock and stopwatch |
| Compass | Compass and spirit level |
| Battery | Time left, power draw and power profiles ([guide](docs/PERFORMANCE.md)) |
| System | CPU, memory, storage and temperature |
| Media | Music, voice recordings, and Jellyfin music and video ([guide](docs/JELLYFIN.md)) |
| Camera | Photos, a gallery, and a QR reader that can join Wi-Fi. Needs a model with a camera ([guide](docs/CAMERA.md)) |
| IR Remote | Universal remote with about 2,000 TVs, projectors, AC units and more, and it can learn from your own remotes ([guide](docs/IR.md)) |
| Dock | Full desktop on an HDMI monitor: browser, video player, terminal, file manager ([guide](docs/DOCK.md)) |
| Desktop | Remote desktop to a PC or Mac (RDP or VNC), on the small screen or a monitor |
| Remote | Tailscale: reach your other devices, SSH, file sharing ([guide](docs/REMOTE.md)) |
| Sync | Keep notes, documents and recordings in sync with Syncthing ([guide](docs/SYNC.md)) |
| Tether | Get online through your phone over Wi-Fi, USB or Bluetooth ([guide](docs/TETHER.md)) |
| Router | Plug in a laptop and send its traffic through a Tailscale exit node |
| Net Tools | Find devices, scan ports, trace routes, look up DNS, capture packets, share Ethernet |
| Car | Music in the car, as a USB stick or over Bluetooth ([guide](docs/CAR.md)) |
| ZClaw | AI chat with Claude, ChatGPT, Gemini, OpenRouter, Groq, Mistral or a local Ollama server, using your own API key ([guide](docs/ZCLAW.md)) |
| Dev Tools | Code editor and runner, REPL, Git, and a serial monitor for Arduino and ESP32 boards ([guide](docs/DEV.md)) |
| Games | NES, Game Boy and GBA, plus SNES, Mega Drive, PlayStation and more with libretro cores. Bring your own games ([guide](docs/GAMES.md)) |
| GPS | Position, saved places and track recording. Needs a GPS module ([guide](docs/EXPANSION.md)) |
| Mesh | Meshtastic text messages over LoRa. Needs the Cap LoRa-1262 ([guide](docs/EXPANSION.md)) |
| Grove | Drive a NeoPixel LED strip from the Grove port |
| RF Tools | Spectrum, scanner, weather sensors, aircraft, APRS, FT8 and more. Needs an RTL-SDR dongle |

## Troubleshooting

**Update says "Up to date" on 0.1.x but there's a newer release.** The device is still pointed at
the project's old address. [Updates](docs/UPDATE.md#it-says-up-to-date-but-a-newer-release-exists)
has a one-line fix.

**Apps won't install, or Terminal opens nothing, after updating from 0.1.x.** Open Update and
install the newest version, then open Apps and press **U**.

**"The package system needs repair first".** From 0.3.3 on, Update and Apps fix this themselves.
On older versions see [Updates](docs/UPDATE.md#it-says-the-package-system-needs-repair-first).

**Slow boot.** After a restart, run this to see what's taking the time:

```sh
systemd-analyze; systemd-analyze critical-chain zeroshell.service | head -12
```

## Guides

Using it:
[Keys](docs/KEYS.md) ·
[Apps](docs/APPS.md) ·
[Updates](docs/UPDATE.md) ·
[Battery and performance](docs/PERFORMANCE.md)

Apps:
[IR Remote](docs/IR.md) ·
[Remote](docs/REMOTE.md) ·
[Sync](docs/SYNC.md) ·
[Tether](docs/TETHER.md) ·
[Media and Jellyfin](docs/JELLYFIN.md) ·
[Car](docs/CAR.md) ·
[Camera](docs/CAMERA.md) ·
[Dock](docs/DOCK.md) ·
[GPS and LoRa](docs/EXPANSION.md) ·
[Dev Tools](docs/DEV.md) ·
[Games](docs/GAMES.md) ·
[ZClaw](docs/ZCLAW.md)

Development:
[Architecture](docs/ARCHITECTURE.md) ·
[Hardware notes](docs/HARDWARE.md) ·
[Hardware test checklist](docs/HARDWARE-TEST.md) ·
[Roadmap](docs/ROADMAP.md)

Hardware docs from M5Stack: [Cardputer Zero](https://docs.m5stack.com/en/CardputerZero)

## How it works

ZeroOS is Raspberry Pi OS (Debian trixie, arm64) with M5Stack's board drivers, cut down to what a
512 MB pocket computer needs. Instead of a desktop it boots straight into **ZeroShell**, a single
C++ program using [LVGL](https://lvgl.io) that draws the status bar, home screen and apps.

```
Apps          Built-in apps, plus installable apps loaded as modules (.so)
ZeroShell     Status bar, launcher, switching apps, routing key presses
Hardware      One interface for battery, sensors, radios, audio, IR, LEDs, power
Linux         NetworkManager, BlueZ, PipeWire, libcamera, systemd
Base image    Raspberry Pi OS trixie arm64, M5Stack board drivers
```

- **Apps are Debian packages.** Each one installs a module to `/usr/lib/zeroos/apps/<id>.so`
  plus whatever it depends on (Remote pulls in Tailscale, Sync pulls in Syncthing, and so on).
  The Apps app reads an index from the apps repository's latest GitHub release and installs
  through apt. Removing an app removes what it brought with it.
- **ZeroOS itself is one Debian package**, `zeroos`. The SD card image has it pre-installed, and
  the Update app installs a newer version of the same package, so a fresh card and an updated
  device end up with the same files.
- **Apps never talk to hardware directly.** They go through the hardware layer, which reads
  sysfs and runs standard tools (`nmcli`, `wpctl`, `ir-ctl` and so on). In the simulator the same
  interface returns fake readings, which is how every app can run on a PC and in CI.

The full picture is in [Architecture](docs/ARCHITECTURE.md).

## Building it yourself

### Run it on a PC

The home screen and every app run in a window on Linux:

```sh
sudo apt install cmake g++ libsdl2-dev
cmake -S shell -B build/sim -DZS_BACKEND=sim
cmake --build build/sim -j
./build/sim/zeroshell --zoom 4
```

There's also a browser version, which needs [Emscripten](https://emscripten.org):

```sh
web/build.sh build/zeroos-simulator.html
```

### Build for the device

Cross-compile ZeroShell for arm64:

```sh
sudo apt install g++-aarch64-linux-gnu
cmake -S shell -B build/device -DZS_BACKEND=device -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/aarch64-linux-gnu.cmake
cmake --build build/device -j
```

Build the app packages and the SD card image. The image needs
[Docker](https://docs.docker.com/get-docker/) and takes 1 to 2 hours:

```sh
image/apps/build.sh build/device/apps
image/build.sh build/device/zeroshell
```

No Linux machine? Run the
[Build image](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/actions/workflows/image.yml)
workflow from the Actions tab instead.

To push a build to a Cardputer that's already running ZeroOS:

```sh
tools/push-update.sh cardputer.local
```

### Tests

CI opens every app with the keyboard, with and without hardware present, and installs every app
with apt on Debian arm64. To run the keyboard tests locally:

```sh
cmake -S shell -B build/device-native -DZS_BACKEND=device && cmake --build build/device-native -j
gcc -shared -fPIC -o build/fakedev.so shell/tests/device/fakedev.c -ldl
python3 shell/tests/device/device_test.py build/device-native/zeroshell build/fakedev.so
python3 shell/tests/device/apps_test.py build/device-native/zeroshell build/fakedev.so
```

For a real device, go through the [hardware test checklist](docs/HARDWARE-TEST.md).

### Contributing

Pull requests are welcome. All CI checks have to pass before anything merges into `main`.
To change an app or add a new one, use
[CardPuter-Apps.4ME-U](https://github.com/hartj77-blip/CardPuter-Apps.4ME-U) instead of this
repository.

### Releases

Releases are made with the
[Release](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/actions/workflows/release.yml)
workflow: **Run workflow**, enter a version like `0.4.0`, and the update package, apps and SD card
image get built and attached to the release in about 15 minutes. See
[Publishing an update](docs/UPDATE.md#publishing-an-update).

## Security

Please report security problems privately, see [SECURITY.md](SECURITY.md).

## License

MIT, see [LICENSE](LICENSE). ZeroOS uses:

- [LVGL](https://lvgl.io) (MIT)
- [quirc](https://github.com/dlbeer/quirc) (ISC)
- IR codes from [Flipper-IRDB](https://github.com/Lucaslhm/Flipper-IRDB) (CC0)
- M5Stack's [pi-gen fork](https://github.com/CardputerZero/pi-gen) (BSD-3)
- M5Stack's [board drivers](https://github.com/m5stack/m5stack-linux-dtoverlays)
- The [Ubuntu font](https://design.ubuntu.com/font) (Ubuntu Font Licence)

Cardputer and M5Stack are trademarks of M5Stack and are only used here to name the hardware.
