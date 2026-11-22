# Hardware test checklist

Flash `zeroos-cardputerzero-arm64.img.xz` from the newest release, join Wi-Fi, then open **Apps** (in the dock). For each app below: select it, press **Enter**, choose **Install**, wait for "installed", open it from the home screen, do the test, then press **Esc** until you're home. Tick the box if it worked; if not, note what you saw.

If something freezes or crashes, connect over SSH and run `journalctl -u zeroshell -b | tail -50` (it logs which app was open).

If a key does nothing, log every raw key ZeroShell gets: `sudo systemctl edit zeroshell`, add
`[Service]` and `Environment=ZS_KEYLOG=1`, restart it, press the key, and read
`journalctl -u zeroshell -b | grep 'zeroshell: key'` (code and value: 1 down, 0 up). Remove the
override afterwards with `sudo systemctl revert zeroshell`.

## Already on the SD card image

- [ ] **Home screen**: arrow keys move, and so do F, Z, X and C without Fn. Shift + a letter jumps to an app, Enter opens, Esc comes back, 3 to 9 open the dock's apps, 0 shows their numbers. Type fast in Notes later to check no keys are dropped.
- [ ] **Keys** ([KEYS.md](KEYS.md)): on the home screen S/D change the volume and U/I the brightness (a message says the new level); in any app Fn+S/D and Fn+U/I do the same, and Fn+Q plays or pauses music. 2 opens the taskbar panel, and 3 there turns Wi-Fi off and on. In Settings, 0 shows numbers and 4 switches Bluetooth. 1 goes home from anywhere. In Notes, numbers still type.
- [ ] **New keys** ([UX.md](UX.md)): in Notes, type something, then Ctrl + Space opens the taskbar panel and nothing extra is typed; Ctrl + Space again closes it. Try it both ways: Ctrl held down, and Ctrl tapped (the status bar shows Ctrl) then Space. In Wi-Fi, open a network's password box and hold Esc for a second: you land on the home screen. A quick Esc there only goes back to the list.
- [ ] **Wi-Fi**: the list of networks shows; the password shows as you type it; Enter joins.
- [ ] **Settings**: brightness and volume change, Bluetooth toggles.
- [ ] **Files**: browse ~, open a .txt (opens in Notes once Notes is installed), Esc goes up a folder.
- [ ] **Terminal**: Enter opens a console; type `ls`; Ctrl+Q or `exit` comes back to the home screen.
- [ ] **Update**: says you're on the newest version.
- [ ] **Dock**: with an HDMI monitor plugged in, Start Dock mode shows the desktop; without one it says No monitor.
- [ ] **Apps**: R refreshes the list; you see the installable apps listed.

## Installed from Apps

- [ ] **Notes**: type a paragraph quickly, Esc, reopen: every letter is there.
- [ ] **Clock**: time is right; Space starts and stops the stopwatch.
- [ ] **Battery**: shows a real percentage and time left; plugging in USB shows charging.
- [ ] **System**: CPU, memory and temperature show real, changing numbers.
- [ ] **Compass**: needle moves as you turn the device (Lite model: should say "No IMU").
- [ ] **Media**: copy a song into ~/Music, it plays through the speaker; record a voice note and play it back.
- [ ] **Camera** (full model): viewfinder opens, a photo saves; Q scans a Wi-Fi QR code. Lite: should say "No camera found".
- [ ] **IR Remote**: Universal > pick a TV brand, point at a TV, it turns off; learn one button from a real remote and send it back.
- [ ] **GPS** (needs the Cap LoRa-1262 or a Grove GPS): outdoors, a fix within a few minutes. With no module: "No GPS found" after a few seconds.
- [ ] **Mesh** (needs the Cap LoRa-1262): turn it on, your node name shows, a message reaches a phone running Meshtastic. Without the cap: should say no radio, not hang.
- [ ] **Tether**: share your phone's internet over USB or Bluetooth; Wi-Fi off, a page still loads in Terminal (`curl -I example.com`).
- [ ] **Remote**: sign in to Tailscale with the link or QR, your other devices are listed, SSH to one opens.
- [ ] **Sync**: shows a device ID; pair with Syncthing on your PC, a note syncs across.
- [ ] **Car**: USB stick mode builds, then a car stereo (or PC) sees a USB drive with your music; Bluetooth pairs with a speaker.

## Remove

- [ ] Remove one app in **Apps** (Enter > Remove): it disappears from the home screen and `zeroos-apps list` no longer shows it installed.
