# ZeroOS for the Cardputer Zero

ZeroOS is a custom operating system for the [M5Stack Cardputer Zero](https://docs.m5stack.com/en/CardputerZero),
the pocket computer with a keyboard, a 320x170 screen and a Raspberry Pi CM0 inside.

It is Debian Linux with a home screen made for the Cardputer: everything works from the keyboard,
it looks like a small Ubuntu desktop, and it supports all of the board's hardware (battery, motion
sensors, IR, radios, audio, camera, clock, key lights and the expansion ports).

> **Early project.** The apps work in the simulator and the image builds in CI, but much of it
> has not yet been tested on a real device. Expect rough edges, and please report what you find.
>
> ZeroOS is an unofficial community project. It is not made, endorsed or supported by M5Stack.

## Features

- **Keyboard first.** Arrow keys and Enter for everything, letters jump to apps, Esc goes back.
- **Install only what you need.** Seven apps come built in; the rest install from the **Apps** app.
- **Updates over the air.** New versions install from the **Update** app, with no re-flashing.
- **Long battery life.** The screen dims after 30 seconds and turns off after a minute on battery.
  Placing the device face down turns the screen off at once; picking it up turns it back on.
- **Desktop mode.** Plug in an HDMI monitor for a desktop with a web browser, video, terminal and files.

### Built-in apps

| App | What it does |
| --- | --- |
| **Apps** | Browse and install more apps |
| **Dock** | Desktop on an HDMI monitor: browser, video, terminal, files |
| **Files** | Browse the files on the card |
| **Settings** | Wi-Fi, Bluetooth, brightness, volume, power saving, restart, shut down |
| **Terminal** | A Linux console on the built-in screen |
| **Update** | Install a newer ZeroOS over the network |
| **Wi-Fi** | Find and join networks |

### Apps you can install

| App | What it does |
| --- | --- |
| **IR Remote** | Universal remote for TVs, projectors, audio, ACs and lights (about 2,000 remotes), plus learning your own |
| **Media** | Music player, music from a Jellyfin server, voice notes |
| **Notes** | Quick text notes |
| **Remote** | Tailscale: reach your other devices, SSH, remote desktop, file sharing |
| **Sync** | Keep notes, documents and recordings in step with your PC and phone (Syncthing) |
| **Tether** | Get online through your phone over Wi-Fi, USB or Bluetooth |
| **Camera** | Photos, gallery, and a QR code reader that can join Wi-Fi networks (models with a camera) |
| **GPS** | Position, saved places, track recording (needs a GPS module) |
| **Mesh** | Text messaging over LoRa with Meshtastic (needs the Cap LoRa-1262) |
| **Car** | Music in the car, as a USB stick or over Bluetooth |
| **Compass** | Compass and spirit level |
| **Battery** | Time left, power use, power profile |
| **Clock** | Clock and stopwatch |
| **System** | CPU and memory use |

Radio apps are available from [cardputer-zero-rf](https://github.com/hartj77-blip/cardputer-zero-rf).

## Install

1. Download the image:
   [zeroos-cardputerzero-arm64.img.xz](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/releases/latest/download/zeroos-cardputerzero-arm64.img.xz)
   (about 1.5 GB; a checksum is on the [releases page](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/releases/latest)).
2. Write it to a microSD card with [Raspberry Pi Imager](https://www.raspberrypi.com/software/)
   (*Choose OS > Use custom*) or [balenaEtcher](https://etcher.balena.io/). There's no need to unzip it first.
3. Put the card in the Cardputer Zero and switch it on. The first start takes a few minutes.

### First login

The user name is `pi` and the starting password is `raspberry`. The first time you log in, in the
**Terminal** app or over SSH (`ssh pi@cardputer.local`), you are asked to choose a new password.
If you set up an SSH key in Raspberry Pi Imager, you can skip this and change the password later with `passwd`.

## How to use it

| Key | Action |
| --- | --- |
| Arrow keys | Move |
| **Enter** | Open |
| **Esc** or **`** | Go back |
| A letter | Jump to an app on the home screen |
| **1** to **5** | Open the dock's apps from the home screen |

Some apps have their own keys, shown on screen. For example, in **Media** **Space** pauses and
**N** skips, and in **IR Remote** **L** learns a button from your own remote.

To get new versions, open **Update**. To add apps, open **Apps**.

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
- [Dock](docs/DOCK.md): desktop mode on a monitor
- [GPS and LoRa](docs/EXPANSION.md): expansion modules
- [Battery and performance](docs/PERFORMANCE.md)

For developers: [architecture](docs/ARCHITECTURE.md), [hardware notes](docs/HARDWARE.md), [roadmap](docs/ROADMAP.md).

## Try it on a PC

You can run the same home screen and apps in a window on Linux:

```sh
sudo apt install cmake g++ libsdl2-dev
cmake -S shell -B build/sim -DZS_BACKEND=sim
cmake --build build/sim -j
./build/sim/zeroshell            # add --zoom 4 for a bigger window
```

There is also a browser version (WebAssembly, needs [Emscripten](https://emscripten.org)):
`web/build.sh build/zeroos-simulator.html`.

## Build it yourself

```sh
sudo apt install g++-aarch64-linux-gnu
cmake -S shell -B build/device -DZS_BACKEND=device -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/aarch64-linux-gnu.cmake
cmake --build build/device -j
image/apps/build.sh build/device/apps        # the installable apps
image/build.sh build/device/zeroshell        # the SD card image; needs Docker, takes 1-2 hours
```

Or run the **Build image** workflow from the Actions tab. To send a build to a running device,
use `tools/push-update.sh cardputer.local` (see [docs/UPDATE.md](docs/UPDATE.md)).

## Security

To report a security problem, see [SECURITY.md](SECURITY.md).

## License

MIT. ZeroOS builds on [LVGL](https://lvgl.io) (MIT), [quirc](https://github.com/dlbeer/quirc) (ISC),
IR codes from [Flipper-IRDB](https://github.com/Lucaslhm/Flipper-IRDB) (CC0), M5Stack's
[pi-gen fork](https://github.com/CardputerZero/pi-gen) (BSD-3) and
[board drivers](https://github.com/m5stack/m5stack-linux-dtoverlays).

Cardputer and M5Stack are trademarks of M5Stack, used here only to name the hardware.
