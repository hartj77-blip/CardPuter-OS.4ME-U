# Hardware test checklist

Latest results on real hardware: [TESTED.md](TESTED.md).

Flash `zeroos-cardputerzero-arm64.img.xz` from the newest release, join Wi-Fi, then open **Apps** (pinned on the home screen). For each app below: select it, press **Enter**, choose **Install**, wait for "installed", open it from the home screen, do the test, then press **Esc** until you're home. Tick the box if it worked; if not, note what you saw.

If something freezes or crashes, connect over SSH and run `journalctl -u zeroshell -b | tail -50` (it logs which app was open).

If a key does nothing, log every raw key ZeroShell gets: `sudo systemctl edit zeroshell`, add
`[Service]` and `Environment=ZS_KEYLOG=1`, restart it, press the key, and read
`journalctl -u zeroshell -b | grep 'zeroshell: key'` (code and value: 1 down, 0 up). Remove the
override afterwards with `sudo systemctl revert zeroshell`.

## Already on the SD card image

- [ ] **Home screen**: typing "wi" lists Wi-Fi (a setting) and Turn Wi-Fi off; Enter opens the first, Settings at Wi-Fi; Esc clears. All apps has no Wi-Fi or Bluetooth: they are in Settings. 1 to 7 open the pinned apps. Up from the pinned apps reaches All apps, where typing finds an app. Tab > P pins or unpins the app in focus. Type fast in Notes later to check no keys are dropped.
- [ ] **Keys** ([KEYS.md](KEYS.md)): Fn+S/D change the volume and Fn+U/I the brightness (a message says the new level); in any app Fn+S/D and Fn+U/I do the same, and Fn+Q plays or pauses music. Ctrl + Space opens the taskbar panel, and 1 there turns Wi-Fi off and on. In Settings, 0 shows numbers and 2 opens Bluetooth. Holding Esc goes home from anywhere. In Notes, numbers still type.
- [ ] **New keys** ([UX.md](UX.md)): in Notes, type something, then Ctrl + Space opens the taskbar panel and nothing extra is typed; Ctrl + Space again closes it. Try it both ways: Ctrl held down, and Ctrl tapped (the status bar shows Ctrl) then Space. In Settings > Wi-Fi, open a network's password box and hold Esc for a second: you land on the home screen. A quick Esc there only goes back to the list.
- [ ] **Sticker keys** ([KEYS.md](KEYS.md)): Fn+J (PRSC) says "Screenshot saved"; the PNG in ~/Pictures/Screenshots (open it in Files, or copy it off) shows the screen as it was. Fn+J in the Terminal beeps and saves the console. Fn+H (HELP) shows the keys for the screen. Hold Tab (LOCK) for a second: the screen goes off; the next key only turns it back on, on the same screen. Also note: does Fn+J in the Terminal do anything else there (it shouldn't quit the program running)?
- [ ] **Wi-Fi** (Settings > Wi-Fi): Left/Right on the first row turns Wi-Fi off and on; Esc goes back to Settings. The list of networks shows; the password shows as you type it; Enter joins. The password now reaches NetworkManager through a private file, not nmcli's command line: after joining, restart, and check it reconnects by itself (the password was saved). A wrong password says so and leaves an already-saved network working. Try a WPA3-only network if you have one.
- [ ] **Settings**: brightness and volume change. In Bluetooth, Left/Right on the first row turns it off and on; the paired devices and Scan for devices are in the same list.
- [ ] **Files**: browse ~, open a .txt (opens in Notes once Notes is installed), Esc goes up a folder.
- [ ] **Terminal**: Enter opens a console; type `ls`; Ctrl+Q or `exit` comes back to the home screen.
- [ ] **Update**: says you're on the newest version.
- [ ] **Dock**: with an HDMI monitor plugged in, Start Dock mode shows the desktop; without one it says No monitor.
- [ ] **Apps**: Tab > R refreshes the list; you see the installable apps listed.
- [ ] **First login and SSH**: on a fresh card, `ssh pi@cardputer.local` with `raspberry` is refused (keys only). The Terminal app asks for a new password; after it, `ssh` with the new password works. Ctrl+C at the password prompt on a console ends the login.
- [ ] **Adding a source**: Apps > Tab > P > Add a GitHub repo asks for your password; a wrong one says "Wrong password" and adds nothing; the right one adds it. Installing and removing apps still needs no password.

## Installed from Apps

- [ ] **Notes**: type a paragraph quickly, Esc, reopen: every letter is there.
- [ ] **Clock**: time is right; Space starts and stops the stopwatch.
- [ ] **Battery** (Settings > Battery & power; it is no longer an app): the heading shows a real percentage and time left, and the rows under the settings show power draw, voltage, current, temperature, health and cycles, changing every second; plugging in USB shows charging. Typing "battery" on the home screen opens it, and so does 5 in the taskbar panel. If the old Battery app was installed, it is gone from All apps, and Apps > Tab > U removes its package.
- [ ] **System**: CPU, memory and temperature show real, changing numbers.
- [ ] **Compass**: needle moves as you turn the device (Lite model: should say "No IMU").
- [ ] **Media**: copy a song into ~/Music, it plays through the speaker; Fn + Q pauses it. **Recorder**: record a voice note and play it back.
- [ ] **Camera** (full model): viewfinder opens, a photo saves; Q scans a Wi-Fi QR code. Lite: should say "No camera found".
- [ ] **IR Remote**: Universal > pick a TV brand, point at a TV, it turns off; learn one button from a real remote and send it back.
- [ ] **GPS** (needs the Cap LoRa-1262 or a Grove GPS): outdoors, a fix within a few minutes. With no module: "No GPS found" after a few seconds.
- [ ] **Mesh** (needs the Cap LoRa-1262): turn it on, your node name shows, a message reaches a phone running Meshtastic. Without the cap: should say no radio, not hang. A phone on the same Wi-Fi can't open `https://<cardputer>:9443` until **Allow phones**, and can after; **Block phones** or turning the mesh off closes it again.
- [ ] **Tether**: share your phone's internet over USB or Bluetooth; Wi-Fi off, a page still loads in Terminal (`curl -I example.com`).
- [ ] **Remote**: sign in to Tailscale with the link or QR, your other devices are listed, SSH to one opens.
- [ ] **Sync**: shows a device ID; pair with Syncthing on your PC, a note syncs across.
- [ ] **Car**: USB stick mode builds, then a car stereo (or PC) sees a USB drive with your music; Bluetooth pairs with a speaker. (The image is now built as `pi`, not root: check it still builds, and that `/var/lib/zeroos/car/music.img` is owned by root.)
- [ ] **Bluetooth pairing**: pairing a keyboard, headphones and an Xbox pad from Settings still works (the pairing window is now only open for the device you picked, and scanning no longer opens one).
- [ ] **Remote with an auth key**: signing in to Tailscale with an auth key still works (the key goes to `tailscale up` as a file now).
- [ ] **Jellyfin**: signing in with a bare server name uses https when the server has it; an `http://` address warns that it's unencrypted and still works; videos and music still play (the token goes to mpv in a file now).
- [ ] **Bluetooth re-pairing**: a gamepad that lost its pairing pairs again from Settings > Bluetooth (re-pairing now needs that OK on the device).

## Remove

- [ ] Remove one app in **Apps** (Enter > Remove, then Enter to confirm): it disappears from All apps and `zeroos-apps list` no longer shows it installed.
