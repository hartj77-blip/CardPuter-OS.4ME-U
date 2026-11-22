# ZeroOS for the Cardputer Zero

ZeroOS is a custom operating system for the [M5Stack Cardputer Zero](https://shop.m5stack.com/pages/m5-cardputerzero),
a pocket computer with a keyboard, a 320x170 screen and a Raspberry Pi CM0 inside.

It is Debian Linux with a home screen made for the Cardputer. Everything works from the keyboard,
it comes with a small set of built-in apps, and you install the rest as you need them.

ZeroOS is an unofficial community project. It is not made, endorsed or supported by M5Stack.

Quick links:

- [Download the latest release](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/releases/latest)
- [Install steps](#install)
- [Wi-Fi setup](#wi-fi)
- [Apps](#apps) and the [Apps guide](docs/APPS.md)
- [Speed and memory](#speed-and-memory)
- [All guides](#guides)
- [Report a problem](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/issues)
- [M5Stack's Cardputer Zero documentation](https://docs.m5stack.com/en/CardputerZero)

## Status

This is an early project. The apps work in the simulator and the image builds in
[CI](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/actions/workflows/ci.yml), but much of it
has not been tested on a real device yet. Expect rough edges, and please
[open an issue](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/issues) for anything you find.
The [roadmap](docs/ROADMAP.md) lists what is done and what is next.

## Features

- Keyboard navigation. Use the arrow keys and Enter for everything. Press a letter to jump to an
  app, and Esc to go back.
- Install only what you need. Four apps are part of ZeroOS. Every other app is installed, updated
  and removed in the Apps app. See the [Apps guide](docs/APPS.md).
- Updates over the air. New versions install from the Update app, with no re-flashing. See
  [Updates](docs/UPDATE.md).
- Power saving. On battery the screen dims after 30 seconds and turns off after a minute. Placing
  the device face down turns the screen off, and picking it up turns it back on. See
  [Battery and performance](docs/PERFORMANCE.md).
- HDMI desktop. Plug in a monitor for a desktop with a web browser, video player, terminal and
  file manager. See [Dock](docs/DOCK.md).

## Apps

### Built in

These are part of ZeroOS, so a device can always get online, update itself and install apps.

| App | What it does |
| --- | --- |
| Apps | Browse, install, update and remove apps ([guide](docs/APPS.md)) |
| Settings | Wi-Fi, Bluetooth, brightness, volume, power saving, restart and shut down |
| Update | Install a newer ZeroOS over the network ([guide](docs/UPDATE.md)) |
| Wi-Fi | Find and join wireless networks |

### Installable

Install these from the Apps app. Terminal, Files and Dock are already installed on the SD card
image, and can be removed like any other app.

| App | What it does |
| --- | --- |
| Terminal | A Linux terminal on the built-in screen |
| Files | Browse the files on the device |
| Dock | Desktop on an HDMI monitor, with a browser, video player, terminal and file manager ([guide](docs/DOCK.md)) |
| IR Remote | Universal remote for TVs, projectors, audio equipment, air conditioners and lights. About 2,000 remotes, and it can learn buttons from your own ([guide](docs/IR.md)) |
| Media | Music player, Jellyfin streaming and voice recordings ([guide](docs/JELLYFIN.md)) |
| Notes | Simple text notes |
| Remote | Tailscale: reach your other devices, SSH, remote desktop and file sharing ([guide](docs/REMOTE.md)) |
| Sync | Keep notes, documents and recordings in step with your PC and phone using Syncthing ([guide](docs/SYNC.md)) |
| Tether | Get online through your phone over Wi-Fi, USB or Bluetooth ([guide](docs/TETHER.md)) |
| Camera | Photos, a gallery and a QR code reader that can join Wi-Fi networks. Needs a model with a camera ([guide](docs/CAMERA.md)) |
| GPS | Position, saved places and track recording. Needs a GPS module ([guide](docs/EXPANSION.md)) |
| Mesh | Text messages over LoRa with Meshtastic. Needs the Cap LoRa-1262 ([guide](docs/EXPANSION.md)) |
| Car | Music in the car, as a USB stick or over Bluetooth ([guide](docs/CAR.md)) |
| Compass | Compass and spirit level |
| Battery | Time left, power use and power profiles ([guide](docs/PERFORMANCE.md)) |
| Clock | Clock and stopwatch |
| System | CPU and memory use |

## Install

1. Download the image from the [latest release](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/releases/latest):
   [zeroos-cardputerzero-arm64.img.xz](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/releases/latest/download/zeroos-cardputerzero-arm64.img.xz).
   It is about 1.5 GB. A checksum file (`.sha256`) is next to it on the release page.
2. Write it to a microSD card with [Raspberry Pi Imager](https://www.raspberrypi.com/software/)
   (Choose OS > Use custom) or [balenaEtcher](https://etcher.balena.io/). Both write the `.xz`
   file directly, so there is no need to unzip it first.
3. Put the card in the Cardputer Zero and switch it on. The first start takes a few minutes.

The image for a new release is built right after the release is published and appears about
10 minutes later. If the download link gives a "not found" page, try again shortly, or pick the
image from an earlier release on the [releases page](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/releases)
and then use the Update app.

### Wi-Fi

There are two ways to get online:

- On the device: open the Wi-Fi app, pick your network and type the password. The password shows
  as you type it, Backspace deletes, and Enter connects.
- Before the first start: in Raspberry Pi Imager, fill in your Wi-Fi name and password in the
  settings it offers before writing the card. ZeroOS applies them on the first boot.

You can also get online through your phone with the [Tether](docs/TETHER.md) app, or scan a Wi-Fi
QR code with the [Camera](docs/CAMERA.md) app.

### First login

| | |
| --- | --- |
| User name | `pi` |
| Password | `raspberry` |

The first time you log in, in the Terminal app or over SSH (`ssh pi@cardputer.local`), you are
asked to choose a new password. If you set up an SSH key in Raspberry Pi Imager, you can skip this
and change the password later with `passwd`.

## Controls

| Key | Action |
| --- | --- |
| Arrow keys | Move |
| Enter | Open the selected item |
| Esc or ` | Go back |
| A letter | Jump to an app on the home screen |
| 1 to 5 | Open the dock's apps from the home screen |

Some apps have their own keys, shown on screen. For example, in Media, Space pauses and N skips to
the next track. In IR Remote, L learns a button from your own remote.

To add apps, open Apps. To get a newer ZeroOS, open Update.

## Speed and memory

ZeroOS is tuned for the Cardputer Zero's 512 MB of RAM and small battery.

- Fast start. The screen's driver loads first, the CPU runs at full speed until the home screen
  is up, and services a pocket computer doesn't need are switched off.
- More room for apps. Memory that isn't in use is compressed (zram), so apps that wouldn't fit
  still run. If memory runs out, the biggest app is closed before the device freezes.
- Nothing heavy until you use it. The HDMI desktop and its browser use no memory until you open
  Dock or plug in a monitor.

To time a boot, run this in the Terminal app or over SSH after a restart:

```sh
systemd-analyze; systemd-analyze critical-chain zeroshell.service | head -12
```

For a full report on memory, CPU and battery use, run `zeroos-measure`. See
[Battery and performance](docs/PERFORMANCE.md) for details.

## Guides

- [Apps](docs/APPS.md): installing apps, and making your own
- [Updates](docs/UPDATE.md): updating over the network
- [IR Remote](docs/IR.md): the universal remote
- [Remote](docs/REMOTE.md): Tailscale, SSH and file sharing
- [Sync](docs/SYNC.md): syncing notes and files
- [Tether](docs/TETHER.md): getting online through your phone
- [Media and Jellyfin](docs/JELLYFIN.md): music from your server
- [Car](docs/CAR.md): music in the car
- [Camera](docs/CAMERA.md): photos and QR codes
- [Dock](docs/DOCK.md): the desktop on a monitor
- [GPS and LoRa](docs/EXPANSION.md): expansion modules
- [Battery and performance](docs/PERFORMANCE.md): power saving and memory

For developers:

- [Architecture](docs/ARCHITECTURE.md)
- [Hardware notes](docs/HARDWARE.md)
- [Hardware test checklist](docs/HARDWARE-TEST.md): one line per app to try on a real Cardputer
- [Roadmap](docs/ROADMAP.md)

## Run it on a PC

You can run the same home screen and apps in a window on Linux, using the SDL simulator.

```sh
sudo apt install cmake g++ libsdl2-dev
cmake -S shell -B build/sim -DZS_BACKEND=sim
cmake --build build/sim -j
./build/sim/zeroshell
```

Add `--zoom 4` for a bigger window.

There is also a browser version (WebAssembly). Building it needs [Emscripten](https://emscripten.org):

```sh
web/build.sh build/zeroos-simulator.html
```

## Tests

Every pull request opens each app with the keyboard, with and without hardware, and installs each
app with `apt` on Debian arm64. To run the keyboard tests on Linux:

```sh
cmake -S shell -B build/device-native -DZS_BACKEND=device && cmake --build build/device-native -j
gcc -shared -fPIC -o build/fakedev.so shell/tests/device/fakedev.c -ldl
python3 shell/tests/device/device_test.py build/device-native/zeroshell build/fakedev.so
python3 shell/tests/device/apps_test.py build/device-native/zeroshell build/fakedev.so
```

To check a real Cardputer by hand, use the [hardware test checklist](docs/HARDWARE-TEST.md).

## Build from source

Install the cross-compiler and build the device version:

```sh
sudo apt install g++-aarch64-linux-gnu
cmake -S shell -B build/device -DZS_BACKEND=device -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/aarch64-linux-gnu.cmake
cmake --build build/device -j
```

Build the installable apps and the SD card image. The image needs [Docker](https://docs.docker.com/get-docker/)
and takes 1 to 2 hours.

```sh
image/apps/build.sh build/device/apps
image/build.sh build/device/zeroshell
```

You can also build an image without a PC by running the
[Build image](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/actions/workflows/image.yml)
workflow from the Actions tab.

To send a build to a device that is already running ZeroOS:

```sh
tools/push-update.sh cardputer.local
```

See [Updates](docs/UPDATE.md) for details.

Every change is built and tested by the [CI](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/actions/workflows/ci.yml)
workflow. Tagged versions are published by the
[Release](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/actions/workflows/release.yml) workflow.

## Security

To report a security problem, see [SECURITY.md](SECURITY.md).

## License

ZeroOS is released under the [MIT license](LICENSE). It builds on:

- [LVGL](https://lvgl.io) (MIT)
- [quirc](https://github.com/dlbeer/quirc) (ISC)
- IR codes from [Flipper-IRDB](https://github.com/Lucaslhm/Flipper-IRDB) (CC0)
- M5Stack's [pi-gen fork](https://github.com/CardputerZero/pi-gen) (BSD-3)
- M5Stack's [board drivers](https://github.com/m5stack/m5stack-linux-dtoverlays)

Cardputer and M5Stack are trademarks of M5Stack, used here only to name the hardware.
