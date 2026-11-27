# What's verified on real hardware

Results of running [HARDWARE-TEST.md](HARDWARE-TEST.md) on a real Cardputer Zero (full
model, camera present), freshly flashed with the **ZeroOS 0.7.1** image, on 2026-11-24.

Keys were injected through the keyboard's input device and the LCD was read back from
`/dev/fb1`, so every **PASS** below has a screenshot or command output behind it.

- **PASS**: seen working.
- **PARTIAL**: works as far as it could be checked without the missing hardware or account.
- **NEEDS HUMAN**: still to do by hand. The **Still to test by hand** list says exactly what.

Fn chords were tested by sending the codes the keyboard driver produces for them (for
example Fn+S is volume down). Pressing the physical Fn key itself is still to do by hand.

## Boot

| Check | Result | Notes |
| --- | --- | --- |
| ZeroShell starts, no failed units | PASS | No ZeroShell warnings in the journal for the whole session. |
| Wi-Fi joins (on-device setup) | PASS | |
| Root filesystem expands to card size | PASS | 29 GB card. 5.5 GB used after installing every first-party app. |
| Battery reading | PASS | bq27220: % shown, plus voltage and health. |
| Temperature | PASS | 43 °C idle, 52 °C under load. |
| Update | PASS | Showed "Version 0.7.2 is ready", which was correct: 0.7.2 came out during the test. |

## On the SD card image

| Item | Result | Notes |
| --- | --- | --- |
| Home search ("wi", Enter, Esc) | PASS | |
| Pinned apps 1 to 7 | PASS | |
| Up to All apps, typing finds an app | PASS | |
| Tab > P pin and unpin | PASS | |
| Fast typing in Notes | PASS | 118 characters at 10, 25 and 50 chars/s, none dropped. |
| Volume and brightness keys, with on-screen message | PASS | |
| Fn+Q play/pause | PASS | |
| Ctrl + Space taskbar panel | PASS | Opens and closes with Ctrl held, nothing typed. Tapped Ctrl then Space not tested. |
| Taskbar 1: Wi-Fi off and on | PASS | |
| Taskbar 2: Bluetooth | PASS | |
| Hold Esc goes home | PASS | Except Camera, see Known issues. |
| Wi-Fi password box | PASS | Typed characters show. Quick Esc goes back to the list, hold Esc goes home. |
| PRSC screenshot (home and Terminal) | PASS | Doesn't quit the program running in Terminal. |
| HELP | PASS | |
| LOCK (hold Tab) | PASS | Takes two keys to get back (wake, then dismiss). |
| Settings: brightness, volume, Bluetooth, 0 shows numbers | PASS | |
| Files: browse, open .txt in Notes, Esc goes up | PASS | |
| Terminal: console, `ls`, Ctrl+Q home | PASS | |
| Dock without a monitor | PASS | Says "No monitor". Start Dock mode doesn't crash. |
| Apps: Tab > R refresh, list | PASS | |
| Remove an app | PASS | Gone from All apps and from `zeroos-apps list`. |

## Installed from Apps

All 25 first-party store apps installed through the on-screen Apps UI without dependency
or ABI errors. Each took 1 to 4 minutes. ZeroShell never restarted.

| App | Result | Notes |
| --- | --- | --- |
| AI | PARTIAL | Provider, key and model screens work. No API key was entered, so chat is untested. |
| Battery | PASS | Charging on USB plug-in is untested. |
| Browser | PASS | Loaded example.com over Wi-Fi. |
| Camera | PARTIAL | Viewfinder opens and a 3280x2464 photo saves, but the test photo was black. The QR page opens. Esc: see Known issues. |
| Car | PARTIAL | UI opens. USB stick mode and Bluetooth untested. |
| Clock | PASS | Stopwatch works. Shows UTC, see Known issues. |
| Compass | PARTIAL | IMU found and heading is live. Rotation untested. |
| Desktop (remote desktop) | PARTIAL | Opens to "Add a computer". No target was connected. |
| Dev | PASS | Code, REPL (Python, Lua, calculator, SQLite), Git and Serial screens work. |
| Libretro | PARTIAL | No ROMs ship. A homebrew NES ROM ran in Nestopia, and pause menu and Esc work. GB/GBA cores untested. |
| GPS | PASS | No module: "No GPS found" after a few seconds. Outdoor fix untested. |
| Grove | PARTIAL | LED strip page opens. No strip attached. |
| IR Remote | PARTIAL | Universal TV power codes send and remotes can be created. TV reaction and learning untested. |
| Jellyfin | PARTIAL | Reaches the server address prompt. |
| Media | PASS | MP3 and WAV from ~/Music play, play/pause works. Sound through the speaker not confirmed by ear. |
| Mesh | PASS | No cap: "No radio" after about 15 s, doesn't hang. |
| Net | PASS | Discover found 25 LAN devices. DNS lookup works. |
| Notes | PASS | |
| Recorder | PARTIAL | Records and lists the file, but the audio is near silent. See Known issues. |
| Remote (Tailscale) | PARTIAL | Shows the sign-in link and QR. Not signed in. |
| RF | PASS | No dongle: "No radio found". |
| Router | PARTIAL | UI only. Nothing was started. |
| Sync | PARTIAL | Device ID and folders show, Syncthing runs. Not paired. |
| System | PASS | CPU, memory and temperature are live. The IP row shows Ethernet when both are up. |
| Tether | PARTIAL | Options list opens. No phone was attached. |

## m5stack store apps (sample)

The `m5stack` source offers 179 apps. Only a sample was smoke-tested: install, open,
usable screen, hold Esc home, remove. M5 apps need Esc held for about 3 seconds.

| App | Result | Notes |
| --- | --- | --- |
| m5-coinflip | PASS | |
| m5-craps | PASS | |
| m5-sudokuzero | PASS | |
| m5-battleship | PASS | |
| m5-zerochord | PASS | |
| m5-wanwu-desktop | FAIL | Opens to "cannot find desktop (lightdm/labwc not running?)". |
| m5-matrixzero | not finished | Still installing after 2 minutes. |

The other ~170 are untested.

## Known issues found

1. **Recorder audio is near silent.** Peak level was −54 dB, and only −44 dB with a loud
   tone playing on the speaker. The cause is the mic gain left at the chip's default; fixed in
   #79.
2. **Camera viewfinder ignores a short Esc press.** This is by design: the viewfinder is
   M5Stack's `camera_app`, which ZeroShell hands the screen to. You leave it with hold 4
   (as its help says) or a long Esc hold, like other external programs (shell.cpp
   `kExternalQuitMs`), so a stray tap doesn't drop you out.
3. **Time zone defaults to UTC**, so Clock and the status bar show UTC.
4. **System app IP row** shows the Ethernet address when both Ethernet and Wi-Fi are up.
5. **Apps search can rank an M5 app above ours.** For example, typing "desktop" and pressing
   Enter twice installs `m5-wanwu-desktop`.
6. **Lock screen** takes two key presses to get back (wake, then dismiss). This may be
   intended.
7. **`sudo reboot` doesn't come back.** Shutdown completes in the journal ("Shutting down",
   filesystems synced), then the board never restarts, and the 2-minute hardware watchdog
   doesn't recover it either. It needed a power cycle. Seen once; not yet re-tested.

## Still to test by hand

- HDMI Dock mode.
- IR: aim at a TV, and learn a button from a real remote.
- GPS fix outdoors.
- Mesh with the LoRa cap and a phone.
- Tether with a phone.
- Tailscale sign-in and SSH.
- Syncthing pairing.
- Car: USB stick mode and Bluetooth speaker.
- Compass rotation.
- Charging on USB plug-in.
- Camera: a real photo and a Wi-Fi QR scan.
- Recorder: speak into it and play it back.
- Media: listen through the speaker.
- Physical Fn chords.
- AI chat with an API key.
- Jellyfin with a server.
- RF with an SDR dongle.
- Router with Tailscale signed in.
- Libretro with real ROMs.
- The remaining m5stack apps.
