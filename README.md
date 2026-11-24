# ZeroOS for the Cardputer Zero

IM CONSTANTLY WORKING ON THIS SO IF YOU HAVE A GOOD WORKING VERSION DONT UPDATE -IT COULD BE BETTER THO... README WILL BE UPDATED AS OFTEN AS I CAN. IF YOU USE IT - REPORT ISSUES - MAKE SUGGESTIONS - ILL SEE WHAT I CAN DO.

ZeroOS is an operating system for the [M5Stack Cardputer Zero](https://shop.m5stack.com/pages/m5-cardputerzero),
the pocket computer with a keyboard, a 320x170 screen and a Raspberry Pi CM0 inside.

Under the hood it's Debian Linux (Raspberry Pi OS trixie, arm64). On top is a home screen built
for that tiny screen and keyboard: no mouse, no touch, everything from the keys. It comes with the
basics (Wi-Fi, Settings, Update, Apps, plus Terminal, Files and Dock) and you install the rest
from the device when you want them.

It only runs on the Cardputer Zero. The original Cardputer and the Cardputer ADV have an ESP32
inside, not a Raspberry Pi, and can't run it.

This is a community project. It isn't made, endorsed or supported by M5Stack.

- [Download](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/releases/latest)
- [Try it in your browser](https://hartj77-blip.github.io/emu-cardputer-os.4me-u/) (the real home screen and apps, simulated hardware)
- [Report a problem](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/issues)
- [The apps repository](https://github.com/hartj77-blip/CardPuter-Apps.4ME-U)

## Contents

1. [Status: what is tested and what isn't](#status-what-is-tested-and-what-isnt)
2. [Install](#install)
3. [Using it](#using-it)
4. [Apps](#apps)
5. [Troubleshooting](#troubleshooting)
6. [How it works](#how-it-works)
   - [The layers](#the-layers)
   - [What happens at boot](#what-happens-at-boot)
   - [ZeroShell, the home screen](#zeroshell-the-home-screen)
   - [How a key press travels](#how-a-key-press-travels)
   - [How word suggestions work](#how-word-suggestions-work)
   - [How long lists stay fast](#how-long-lists-stay-fast)
   - [The watchdog: a frozen screen restarts itself](#the-watchdog-a-frozen-screen-restarts-itself)
   - [The hardware layer](#the-hardware-layer)
   - [How apps are loaded](#how-apps-are-loaded)
   - [How apps are installed](#how-apps-are-installed)
   - [How new apps reach the home screen](#how-new-apps-reach-the-home-screen)
   - [How ZeroOS updates itself](#how-zeroos-updates-itself)
   - [How the SD card image is built](#how-the-sd-card-image-is-built)
   - [Memory, CPU and battery](#memory-cpu-and-battery)
   - [Versions](#versions)
7. [Security and what you are trusting](#security-and-what-you-are-trusting)
8. [Testing](#testing)
9. [Repository layout](#repository-layout)
10. [Building it yourself](#building-it-yourself)
11. [Contributing](#contributing)
12. [Releases](#releases)
13. [Guides](#guides)
14. [License and third-party code](#license-and-third-party-code)

## Status: what is tested and what isn't

ZeroOS is young. The first commit was on 13 November 2026 and the newest release is 0.6.0. It is
being written faster than it can be tried on hardware, so this section says plainly where things
stand.

| | State |
| --- | --- |
| Builds for arm64, the PC simulator and the browser | Yes, on every pull request |
| Every app opens without crashing, with and without hardware present | Yes, in CI, on a PC with a fake screen and keyboard |
| Every app installs and removes cleanly with the real apt on Debian trixie arm64 | Yes, in CI, in a container |
| Update logic: find, download, verify, install, roll back | Yes, in CI, with stand-ins for `dpkg` and `systemctl` |
| SD card image builds | Yes, for every release, on GitHub's arm64 runner |
| Boots and runs on a real Cardputer Zero | Partly. Owners have run it; most apps have not been checked on the device yet |
| Watchdog restart and new apps on home after a terminal install | In CI on a PC (`shell/tests/watch_test.py`). Not yet seen on the device |
| Word suggestions while typing | In CI: unit tests, and a word finished with Tab in Notes on the fake keyboard. Not yet tried on the device's own keyboard |
| Battery life, boot time, memory use on the device | Not measured yet. Numbers in this README marked "estimate" are estimates |

In short: the software is checked heavily by machines, but a lot of it has not been touched by a
person holding the device. If something doesn't work on yours,
[open an issue](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/issues) and say which version
you're on (`cat /etc/zeroos-release`). The [hardware test checklist](docs/HARDWARE-TEST.md) is
the list of things still to confirm, and the [roadmap](docs/ROADMAP.md) has what's planned.

## Install

You need a microSD card and a computer to write it from.

1. Download [zeroos-cardputerzero-arm64.img.xz](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/releases/latest/download/zeroos-cardputerzero-arm64.img.xz)
   from the [latest release](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/releases/latest).
   It's about 1.2 GB.
2. Optionally, check the download. The release has a `.sha256` file next to the image. In the
   folder you downloaded both to:

   ```sh
   sha256sum -c zeroos-cardputerzero-arm64.img.xz.sha256
   ```

   On a Mac, use `shasum -a 256 -c` instead. It should print `OK`.
3. Write it to the card with [Raspberry Pi Imager](https://www.raspberrypi.com/software/)
   (Choose OS > Use custom) or [balenaEtcher](https://etcher.balena.io/). Both take the `.xz`
   file as it is, no need to unzip it.
   In Raspberry Pi Imager, pick Raspberry Pi Zero 2 W as the device (the CM0 is the same chip).
4. Put the card in the Cardputer and switch it on. The first boot takes a few minutes, because the
   card's filesystem is grown to fill the card. Boot text is shown on the screen while it runs.

The image is built after each release is published, so for about 10 minutes after a new release
the download link can show "not found". Wait a bit, or grab the image from an
[older release](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/releases) and update from
the device afterwards.

### Getting online

- On the device: open Wi-Fi, pick your network, type the password and press Enter.
- Before the first boot: in Raspberry Pi Imager, fill in your Wi-Fi name and password in its
  settings before writing the card. ZeroOS picks them up on first boot.
- Through your phone: use the [Tether](docs/TETHER.md) app (Wi-Fi hotspot, USB or Bluetooth).
- With a QR code: scan a Wi-Fi QR code with the [Camera](docs/CAMERA.md) app.
- With a cable: the Cardputer Zero has an Ethernet port. Plug it in and it is used.

### Logging in

The user is `pi` and the password is `raspberry`. SSH is on. The first time you log in, from the
Terminal app or over SSH (`ssh pi@cardputer.local`), you have to pick a new password before you
can do anything else. If you added an SSH key in Raspberry Pi Imager you can skip that and run
`passwd` later. How this works is explained under
[Security](#security-and-what-you-are-trusting).

You can also set your own username and password in Raspberry Pi Imager's settings before writing
the card; ZeroOS then runs as that user instead of `pi`. This needs a version newer than 0.4.0
(see [Troubleshooting](#troubleshooting)).

## Using it

Arrow keys move, Enter opens, Esc goes back. The \` key also works as Esc. On the built-in
keyboard the arrows are Fn + F, Z, X and C.

| Key | What it does |
| --- | --- |
| Esc | Back. Hold it to go to the home screen from anywhere |
| Ctrl + Space | Taskbar panel: Wi-Fi, Bluetooth, volume, brightness, battery |
| 1 to 9 | Pick an item. In a list, the 1st to 9th row, like a phone menu (when you're not typing) |
| 0 | Show the keys for the current screen |
| Tab | Menu of everything you can do on the current screen (on screens using the new layout) |
| Ctrl + a letter | Run the menu item with that letter without opening the menu |
| Fn + J (PRSC) | Screenshot of the screen, saved as a PNG in `~/Pictures/Screenshots` |
| Fn + H (HELP) | Same as 0: the keys for the current screen |
| Hold the top-right button | Screen off (LOCK); the next key only wakes it |
| Letters | In a list, find a row: only the rows that match stay |
| Tab, while typing | Finish the word: types the first suggestion shown in the top bar. See [Typing](#typing-word-suggestions) |

On the home screen:

| Key | What it does |
| --- | --- |
| Letters | Search apps, settings and actions ("wifi", "bright", "mute", "music", "bt"). The best match is first, and Enter opens it |
| 1 to 7 | Open the pinned apps. While searching, 1 to 9 pick a result |
| Esc | Clear the search |
| Tab | Open All apps (every app, A to Z). There, Tab on an app, P, then 1 to 7 pins it on that key |

Ctrl, Shift, Fn and Sym are sticky: tap one and then press the next key. Apps show their own keys
on screen. For example, in Media Space pauses and M skips to the next track, and in IR Remote L learns a button from
another remote. The full list is in [Keys](docs/KEYS.md), and the reasoning behind the layout is
in [UX](docs/UX.md).

A USB or Bluetooth keyboard works too. ZeroShell reads every keyboard it finds, including ones
plugged in after boot.

Installing apps: open Apps, pick one and press Enter. The list includes
[M5Stack's app store](docs/APPS.md#m5stacks-app-store) too (marked M5): about 180 community apps
made for M5's launcher, which ZeroOS opens full screen (hold Esc for 3 seconds to leave one).
Update all at the top of the list (or Ctrl + U) updates everything.

Updating ZeroOS: open Update. No re-flashing needed, see [Updates](docs/UPDATE.md) and
[How ZeroOS updates itself](#how-zeroos-updates-itself).

### Typing: word suggestions

The keyboard is small, so ZeroOS finishes words for you. It works in every text box: Notes, the AI
and Mesh chats, Git commit messages, names and searches, in ZeroOS's own screens and in installed
apps.

1. Start typing a word. After two letters, up to three ways to finish it appear at the left of the
   top bar, next to an orange **Tab** label. The first one is brighter.
2. Press Tab. The rest of that word is typed, with a space after it, so you can go straight on to
   the next word.
3. If it was the wrong one, press Tab again: the word is swapped for the next suggestion, and
   again for the third. Or just keep typing letters and the list narrows.

| Key | What it does while suggestions show |
| --- | --- |
| Tab | Type the bright suggestion and a space |
| Tab again | Swap it for the next suggestion |
| Backspace, straight after Tab | Take the whole suggestion back, leaving what you typed |
| . , ! ? ; : straight after Tab | The space Tab typed is removed, so you get `word.` and not `word .` |
| Any other key | Carry on typing as normal |

A worked example, in Notes: type `I wan`, press Tab (`I want `), type `to mee`, press Tab
(`I want to meet `), type `tom`, press Tab, then `.`, and the note reads `I want to meet tomorrow.`
That is 18 key presses for 24 characters.

Things to know:

- **Capitals are kept.** `Th` offers `That`, and `TH` offers `THAT`.
- **It learns your words.** A word of four letters or more that you type twice is offered from then
  on, ahead of the built-in words, and the words you use most come first. Names, places and
  technical words are picked up this way. It keeps the 200 words you use most.
- **Tab is still the menu.** Tab only finishes a word while suggestions are showing. With none on
  screen (after a space, or in an empty box) Tab opens the screen's menu as before, and Ctrl + a
  letter always works. Holding Tab still locks the screen.
- **Passwords are never suggested or learned.** Password boxes are skipped completely. So is
  anything that isn't a plain word: web addresses, email addresses, file paths, numbers and command
  options are left alone.
- **The Terminal is separate.** It is a real Linux shell, which has its own Tab completion for
  commands and file names.
- **Home search and list filters** already find things as you type, so they have no suggestions.

To turn it off: Settings, Display, Word suggestions. Or type `suggest` on the home screen and press
Enter on "Turn word suggestions off". To wipe the words it has learned, type `forget` on the home
screen and choose "Forget learned words". The learned words are in
`~/.config/zeroos/words.learned`, one word and its count per line, readable only by you; you can
edit or delete the file.

It costs almost nothing: no background work, about 6 KB for the built-in words and at most 6 KB
for yours. [How word suggestions work](#how-word-suggestions-work) has the details.

### Battery and memory

The Cardputer Zero has 512 MB of RAM and a 1750 mAh battery, and the Raspberry Pi chip has no
sleep mode, so ZeroOS is set up for that:

- On battery the screen dims after 30 seconds and turns off after a minute. Turn the device face
  down to switch the screen off, pick it up to switch it back on. The first key press after the
  screen goes off only wakes it; it isn't passed to the app.
- At 15% battery the power saving profile turns on by itself. At 3% the device shuts down cleanly
  so nothing is lost.
- Unused memory is compressed (zram), so bigger apps still fit. If memory does run out, the
  biggest app is closed instead of the whole device locking up. The home screen, SSH, networking
  and audio are never the ones closed.
- Settings > Running programs (or type "kill" on home) lists what is running as you, biggest
  first, with its memory and CPU: programs started from Terminal, M5Stack apps that keep going in
  the background, a player. Enter stops one (it gets 1.5 seconds to save and quit, then it is
  killed); Ctrl + K stops them all. The home screen and the services it needs aren't listed, and
  system services can't be stopped from there.
- Services a pocket computer doesn't need are off, and the HDMI desktop uses no memory until you
  open Dock or plug in a monitor.

Run `zeroos-measure` in a terminal for a memory, CPU and battery report. The details are under
[Memory, CPU and battery](#memory-cpu-and-battery) and in
[Battery and performance](docs/PERFORMANCE.md).

## Apps

### Built in

These four are part of ZeroOS itself and can't be removed, so a device can always get online,
update and install apps. Bluetooth is also built in, but lives inside Settings.

| App | What it does |
| --- | --- |
| Apps | Browse, install, update and remove apps ([guide](docs/APPS.md)) |
| Settings | Wi-Fi, Bluetooth, brightness, volume, power saving, running programs, restart, shut down |
| Update | Install a newer ZeroOS over the network ([guide](docs/UPDATE.md)) |
| Wi-Fi | Find and join networks |

### Installable

Everything else comes from the Apps app. Terminal, Files and Dock are already on the SD card
image, but you can remove them like anything else. The code for these lives in
[CardPuter-Apps.4ME-U](https://github.com/hartj77-blip/CardPuter-Apps.4ME-U). Each one is a
separate package with its own version, so updating one app never touches another.

| App | What it does |
| --- | --- |
| Terminal | Linux terminal on the built-in screen |
| Files | Browse files on the device |
| Notes | Quick text notes |
| Clock | Clock and stopwatch |
| Compass | Compass and spirit level |
| Battery | Time left, power draw and power profiles ([guide](docs/PERFORMANCE.md)) |
| System | CPU, memory, storage and temperature |
| Media | Music from the SD card, USB drives and server shares, in a classic MP3-player look, with other looks to switch to (T) |
| Jellyfin | Music, movies and shows from your Jellyfin server ([guide](docs/JELLYFIN.md)) |
| Recorder | Voice notes: record, play back, rename, delete |
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
| AI | Chat with Claude, ChatGPT, Gemini, OpenRouter, Groq, Mistral or a local Ollama server, using your own API key ([guide](docs/AI.md)) |
| Dev Tools | Code editor and runner, REPL, Git, and a serial monitor for Arduino and ESP32 boards ([guide](docs/DEV.md)) |
| Games | NES, Game Boy and GBA, plus SNES, Mega Drive, PlayStation and more with libretro cores. Bring your own games ([guide](docs/GAMES.md)) |
| GPS | Position, saved places and track recording. Needs a GPS module ([guide](docs/EXPANSION.md)) |
| Mesh | Meshtastic text messages over LoRa. Needs the Cap LoRa-1262 ([guide](docs/EXPANSION.md)) |
| Grove | Drive a NeoPixel LED strip from the Grove port |
| RF Tools | Spectrum, scanner, weather sensors, aircraft, APRS, FT8 and more. Needs an RTL-SDR dongle |

Apps that need extra software bring it with them and take it away again when removed: Remote
installs Tailscale, Sync installs Syncthing, Camera installs libcamera, Mesh installs
meshtasticd, GPS installs gpsd. None of that is on the base image.

## Troubleshooting

Update says "Up to date" on 0.1.x but there's a newer release. The device is still pointed at
the project's old address. [Updates](docs/UPDATE.md#it-says-up-to-date-but-a-newer-release-exists)
has a one-line fix.

Apps won't install, or Terminal opens nothing, after updating from 0.1.x. Open Update and
install the newest version, then open Apps and choose Update all.

"The package system needs repair first". From 0.3.3 on, Update and Apps fix this themselves.
On older versions see [Updates](docs/UPDATE.md#it-says-the-package-system-needs-repair-first).

The home screen never starts, and every login asks for a new password, after setting a username
in Raspberry Pi Imager. Versions up to 0.4.0 only work with the user `pi`; this is fixed from
0.5.0. To fix a card that's already like this, log in over SSH with your username
(`ssh yourname@cardputer.local`; if it asks for a new password, choose one), run
`sudo zeroos-update install`, then restart. Or write the card again without setting a username in
Imager, and change the password after the first login.

Apps are missing after updating from 0.4.x to 0.5.0. That update takes the old apps off and puts
the new builds back; if Wi-Fi was down at that moment, they stay off. Run
`sudo zeroos-apps upgrade` from the Terminal or SSH, which puts them back. From 0.6.0, **Update all** in Apps counts them too and does the same.

The Wi-Fi list says "No networks found" and nothing connects. Wi-Fi may be off: with **Wi-Fi off
with screen** on (Settings), a restart while the screen was off used to leave it off. Press
**Tab > W** in the Wi-Fi list (from 0.6.0 the list says when Wi-Fi is off, and
it comes back on by itself after a restart). Over SSH, `nmcli radio wifi on` does the same.

The screen stays black. ZeroShell logs why it couldn't start. Over SSH:

```sh
journalctl -u zeroshell -b | tail -50
```

Slow boot. After a restart, run this to see what's taking the time:

```sh
systemd-analyze; systemd-analyze critical-chain zeroshell.service | head -12
```

The screen froze, then the home screen came back on its own a few seconds later. That is the
[watchdog](#the-watchdog-a-frozen-screen-restarts-itself) restarting a stuck ZeroShell. It is a
bug worth reporting: the journal says what was open (`zeroshell: open <app>`) just before
systemd's `Watchdog timeout` line. Send the output of
`journalctl -u zeroshell -b | grep -B20 -i watchdog`.

An app installed from a terminal isn't on the home screen. It should appear within about two
seconds on the home screen, or the next time you go home from another app
([how](#how-new-apps-reach-the-home-screen)). If it doesn't, check it's really there
(`zeroos-apps list`) and look for `apps changed` in `journalctl -u zeroshell -b`.

A key does nothing. ZeroShell can log every raw key it receives. Run
`sudo systemctl edit zeroshell`, add `[Service]` and `Environment=ZS_KEYLOG=1`, restart it, press
the key, then read `journalctl -u zeroshell -b | grep 'zeroshell: key'`. Undo it with
`sudo systemctl revert zeroshell`.

A failed boot. Logs are kept on the card (capped at 24 MB), so you can read them from another
computer: `journalctl -D <card>/var/log/journal -b`.

## How it works

This section describes what the code actually does, with the file each part lives in, so you can
check any of it yourself.

### The layers

ZeroOS is Raspberry Pi OS (Debian trixie, arm64) with M5Stack's board drivers, cut down to what a
512 MB pocket computer needs. Instead of a desktop it boots straight into ZeroShell, a single C++
program that draws the status bar, home screen and apps with [LVGL](https://lvgl.io).

```
Apps          Built-in apps, plus installable apps loaded as modules (.so)
ZeroShell     Status bar, launcher, switching apps, routing key presses
Hardware      One interface for battery, sensors, radios, audio, IR, LEDs, power
Linux         NetworkManager, BlueZ, PipeWire, libcamera, systemd
Base image    Raspberry Pi OS trixie arm64, M5Stack board drivers
```

Each layer only talks to the one below it. Apps never open a device file or run a system tool
themselves; they ask the hardware layer. That one rule is what lets the same apps run in a window
on a PC, in a browser and in CI, with simulated hardware behind them.

| Layer | Code |
| --- | --- |
| Apps | `shell/src/apps/` (the built-in ones), the rest in the apps repository |
| ZeroShell | `shell/src/ui/`, `shell/src/main.cpp` |
| Screen and keyboard | `shell/src/platform/` |
| Hardware layer | `shell/src/hal/` |
| IR code library | `shell/src/ir/` (no LVGL, tested on its own) |
| System files, services, scripts | `image/stage-zeroos/` |
| Image build | `image/build.sh` |

### What happens at boot

1. The Raspberry Pi firmware reads `config.txt`. ZeroOS sets `boot_delay=0`, `disable_splash=1`,
   `display_auto_detect=0` (there's no DSI panel to probe for) and `gpu_mem=32` (there's no 3D
   desktop, so most of the GPU's memory goes back to Linux).
2. The kernel starts with the CPU governor on `performance` for the boot only, the Wi-Fi region
   set from the image's country, and the console left on, so boot text shows on the LCD instead
   of a black screen.
3. systemd loads the LCD's driver chain first, in order (`/etc/modules-load.d/zeroos-lcd.conf`:
   I2C bus, M5's IO expander, backlight, SPI bus, M5's panel driver). Left to udev, the LCD came
   up about 15 seconds into the boot, behind every other device.
4. `zeroshell.service` starts once the user session for uid 1000 is up (PipeWire runs there, and
   volume and mute need it). Before ZeroShell itself, a small root script,
   `/usr/lib/zeroos/power-perms`, lets the `video` group change the CPU governor and blank the
   panel. That's the only part of starting the UI that runs as root.
5. ZeroShell starts as uid 1000, not root, on `tty1`. It puts the console into graphics mode with
   the keyboard off, so the console never draws over the UI and keys never pile up in a login
   prompt behind it. It looks for the LCD's framebuffer (`/dev/fb_lcd`, which is `fb0`, or `fb1`
   if an HDMI monitor was connected at boot), waits up to 10 seconds for it, and exits with the
   reason in the journal if it never appears. systemd restarts it a second later.
6. It sets the CPU governor for the saved power profile, opens every keyboard it finds, loads the
   installed app modules and draws the home screen.

The service is in `image/stage-zeroos/files/zeroshell.service`. The image build changes that
make this order work, and the owner reports behind each one, are listed in
[Hardware notes](docs/HARDWARE.md#known-issues-from-early-owners-and-what-the-image-does).

### ZeroShell, the home screen

ZeroShell is one C++17 program. Facts about it:

- LVGL 9.2.2, pinned in `shell/CMakeLists.txt` and fetched at build time, 16-bit colour. UI
  objects are allocated with the C library's `malloc` (`LV_USE_STDLIB_MALLOC` in
  `shell/lv_conf.h`). Up to 0.4.0 LVGL used its own fixed 512 KB pool; building the Apps list
  with M5Stack's ~180 apps used it all up and ZeroShell froze. Two changes fix that from both
  sides: LVGL now draws on the system's memory, and lists only build the rows on screen
  ([How long lists stay fast](#how-long-lists-stay-fast)), so the Apps list no longer needs much
  memory in the first place.
- A failed LVGL assertion calls `abort()` (`LV_ASSERT_HANDLER` in `shell/lv_conf.h`). It used to
  spin forever (`while(1);`), which froze the screen and ignored systemd's stop signal. Now the
  process ends and systemd starts it again.
- About 22,000 lines of C++ in `shell/src/` (UI 5,000, hardware layer 5,100, apps that are still
  in this repository 10,800, screen and keyboard 900, IR library 700). The rest of the apps are in
  the apps repository.
- One binary, four ways to build it, chosen with `-DZS_BACKEND`:

  | Backend | Screen | Keyboard | Hardware | Used for |
  | --- | --- | --- | --- | --- |
  | `device` | The LCD's Linux framebuffer | evdev | Real (`LinuxHal`) | The Cardputer |
  | `sim` | An SDL2 window | The PC's | Simulated (`MockHal`) | Development on a PC |
  | `headless` | Image files | A script of keys | Simulated | CI screenshots and tests |
  | `web` | A canvas, via WebAssembly | The browser's | Simulated | The browser demo |

- The main loop sleeps until the next LVGL timer or key is due, instead of waking on a fixed
  tick. Apps get a `tick()` call about 10 times a second. While the screen is off the redraw
  timers stop entirely.
- User settings are plain `key=value` lines in `~/.config/zeroos/settings.conf`
  (`shell/src/ui/prefs.cpp`), written as soon as you change something.
- When systemd stops it (an update, a restart, shutdown), it closes the open app first, so Notes
  saves and a GPS track is finished, then restores `tty1` (`shell/src/main.cpp`).

An app is a C++ class. This is the whole interface, from `shell/src/ui/app.h`:

```cpp
class App {
public:
    virtual ~App() = default;
    virtual void build(lv_obj_t* root) = 0;           // draw into a 320x150 area
    virtual void tick() {}                             // about 10 times a second
    virtual bool on_key(uint32_t key) { return false; }  // true = handled
    virtual bool on_back() { return false; }           // Esc: true = handled inside the app
    lv_group_t* group = nullptr;
};
```

Apps get a small set of services from the shell, all declared in the same header:
`shell_toast()` for a message, `shell_open_app(id, arg)` to open another app,
`shell_confirm()` to ask before something that can't be undone, and `shell_set_actions()` to
say what the screen can do. That last one is a list of actions (label, letter, key, function);
the shell draws the soft-key bar along the bottom, the Tab menu and the Ctrl + letter shortcuts
from that one list, so the hints on screen can't disagree with what the keys do.

Apps reach each other only through `shell_open_app()`, never by calling another app's code,
because the other app might not be installed. Opening an app from inside a key handler is
deferred until that handler returns, so an app can never be destroyed while its own code is
still running.

Some things need the whole screen and aren't LVGL: the Linux terminal, M5Stack's camera program
and apps from M5Stack's store. For those ZeroShell hands the screen over (it stops drawing and
gives up the keyboard), starts the program, and takes the screen back when the program exits.

### How a key press travels

1. The keyboard is a TCA8418 key matrix. M5Stack's kernel driver reports it as an ordinary evdev
   input device. It handles Shift, Fn and Sym itself (tap once for the next key, tap twice to
   lock) and sends a different key code for each layer.
2. ZeroShell reads every keyboard directly from `/dev/input`, not through LVGL's driver, because
   LVGL's evdev driver drops letters and digits. It checks for newly plugged-in keyboards every
   couple of seconds, and each keyboard gets its own translator so a modifier held on one
   doesn't affect another.
3. `shell/src/platform/keys.cpp` turns evdev codes into characters and navigation keys. It has
   no LVGL or kernel headers, so it's unit tested on any machine (`shell/tests/keys_test.cpp`).
   Ctrl is a plain key in M5's driver; ZeroShell makes a tapped Ctrl apply to the next key, like
   the other modifiers.
4. The kernel doesn't repeat held keys on this board (the device tree lacks autorepeat), so
   ZeroShell repeats them itself.
5. The shell offers the key to the open app's `on_key()` first. If the app doesn't take it, the
   shell's own keys get a turn (Esc for back, the volume and brightness keys, number keys to
   pick a row, Tab for the menu). Only then does it reach the LVGL widget in focus. That order is
   what lets an app use plain letters as shortcuts while text boxes still type normally.
6. One thing comes before the app: if a text box has the focus and word suggestions are showing,
   Tab finishes the word (next section). With no suggestions showing, Tab goes down the list
   above as usual.

### How word suggestions work

The feature is part of ZeroShell, not of any app, so every text box gets it, including those in
installed apps, and no app had to change (the app level, `ZS_APP_ABI`, stays at 4).

| Piece | File | What it does |
| --- | --- | --- |
| The engine | `shell/src/ui/suggest.cpp` | Finds the word being typed, looks it up, learns words. No LVGL, so it is unit tested on any machine |
| The built-in words | `shell/src/ui/suggest_words.cpp` | About 900 everyday English words plus the words this device uses (battery, bluetooth, wireless...), most common first |
| The bar | `shell/src/ui/suggest_bar.cpp` | Shows the suggestions over the status bar, handles Tab and Backspace, saves learned words |

What happens on a key press in a text box:

1. `Shell::handle_key()` passes the key to `suggest_key()` together with the text box in focus.
   The first time a text box is typed in, the bar attaches one event listener to it; LVGL frees
   it with the box.
2. LVGL types the character and the listener hears that the text changed. It reads the word just
   before the cursor. A word is letters and apostrophes; it must come after the start of the
   text, a space, a new line or an opening bracket or quote. That rule is what keeps
   `192.168.we`, `me@exam`, `/home/pi/Doc` and `--verb` from being treated as words.
3. With two or more letters, the engine collects up to three words that start with them: first
   your learned words (those typed at least twice, most used first), then the built-in list in
   its own order. The built-in list is one string in the program's read-only data, scanned from
   the start; at about 6 KB that takes a few microseconds, so there is no index to build or keep.
4. The bar shows them at the left of the status bar, as wide as the words need, so the battery
   and Wi-Fi icons on the right stay visible.
5. Tab types the rest of the word and a space. The listener ignores changes the bar makes itself,
   but the app's own listeners still hear them, so Notes autosaves as usual.

Learning: when the next character ends a word (a space, a new line, punctuation, or Enter in a
one-line box), that word's count goes up by one. A word accepted with Tab is counted once the next
key shows it was kept. Words shorter than four letters aren't stored (finishing them saves no
key presses), and neither is anything with a digit or symbol in it. The list holds 200 words at
28 bytes each; when it is full, the least used word makes room, the oldest first.

Saving: learned words are written to `~/.config/zeroos/words.learned` no sooner than 30 seconds
after a change, and before a shutdown, so typing never causes a stream of card writes. The write
is atomic (a temporary file, then a rename), so a power cut leaves the old file intact. At most
the last 30 seconds of learning can be lost.

Left alone on purpose: password boxes (LVGL's password mode), a text box an app has marked with
`kNoSuggestFlag` (`shell/src/ui/suggest_bar.h`), the home search and list filters (not text
boxes; they match as you type already), and the Terminal (a real console, outside ZeroShell).

### How long lists stay fast

The list screens (Apps, All apps, search results, Settings, Wi-Fi, Bluetooth, and the tool lists
in Net Tools and RF Tools) all use one class, `ListView` (`shell/src/ui/list_view.h` and
`list_view.cpp`). With M5Stack's store added, the Apps list has around 200 rows, and it keeps
growing.

Until version 0.4.0 every row was a set of LVGL widgets: the row, its number badge and the
badge's digit, an icon, the name and the value on the right. Six objects per row, so about 1,200
for the Apps list, all made when the list opened, all measured for layout, and all kept in
memory while it was open. On the Cardputer's CPU that was a visible pause, and it got slower with
every app M5 added.

Now a row is only data: its icon, text, value, colour and what Enter does. `add()` stores that
and nothing else. When the list is built it makes one set of widgets, a slot, for each line that
fits on the screen (five whole lines and part of a sixth on the Apps screen), and that is all it
ever makes. Moving down past the last line doesn't scroll anything: the slots stay where they are
and the next rows' text is written into them (`bind()`). So:

- Opening a 200-row list costs the same as opening a 6-row one: about 36 LVGL objects instead of
  about 1,200. `add()` for 200 rows is 200 small copies of strings.
- A key press rewrites at most six slots' labels. Typing to filter walks the stored rows' text,
  which is plain string matching, then rewrites the same six slots.
- The last, partly visible line is drawn but can't take the focus, as before, so there is still a
  hint that more rows are below. A thin bar on the right shows where you are in a long list.

What stays the same for the person using it: 1 to 9 still open the first nine rows the filter
leaves, wherever the list is scrolled; Up and Down still go round from the last row to the first
when the list is the only thing on the screen; Page Up and Page Down (Fn + L and Fn + M) move
five rows at a time; and Home and End (Fn + K and Fn + N) now go to the first and last row.

The list handles Up, Down, Page Up, Page Down, Home and End itself instead of leaving them to
LVGL's focus group, because the group only holds the slots, not the rows. At the first or last
row of a screen that has other widgets too (a text box), it hands the key back to the shell,
which moves the focus to that widget as before.

For app code almost nothing changed. The functions are the same, except that `add()` and
`add_choice()` now return the row's index instead of its widget, since a row no longer has a
widget of its own (no app used that return value). The class's size did change, so the app
level went from 3 to 4 (see [How apps are loaded](#how-apps-are-loaded)).

### The watchdog: a frozen screen restarts itself

If a bug ever makes ZeroShell's main loop stop (an endless loop, a deadlock, a call that never
returns), the screen would stay frozen until the battery ran out, because nothing else draws on
it. The watchdog makes systemd notice and restart it.

How it works:

1. `zeroshell.service` has `WatchdogSec=10` and `NotifyAccess=main`. systemd then passes
   ZeroShell two environment variables: `NOTIFY_SOCKET`, a socket to report to, and
   `WATCHDOG_USEC`, the limit (10,000,000 microseconds).
2. The first thing `main()` does is `watchdog_init()` (`shell/src/platform/watchdog.cpp`): it
   reads both, opens the socket, sends the first `WATCHDOG=1`, and removes the variables from its
   environment so that no program ZeroShell starts (an M5 app, the terminal) can ping on its
   behalf and hide a frozen UI.
3. Every pass of the main loop (`platform_run()` in `shell/src/platform/platform.cpp`) calls
   `watchdog_kick()`. That sends `WATCHDOG=1` again if a quarter of the limit (2.5 seconds) has
   passed since the last one, and otherwise returns at once. The loop comes round at least every
   33 ms while the screen is on and every 500 ms while it's off, so a healthy ZeroShell pings
   every 2.5 seconds whatever it's doing.
4. If 10 seconds pass with no ping, systemd logs `Watchdog timeout`, kills ZeroShell with SIGABRT
   and, because of `Restart=always` and `RestartSec=1`, starts it again a second later. You are
   back on the home screen. Any note open at that moment loses what wasn't saved, which is the
   price of not staying frozen.

The protocol is a single datagram of text on a Unix socket, the same thing `sd_notify()` in
libsystemd sends, so ZeroShell needs no extra library for it. The cost is one small `sendto()`
every 2.5 seconds.

Some waits on the UI thread are slow on purpose, not stuck: pairing a Bluetooth device runs
`bluetoothctl --timeout 25 pair`, and a Jellyfin request may take up to 8 seconds. The functions
that run a command and wait for it (`run()` in `shell/src/hal/linux_hal.cpp` and
`shell/src/ui/store.cpp`, `capture()` in `shell/src/apps/kit/run.cpp`, `http()` in
`shell/src/hal/jellyfin.cpp`) create a `WatchdogGrace` for as long as they run. It tells systemd
to raise the limit to 90 seconds (`WATCHDOG_USEC=90000000`, which systemd accepts while the
service runs) and puts it back to 10 when the command returns. Grace is only given on the UI
thread: a worker thread waiting doesn't stop the loop, so it doesn't need any.

Outside systemd (the PC simulator, the headless build, running `zeroshell` by hand over SSH)
there is no `NOTIFY_SOCKET`, and all of this does nothing.

This works together with the other ways ZeroShell can stop: a crash, or a failed LVGL assertion
(which now calls `abort()`), also ends the process, and the same `Restart=always` brings it back.
The watchdog covers what those can't: a loop that is stuck without crashing.

### The hardware layer

`zs::Hal` in `shell/src/hal/hal.h` is the only way the UI reaches hardware. It has two
implementations.

`MockHal` (`mock_hal.cpp`) returns plausible simulated readings: a battery that drains, Wi-Fi
networks to join, a compass that turns. It is used by the simulator, the browser build and CI,
and on the device with `zeroshell --mock`.

`LinuxHal` (`linux_hal.cpp`) uses standard Linux interfaces and tools, nothing private to
ZeroOS:

| Hardware | Chip | How ZeroOS reads or drives it |
| --- | --- | --- |
| Battery gauge | BQ27220 | `/sys/class/power_supply/bq27220-0/` |
| Screen | ST7789V3, 320x170 over SPI | Linux framebuffer, drawn by LVGL |
| Backlight | PWM | `/sys/class/backlight/*` |
| Keyboard | TCA8418 | evdev, see above |
| Motion sensor | BMI270 and BMM150 (not on the Lite) | iio, `/sys/bus/iio/devices/` |
| Clock | RX8130CE | `/dev/rtc0` |
| Wi-Fi and Bluetooth | BCM43439 | `nmcli` (NetworkManager), `bluetoothctl`, `rfkill` |
| Audio | ES8389 codec, 1 W speaker, mic | `wpctl` (PipeWire), falling back to `amixer`; sox `play` and `rec` |
| Infrared | GPIO transmitter and receiver | `ir-ctl` (rc-core), with codes encoded by `shell/src/ir/` |
| Camera | IMX219 (not on the Lite) | libcamera, `rpicam-vid` for QR codes |
| Expansion port | Grove (I2C, UART), 14-pin bus | i2c-dev, spidev, UART, gpsd, meshtasticd |

The full map, with what each part is used for, is in [Hardware notes](docs/HARDWARE.md).

Two rules run through `LinuxHal`:

- Every read degrades to "unknown" instead of failing. A missing driver or file shows `--` on
  screen; it doesn't crash the UI. This is tested: CI opens every app using `LinuxHal` on a
  machine that has none of the Cardputer's hardware and none of the apps' extra tools.
- Anything typed by the user that ends up in a shell command (a Wi-Fi name or password, a file
  path, a Tailscale peer) goes through `shell_quote()`, which single-quotes it. Long-running
  programs (the music player, the camera, the terminal) are started with `fork` and `execvp`
  with an argument list, not through a shell. Calls that fork a tool (Tailscale status,
  `systemctl`) are cached for a few seconds so the UI doesn't fork constantly.

A planned change (see the [roadmap](docs/ROADMAP.md)) moves `LinuxHal` into a small system
service with a D-Bus API, so scripts get the same hardware access and the UI needs fewer
permissions. The `Hal` interface stays the same, so apps won't notice.

### How apps are loaded

On the device, every app except the base ones is a separate shared library,
`/usr/lib/zeroos/apps/<id>.so`. The simulator and the browser build compile every app into one
binary instead.

- `shell/cmake/ZeroShellApps.cmake` turns an app into a module. You list the app once with
  `zs_app(<id> <name> <icon> <colour> <factory> SOURCES ...)`; the CMake code generates a small
  file that exports one C function, `zs_app_module()`, returning the app's name, icon, colour,
  place on the home screen and factory function.
- A module links nothing itself. LVGL, the hardware layer and the shell's functions all come
  from the `zeroshell` binary, which exports them. That keeps modules small and guarantees every
  app uses the same LVGL as the shell.
- `shell/src/apps/registry.cpp` loads each module with `dlopen(RTLD_NOW)`, so a module missing a
  symbol fails at load time with a message in the journal, not with a crash when you open it.
- A module is only accepted if it was built for the same app level (`ZS_APP_ABI` in
  `shell/src/ui/app.h`, currently 4). The level goes up whenever a header that apps include
  changes in a way that breaks already-built modules. `zeroshell --app-abi` prints it. It went
  from 3 to 4 when `ListView` changed size ([How long lists stay fast](#how-long-lists-stay-fast));
  ZeroOS 0.6.0 is the first release with level 4, and the apps repository now builds every app
  for level 4.
- Modules are never unloaded, because an open app's code must stay mapped. When the Apps app
  upgrades a module you've already opened, ZeroShell restarts itself once (about a second) to
  run the new code. After the same upgrade from a terminal it shows a message asking for a
  restart instead, since it won't restart under an app you may be using.
- A built-in app always wins over a module with the same id.

### How apps are installed

The Apps app is a front end. The work is done by a shell script,
`/usr/bin/zeroos-apps` (`image/stage-zeroos/files/apps/zeroos-apps`), running as root through a
systemd unit, `zeroos-apps@<job>.service`. ZeroShell starts the unit and reads progress from
`/run/zeroos/apps.status`.

```
Apps app (ZeroShell)  --systemctl start-->  zeroos-apps@install:media.service  (root)
        ^                                         |  1. read the catalog (from every source's index)
        |  /run/zeroos/apps.status                |  2. download zeroos-app-media_<v>_arm64.deb, check SHA-256
        +-----------------------------------------+  3. apt-get install it, with what it depends on
                                                     4. ZeroShell loads /usr/lib/zeroos/apps/media.so
```

An app is an ordinary Debian package, `zeroos-app-<id>`, holding the module, the app's own files
(services, scripts, settings) and `Depends:` lines for the Debian packages it needs. It always
depends on `zeroos-app-abi-<n>`, which only a ZeroOS with that app level provides, so apt itself
refuses to install an app built for a different ZeroOS. Removing an app with apt also removes
the extras it brought, unless another app still needs them.

A source is a GitHub repository whose newest release holds a file called `zeroos-apps.index` and
the packages it lists. The sources are in `/etc/zeroos/apps.sources`, most trusted first:

1. `hartj77-blip/CardPuter-Apps.4ME-U`, the apps repository.
2. `hartj77-blip/CardPuter-OS.4ME-U`, this repository, whose releases still carry the apps as a
   fallback while they finish moving.
3. `m5stack`, [M5Stack's app store](docs/APPS.md#m5stacks-app-store).

If two sources offer the same app, the first one wins. The index is plain text, one block per
app build. An example:

```
app=media
name=Media
summary=Music player, music and video from a Jellyfin server, voice notes
version=1.4.0-abi4
abi=4
file=zeroos-app-media_1.4.0-abi4_arm64.deb
sha256=<64 hex characters>
size=<bytes>
needs=sox
```

The index is never run or sourced. `zeroos-apps` parses it with awk and drops any block that
isn't exactly the expected shape: the id must be lower-case letters, digits and `-`, the file
name must match the id, the version and checksum must match strict patterns, every value is cut
at 200 characters and control characters are stripped. Blocks for another app level or another
kind of computer are dropped too. Every download is checked against the block's SHA-256 before
apt sees it.

Apps that need a Debian repository of their own (Tailscale for Remote, Meshtastic for Mesh) carry
that repository's list file and signing key in their package. Meshtastic's key is checked against
a pinned fingerprint at build time (`MESHTASTIC_KEY_FPR` in `image/build.sh`).

### How new apps reach the home screen

The home screen's list of apps (`shell/src/apps/registry.cpp`) is read from three folders:

| Folder | What's in it |
| --- | --- |
| `/usr/lib/zeroos/apps` | App modules, `<id>.so` |
| `/usr/share/APPLaunch/applications` | Launcher entries (`.desktop` files) of apps made for M5Stack's launcher |
| `/var/lib/zeroos/apps/m5` | `zeroos-apps`' notes on the M5 apps it installed, which give them the same ids the store uses |

The Apps app reads the list again after each of its own jobs. Up to 0.4.0 that was the only time
it was read, so an app installed from a terminal (`sudo zeroos-apps install`, `apt install`,
`dpkg -i`) only appeared after a restart. Now ZeroShell watches the three folders itself
(`shell/src/ui/app_watch.cpp`):

1. At start it opens an inotify file and asks the kernel to report, for each folder, any file
   created, deleted, moved in or out, or finished being written. dpkg writes `x.so.dpkg-new` and
   renames it into place, which is a "moved in"; `cp` ends with "finished being written".
2. A folder that doesn't exist yet is watched through its nearest parent that does, for that
   folder's creation only. That matters for the M5 folder, which the first M5 app creates and dpkg
   deletes again with the last. When it appears the watches are set again on the folder itself.
3. On the device the inotify file is waited on by the main loop's `poll()` along with the
   keyboards (`platform_watch_fd()` in `shell/src/platform/platform.cpp`). With nothing being
   installed it costs no CPU: there is no timer and nothing is checked on a schedule. The PC builds
   have no such `poll()`, so there it is checked once a second instead.
4. One apt run changes files many times over several seconds, so ZeroShell waits until 1.5
   seconds pass with no change before doing anything.
5. If the home screen is open, it reads the list again and redraws home on the page it was on: a
   search keeps what was typed, All apps keeps its filter and the app in focus. If a Tab menu, a
   question or the taskbar panel is open over home, it waits for that to close first.
6. If any other app is open, it only notes that the list changed. Reading the list replaces every
   entry in it, and the open app may still be using one, so the list is read on the way back home
   instead, just before home is drawn.
7. If a module that was already loaded was replaced (an upgrade from a terminal), the old code
   stays in use until ZeroShell restarts, because a loaded library can't be swapped. Home says so
   once: "An app was updated: restart to use the new version".

Each reload is logged: `journalctl -u zeroshell | grep "apps changed"`.

### How ZeroOS updates itself

Everything ZeroOS owns (ZeroShell, its services, scripts and settings files) is one Debian
package called `zeroos`. `image/stage-zeroos/payload.sh` lists every file that goes in it. The SD
card image installs that same package, and the Update app installs a newer version of it, so a
freshly written card and an updated device end up with the same files.

A release publishes two files: `zeroos_<version>_arm64.deb` and a manifest,
`zeroos-update.manifest`:

```
version=0.4.0
file=zeroos_0.4.0_arm64.deb
sha256=<64 hex characters>
size=<bytes>
```

The Update app starts `zeroos-update.service`, which runs `/usr/bin/zeroos-update install` as
root (`image/stage-zeroos/files/update/zeroos-update`). It:

1. Takes a lock, so only one update runs at a time.
2. Refuses the update address if it's plain `http://` to anything but your own network (see
   [Security](#security-and-what-you-are-trusting)).
3. Downloads the manifest and checks every field against a strict pattern. The manifest is never
   run or sourced.
4. Compares versions with `dpkg --compare-versions`. If the offered version isn't newer, it stops.
5. Checks there's free space for four times the package size, and if the Apps app is
   mid-install, waits for it. (The Update app itself won't start an update under 20% battery
   unless the device is plugged in.)
6. Downloads the package. A connection that drops below 1 kB/s for a minute gives up instead of
   hanging. The file must match the manifest's SHA-256 or it's deleted.
7. Installs it with `dpkg`, keeping any edits you made to `/etc/zeroos/update.conf`.
8. Restarts ZeroShell and watches it for 10 seconds. If ZeroShell doesn't stay up, the previous
   package goes back on and the app says "Update undone".

The last two packages are kept in `/var/lib/zeroos/update/` (`current.deb` and `previous.deb`),
and `sudo zeroos-update rollback` puts the previous one back by hand. If an update is cut off
part way (power loss), the next run finishes it with `dpkg --configure -a` first. Nothing checks
for updates in the background, because that would cost battery; it looks when you open Update.

What an update does not change: Debian's own packages (the kernel, drivers, Chromium and so on
update through apt as usual), `config.txt` and `cmdline.txt`, and the initramfs. The kernel is
pinned and held by the image because M5Stack's board drivers are built for one kernel version;
don't `apt-mark unhold` it. A new kernel means a new image.

The full description, including running your own update server and pushing builds from a PC, is
in [Updates](docs/UPDATE.md).

### How the SD card image is built

`image/build.sh` doesn't fork M5Stack's image builder. It clones M5's public
[pi-gen fork](https://github.com/CardputerZero/pi-gen) at one pinned commit and changes it:

1. It keeps M5's kernel pin (6.18.39) and board-driver build, but pins the
   [driver repository](https://github.com/m5stack/m5stack-linux-dtoverlays) to a commit too.
   M5's own build takes whatever is on `main` that day. The script checks the pin was applied and
   fails if M5's script has changed shape.
2. It removes M5's launcher and demo apps; ZeroShell replaces them.
3. It stops after pi-gen's stage 2, so there's no desktop environment.
4. It adds its own stage, `image/stage-zeroos/`, which installs the `zeroos` package and makes
   the system changes that don't belong in a package (`01-run.sh`).

What that stage changes, with the reason for each written next to it in the script:

- Removes the armhf architecture (apt was twice as slow), the nexmon Wi-Fi driver (it breaks the
  onboard Wi-Fi) and Plymouth.
- Disables services a pocket computer doesn't need: daily apt and man-db jobs, filesystem scrubs,
  network wait-online, the EEPROM updater (the CM0 has no EEPROM), ModemManager, Raspberry Pi
  Connect, triggerhappy, rsyslog, the old swap file service, rpcbind and raspi-config's
  governor reset.
- Disables `adbd`. Its package enables it, and it offers a root shell over USB with no pairing.
  M5's own image turns it off too, in a script ZeroOS doesn't use.
- Masks the login prompt on `tty1`, which ZeroShell owns.
- Loads M5's keymap system-wide, so the Sym layer works outside M5's launcher.
- Lingers the UI user, so PipeWire runs without anyone logging in.
- Adds the user to the hardware groups it needs (video, input, audio, i2c, gpio, spi, dialout,
  bluetooth and so on) and creates the `spi` group, without which `/dev/spidev*` is root-only.
- Sets up the first-login password change (see [Security](#security-and-what-you-are-trusting)).
- Installs any apps chosen for the image with the same `zeroos-apps install-file` a device uses.

CI builds the image on GitHub's arm64 runner, natively rather than under emulation, in about 10
minutes. Before it starts, it resolves every package name in the base list and in every app's
dependencies inside a trixie arm64 container, so a misspelled or missing package fails in
seconds instead of an hour into the build.

### Memory, CPU and battery

The Cardputer Zero has 512 MB of RAM, four 1 GHz Cortex-A53 cores and no suspend. The tuning,
with each setting's file:

| Change | Why |
| --- | --- |
| No desktop, `gpu_mem=32` | A desktop costs 150 to 250 MB. ZeroShell draws straight to the framebuffer |
| zram swap, zstd, 1.5x RAM, with a 512 MB SD swap file for writeback (`files/swap.conf`, `files/zram-zeroos.conf`) | Idle memory is compressed in RAM instead of written to the card. Pages that stay idle move to the card now and then |
| `vm.swappiness=150`, `page-cluster=0` and others (`files/90-zeroos-memory.conf`) | Swap to zram early, since it's faster than re-reading files from the card |
| MGLRU with `min_ttl_ms=1000` (`files/zeroos-memory.tmpfiles`) | Better choice of what to swap out; the last second's working set stays in RAM so the UI doesn't thrash |
| earlyoom at 4% RAM and 8% swap left (`files/earlyoom`) | Closes the biggest app before the system stalls. Never ZeroShell, SSH, systemd, networking, Tailscale or audio; browsers and media players first |
| Logs on the card, capped at 24 MB (`files/journald.conf`) | Few card writes, but a failed boot can still be read |
| Dock, Chromium and the HDMI desktop start only when asked | They cost nothing until used |
| Lists make widgets only for the lines on screen (`shell/src/ui/list_view.cpp`) | The 200-row Apps list costs about 36 UI objects instead of about 1,200, and opens as fast as a short one |
| Word suggestions scan a 6 KB word list in read-only data on each key (`shell/src/ui/suggest.cpp`) | No index, no background work, nothing allocated for the built-in words. Learned words: 200 at most, about 6 KB, written to the card at most every 30 seconds |
| New apps found with inotify in the main loop's `poll()` (`shell/src/ui/app_watch.cpp`) | No polling timer: nothing runs until a file in an app folder changes |
| Watchdog ping at most every 2.5 s (`shell/src/platform/watchdog.cpp`) | One small `sendto()`, from the loop that runs anyway |
| CPU profiles Saver, Balanced, Performance (`shell/src/ui/power.cpp`) | Saver is on-demand capped near 70% speed; the screen-off state drops to the lowest speed |
| Wi-Fi power save (`files/nm-wifi-powersave.conf`), Bluetooth off after 10 minutes with nothing connected | Radios cost a lot on a small battery |

The time left shown in the top bar is ZeroOS's own estimate: an average of the gauge's voltage
times current with a 3-minute time constant, divided into the gauge's remaining charge. The
gauge's own estimate is used for the first 30 seconds and while charging.

What has been measured, on a PC rather than the device: ZeroShell peaked at 5.2 MB resident
while opening every app in turn, and the arm64 binary is 1.1 MB. Everything about the real
device (idle memory, boot time, power draw in each profile) is still an estimate. The estimates,
how they were worked out and how to measure them are in
[Battery and performance](docs/PERFORMANCE.md).

### Versions

The version comes from git (`image/version.sh`). A tagged commit `v0.4.0` is version `0.4.0`.
Three commits after it is `0.4.0+3.gabc1234`, which dpkg sorts as newer than 0.4.0 and older
than 0.4.1. Uncommitted changes add `.dirty`. The running version is in `/etc/zeroos-release`
and in Settings > About this device.

App packages carry the app level in their version: `1.2.0-abi4` is version 1.2.0 built for
level 4.

## Security and what you are trusting

ZeroOS runs SSH, can run Tailscale and Syncthing, and installs software as root, on a device
that sits on your network. This is how it handles that, including the parts that aren't as
strong as they could be.

The default password. Every image starts with user `pi` and password `raspberry`, and SSH is
on. To stop that being left in place, the image build records the default password's hash in
`/var/lib/zeroos/default-password` and adds an sshd `ForceCommand`. While the hash still matches,
every SSH login and every interactive login on the device goes to a password-change prompt
first, and SSH commands without a terminal are refused. A root service watches `/etc/shadow`; as
soon as the password changes, it removes the marker and the sshd setting. Logging in with an SSH
key is let through without the prompt. The scripts are in
`image/stage-zeroos/files/password/`. The password isn't forced through account expiry
(`chage -d 0`) because the Terminal, Dock and the user session start through PAM, which would
refuse an expired account.

What runs as root. ZeroShell does not. It runs as uid 1000 with the groups it needs for the
screen, input, audio, network, I2C, GPIO and Bluetooth. A polkit rule
(`image/stage-zeroos/files/90-zeroos.rules`) lets that one user, and no other, do exactly these
things without a password:

- NetworkManager actions (join and forget networks).
- Power off and reboot.
- Start and stop these units: `zeroos-dock`, `zeroos-terminal`, `tailscaled`, the user's
  `syncthing@` instance, `meshtasticd`, `zeroos-usb-stick`, `zeroos-update`,
  `zeroos-update-check`, and `zeroos-apps@<job>`.

The root side of each of those is a script you can read: `zeroos-update` for updates,
`zeroos-apps` for apps (which checks the job it's given itself), and `power-perms`, which only
changes permissions on the CPU frequency and panel power files.

Updates. Updates come from the address in `/etc/zeroos/update.conf`, by default this
repository's GitHub releases over HTTPS. The download must match the SHA-256 in the manifest,
but the manifest comes from the same place as the package, so the checksum only proves the
download is complete. It says nothing about who made it. In practice you are trusting whoever can
publish releases on this repository, and GitHub's HTTPS. The package is installed by dpkg as
root. Plain `http://` is only accepted for addresses on your own network (localhost, private
ranges, Tailscale's 100.64.0.0/10 and `*.ts.net`, and `.local`, `.lan` and `.home.arpa` names),
and anywhere else only if you add `ALLOW_HTTP=1` yourself. Signing the manifest with a key kept
off GitHub is not done yet; it's the next step for update security.

Apps. An installed app runs inside ZeroShell with the same access as the home screen, and
installing one runs its package's scripts as root. Like updates, the index's SHA-256 only proves
a complete download. Only add sources you trust. Apps from M5Stack's store are reviewed by
M5Stack's community repository, not by this project; ZeroOS only checks that each download is
the file M5's list names.

Your data. Wi-Fi passwords are stored by NetworkManager as usual. A Wi-Fi password that fails to
connect is deleted rather than kept and retried. The AI app's API keys are in
`~/.config/zeroos/ai.conf` with mode 600, and are handed to curl through a private temporary file
so they never show up in the process list. Jellyfin's password is used once to get a sign-in
token and isn't stored. Nothing in ZeroOS sends usage data anywhere.

Other things switched off for security: `adbd` (a root shell over USB with no pairing) and
Raspberry Pi Connect.

Please report security problems privately, through this repository's Security tab. See
[SECURITY.md](SECURITY.md). Only the newest release gets fixes.

## Testing

Every pull request runs the full CI (`.github/workflows/ci.yml`), and a release is only published
if the same CI passes on the tagged commit. Branch rules on `main` require these checks before
anything merges.

| CI job | What it does | What it catches |
| --- | --- | --- |
| Simulator build and screenshots | Builds the SDL and headless versions, runs the unit tests, then drives the headless build through every screen of every app and saves a screenshot of each | Code that doesn't compile, failing tests, an app that crashes when opened |
| Watchdog and new apps (`shell/tests/watch_test.py`, in the same job) | Starts the headless build with a socket standing in for systemd's and checks the first `WATCHDOG=1` arrives; writes an M5 launcher entry into a folder that didn't exist at start, once with home open and once with Settings open, and checks the app list is read again at the right moment | A watchdog that never pings, a folder watch that misses new apps or reloads under an open app |
| Apps as modules | Builds every app as a module, starts ZeroShell with none installed, then loads and opens each one | A module that won't load (missing symbol, wrong app level) or crashes |
| Device build with a fake screen and keyboard | Builds the real device version for the CI machine, runs it with a file standing in for the LCD and a pipe standing in for the keyboard, sends the exact key codes M5's driver sends, and checks the screen and what was typed into Notes, including a word finished with Tab | Broken key translation, typing that drops letters, word suggestions that don't appear or type the wrong thing, a device build that won't start |
| Open every app with the keyboard | The same, opening every app from the home screen, once with simulated hardware and once with the real hardware layer on a machine with no Cardputer hardware | An app that hangs or crashes when its hardware or tools are missing |
| Install every app for real | In a Debian trixie arm64 container with Raspberry Pi's repository added, installs every app with the real apt, side by side, then removes each and checks nothing is left | A dependency that doesn't exist, two apps that clash, files left behind |
| Device build (arm64) | Cross-compiles for the Cardputer, runs the update, app-installer and Bluetooth config tests, and builds the update package and every app package | A broken update or installer, a package that won't build |
| Browser simulator | Builds the WebAssembly version | Code that only builds on Linux |

The unit tests (`shell/tests/`) cover key translation, IR code encoding and Flipper file parsing,
the Jellyfin client, the AI app's requests and replies (no network), the parser for M5Stack
launcher entries, and word suggestions (which word is being typed, what is offered, what is
learned, saved and forgotten: `shell/tests/suggest_test.cpp`). `image/tests/update-test.sh` builds a real update package and drives
`zeroos-update` against a folder standing in for the server, with stand-ins for `dpkg -i` and
`systemctl`: finding an update, refusing a bad checksum or a malformed manifest, installing,
rolling back by hand and rolling back a version that keeps crashing. `image/tests/apps-test.sh`
does the same for `zeroos-apps`.

The fake screen and keyboard work through an `LD_PRELOAD` shim, `shell/tests/device/fakedev.c`,
about 50 lines, which answers the few framebuffer and input `ioctl` calls a plain file and a pipe
can't. Everything else is the device's own code.

What CI can't check is the hardware itself: whether the speaker plays, the IR LED lights, the
battery gauge reads right, the motion sensor's axes are the right way round. That's what the
[hardware test checklist](docs/HARDWARE-TEST.md) is for. If you have a Cardputer Zero, working
through it and reporting what you find is the most useful thing you can do for the project.

## Repository layout

```
shell/                    ZeroShell
  src/main.cpp            entry point and command-line options
  src/ui/                 the shell: launcher, status bar, panel, power, store, theme, settings,
                          word suggestions (suggest*.cpp)
  src/platform/           screen and keyboard for each backend; key translation
  src/hal/                hardware layer: hal.h, linux_hal.cpp, mock_hal.cpp, Jellyfin, Meshtastic
  src/ir/                 IR protocols, Flipper .ir files, the code database
  src/apps/               built-in apps, plus fallback copies of apps that moved to the apps repo
  src/fonts/              the Ubuntu font at five sizes, generated by tools/fonts.sh
  apps.cmake              the list of apps: id, name, icon, colour, sources
  cmake/                  zs_app() and the module generator
  lv_conf.h               LVGL settings
  tests/                  unit tests; tests/device/ the fake-device tests
  tools/                  screenshots, module test, font generation
image/
  build.sh                builds the SD card image on M5's pinned pi-gen
  package.sh              builds the zeroos update package and manifest
  version.sh              version from git
  stage-zeroos/           the image stage: 01-run.sh, payload.sh (every file ZeroOS owns), files/
  apps/                   app package descriptions (.pkg) and make-app-deb.sh
  tests/                  update, installer and Bluetooth config tests
cmake/                    the arm64 cross-compile toolchain file
tools/push-update.sh      send a build to a Cardputer over SSH
tools/irdb/               builds the IR code database from Flipper-IRDB
web/                      the browser simulator
docs/                     guides and design notes
.github/workflows/        CI, release, image build
```

## Building it yourself

### Run it on a PC

The home screen and every app run in a window on Linux, with simulated hardware:

```sh
sudo apt install cmake g++ libsdl2-dev
cmake -S shell -B build/sim -DZS_BACKEND=sim
cmake --build build/sim -j
./build/sim/zeroshell --zoom 4
```

The first configure downloads LVGL 9.2.2 and quirc from GitHub. Arrow keys, Enter and Esc work
as on the device.

There's also a browser version, which needs [Emscripten](https://emscripten.org):

```sh
web/build.sh build/zeroos-simulator.html
```

It produces one self-contained HTML file you can open directly. That build has only the apps this
repository still lists. To build it with every app from
[CardPuter-Apps.4ME-U](https://github.com/hartj77-blip/CardPuter-Apps.4ME-U), point it at a
checkout of that repository:

```sh
ZS_APPS=../CardPuter-Apps.4ME-U web/build.sh build/zeroos-simulator.html
```

That's the version published at
[hartj77-blip.github.io/emu-cardputer-os.4me-u](https://hartj77-blip.github.io/emu-cardputer-os.4me-u/)
by the [emu-cardputer-os.4me-u](https://github.com/hartj77-blip/emu-cardputer-os.4me-u) repository,
which rebuilds it from both repositories' `main` within about 5-15 minutes of a change.

The page draws the Cardputer Zero face on, from M5's dimension drawing and product photo: the red
power slider at the top right, Esc and Tab under the screen's corners, and four rows of 11 keys under
white legend strips (black, Fn in orange, Sym in blue, coloured sym/shift/fn/ctrl/alt labels). Tapping
those keys goes through a copy of M5's keyboard driver (`tca8418_keypad_m5stack`: tap Shift, Fn or
Sym for the next key, tap twice to lock, hold to hold, with the lights), then the same
`KeyTranslator` the device uses. The computer's own keyboard counts as a USB keyboard plugged in.
Both send real presses and releases, so holding Esc or Tab works. Beside it is a guide to the keys
and every app in the store (`apps.json`, written by the site's build from the Apps repository).

Its **Report a problem** tab opens a GitHub issue filled in with the commit, the open app, the
browser, the shell's log and a replay link: the page address plus `#replay=` and every key press
and release with its timing, which opens the simulator and plays them back.

### Command-line options and environment variables

```
zeroshell                 run (SDL window on a PC, the LCD on the device)
zeroshell --mock          device build with simulated hardware
zeroshell --zoom N        simulator window scale (default 3)
zeroshell --script S      headless build: run a list of key presses and save screenshots
zeroshell --app-abi       print the app level
zeroshell --version       print the version and backend
```

A headless script is a comma-separated list of steps: `right`, `left`, `up`, `down`, `enter`,
`esc`, `bksp`, `type:<text>`, `key:<char>`, `open:<app-id>`, `home`, `wait:<ms>`, `live:<ms>`
and `shot:<file.ppm>`. For example:

```sh
cmake -S shell -B build/headless -DZS_BACKEND=headless && cmake --build build/headless -j
./build/headless/zeroshell --script "open:notes,type:hello,wait:200,shot:notes.ppm"
```

| Variable | What it does |
| --- | --- |
| `ZS_FRAMEBUFFER` | Use this framebuffer instead of finding the LCD |
| `ZS_KEYBOARD` | Read keys from this device only |
| `ZS_KEYMAP` | `us` or `cardputer`: how to read a keyboard |
| `ZS_KEYLOG=1` | Log every raw key to the journal |
| `ZS_APP_DIR` | Load app modules from this folder instead of `/usr/lib/zeroos/apps` |
| `ZS_M5_APP_DIR`, `ZS_M5_STATE_DIR` | Read M5 launcher entries and `zeroos-apps`' notes on them from these folders instead of `/usr/share/APPLaunch/applications` and `/var/lib/zeroos/apps/m5` |
| `NOTIFY_SOCKET`, `WATCHDOG_USEC` | Set by systemd, not by hand: where and how often to send the watchdog ping |
| `ZS_GAMES_DIR`, `ZS_GAMES_CORES` | Where the Games app looks for games and libretro cores |
| `ZEROOS_IR_DB` | Use another IR code database |

### Build for the device

Cross-compile ZeroShell and the app modules for arm64:

```sh
sudo apt install g++-aarch64-linux-gnu
cmake -S shell -B build/device -DZS_BACKEND=device -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/aarch64-linux-gnu.cmake
cmake --build build/device -j
```

Build the update package (needs `dpkg-deb`, no root):

```sh
image/package.sh build/device/zeroshell build/update
```

Build the app packages and the SD card image. The image needs
[Docker](https://docs.docker.com/get-docker/) and about 20 GB of disk, and takes 1 to 2 hours on
an x86 PC (about 10 minutes on an arm64 machine with `PIGEN_NATIVE=1`):

```sh
image/apps/build.sh build/device/apps
image/build.sh build/device/zeroshell
```

`image/build.sh` reads `WIFI_COUNTRY` (default `US`), `FIRST_USER_PASS` (default `raspberry`)
and `ZEROOS_APPS` (apps to put on the image, for example `"notes media gps"`).

No Linux machine? Run the
[Build image](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/actions/workflows/image.yml)
workflow from the Actions tab instead.

To push a build to a Cardputer that's already running ZeroOS, over Wi-Fi, Ethernet or Tailscale:

```sh
tools/push-update.sh cardputer.local
```

It copies the newest package from `build/update/` over SSH and runs `zeroos-update install-file`
on the device, so the same checks and rollback apply as for a normal update.

### Tests

Unit tests, after a headless or simulator build:

```sh
ctest --test-dir build/headless --output-on-failure
shell/tests/run.sh
```

The keyboard tests, with the fake screen and keyboard:

```sh
cmake -S shell -B build/device-native -DZS_BACKEND=device && cmake --build build/device-native -j
gcc -shared -fPIC -o build/fakedev.so shell/tests/device/fakedev.c -ldl
python3 shell/tests/device/device_test.py build/device-native/zeroshell build/fakedev.so
python3 shell/tests/device/apps_test.py build/device-native/zeroshell build/fakedev.so
```

Screenshots of every screen:

```sh
shell/tools/screenshots.sh build/headless/zeroshell build/screenshots
```

The update and installer tests:

```sh
image/tests/update-test.sh
image/tests/apps-test.sh
```

`image/tests/apps-install-test.sh` installs and removes packages system-wide, so it refuses to
run outside a container. Leave it to CI.

For a real device, go through the [hardware test checklist](docs/HARDWARE-TEST.md).

## Contributing

Pull requests are welcome. All CI checks have to pass before anything merges into `main`.

- To change an app or add a new one, use
  [CardPuter-Apps.4ME-U](https://github.com/hartj77-blip/CardPuter-Apps.4ME-U) instead of this
  repository. The copies of apps still in `shell/src/apps/` here are fallbacks that will be
  removed.
- If you change a header that apps include (`shell/src/ui/`, `shell/src/hal/`,
  `shell/src/apps/common.h`, LVGL or `lv_conf.h`) in a way that breaks already-built modules,
  raise `ZS_APP_ABI` in `shell/src/ui/app.h` in the same pull request.
- Changes to the image or system files say which problem they fix in a comment, as the existing
  ones do.
- If you have a Cardputer Zero, results from the [hardware test checklist](docs/HARDWARE-TEST.md)
  and real measurements for [Battery and performance](docs/PERFORMANCE.md) are as valuable as
  code.

## Releases

Releases are made with the
[Release](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/actions/workflows/release.yml)
workflow: Run workflow, enter a version like `0.4.0`. It runs all of CI on the newest commit on
`main`, and only if that passes, tags it, builds the update package and apps, and publishes a
GitHub release. The SD card image is built and attached about 10 minutes later. A version with a
`-` (`0.5.0-rc1`) is published as a pre-release, which devices don't see until it's marked as
the latest. See [Publishing an update](docs/UPDATE.md#publishing-an-update).

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
[Jellyfin](docs/JELLYFIN.md) ·
[Car](docs/CAR.md) ·
[Camera](docs/CAMERA.md) ·
[Dock](docs/DOCK.md) ·
[GPS and LoRa](docs/EXPANSION.md) ·
[Dev Tools](docs/DEV.md) ·
[Games](docs/GAMES.md) ·
[AI](docs/AI.md)

Development:
[Architecture](docs/ARCHITECTURE.md) ·
[Interface spec](docs/UX.md) ·
[Hardware notes](docs/HARDWARE.md) ·
[Hardware test checklist](docs/HARDWARE-TEST.md) ·
[Roadmap](docs/ROADMAP.md)

Hardware docs from M5Stack: [Cardputer Zero](https://docs.m5stack.com/en/CardputerZero)

## License and third-party code

ZeroOS's own code is MIT licensed, see [LICENSE](LICENSE). It builds on:

| Project | Used for | License |
| --- | --- | --- |
| [LVGL](https://lvgl.io) 9.2.2 | Drawing the UI | MIT |
| [quirc](https://github.com/dlbeer/quirc) 1.2 | Reading QR codes | ISC |
| [Flipper-IRDB](https://github.com/Lucaslhm/Flipper-IRDB) | IR remote codes | CC0 |
| M5Stack's [pi-gen fork](https://github.com/CardputerZero/pi-gen) | Building the image | BSD-3 |
| M5Stack's [board drivers](https://github.com/m5stack/m5stack-linux-dtoverlays) | Screen, keyboard and other hardware | See that repository |
| The [Ubuntu font](https://design.ubuntu.com/font) | The UI's typeface | Ubuntu Font Licence |
| Ansel Adams, [*The Tetons and the Snake River*](https://commons.wikimedia.org/wiki/File:Adams_The_Tetons_and_the_Snake_River.jpg) (1942), U.S. National Archives 79-AA-G01 | The photograph on home and the lock screen | Public domain |
| Raspberry Pi OS and Debian | Everything underneath | Various free licences |

The hardware notes draw on early owner reports, credited in [Hardware notes](docs/HARDWARE.md).

Cardputer and M5Stack are trademarks of M5Stack and are only used here to name the hardware.
