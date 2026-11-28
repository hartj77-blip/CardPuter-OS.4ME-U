# Hardware test checklist

Latest results on real hardware: [TESTED.md](TESTED.md).

Flash `zeroos-cardputerzero-arm64.img.xz` from the newest release, join Wi-Fi, then open **Apps** (pinned on the home screen). For each app below: select it, press **Enter**, choose **Install**, wait for "installed", open it from the home screen, do the test, then press **Esc** until you're home. Tick the box if it worked; if not, note what you saw.

If something freezes or crashes, connect over SSH and run `journalctl -u zeroshell -b | tail -50` (it logs which app was open).

If a key does nothing, log every raw key ZeroShell gets: `sudo systemctl edit zeroshell`, add
`[Service]` and `Environment=ZS_KEYLOG=1`, restart it, press the key, and read
`journalctl -u zeroshell -b | grep 'zeroshell: key'` (code and value: 1 down, 0 up). Remove the
override afterwards with `sudo systemctl revert zeroshell`.

## Already on the SD card image

- [ ] **Welcome tour**: on a freshly written card, the first screen after boot is the welcome tour, not the home screen. On the arrow keys page, Fn + F, Z, X and C each light their key; tapping Fn first, then the letter, works too. Press 0 and Tab on the Menus page: the key sheet and the menu open. On the last page, 1 opens Wi-Fi and Esc comes back to that page. Restart: it goes straight to the home screen. Typing `tour` on the home screen opens it again.
- [ ] **Home screen**: typing "wi" lists Wi-Fi (a setting) and Turn Wi-Fi off; Enter opens the first, Settings at Wi-Fi; Esc clears. All apps has no Wi-Fi or Bluetooth: they are in Settings. 1 to 7 open the pinned apps. Up from the pinned apps reaches All apps, where typing finds an app. Tab > P pins or unpins the app in focus. Type fast in Notes later to check no keys are dropped.
- [ ] **Keys** ([KEYS.md](KEYS.md)): Fn+S/D change the volume and Fn+U/I the brightness (a message says the new level); in any app Fn+S/D and Fn+U/I do the same, and Fn+Q plays or pauses music. Ctrl + Space opens the taskbar panel, and 1 there turns Wi-Fi off and on. In Settings, 0 shows numbers and 2 opens Bluetooth. Holding Esc goes home from anywhere. In Notes, numbers still type.
- [ ] **New keys** ([UX.md](UX.md)): in Notes, type something, then Ctrl + Space opens the taskbar panel and nothing extra is typed; Ctrl + Space again closes it. Try it both ways: Ctrl held down, and Ctrl tapped (the status bar shows Ctrl) then Space. In Settings > Wi-Fi, open a network's password box and hold Esc for a second: you land on the home screen. A quick Esc there only goes back to the list.
- [ ] **Sticker keys** ([KEYS.md](KEYS.md)): Fn+J (PRSC) says "Screenshot saved"; the PNG in ~/Pictures/Screenshots (open it in Files, or copy it off) shows the screen as it was. Fn+J in the Terminal beeps and saves the console. Fn+H (HELP) shows the keys for the screen. Hold Tab (LOCK) for a second: the screen goes off; the next key only turns it back on, on the same screen. Also note: does Fn+J in the Terminal do anything else there (it shouldn't quit the program running)?
- [ ] **Wi-Fi** (Settings > Wi-Fi): Left/Right on the first row turns Wi-Fi off and on; Esc goes back to Settings. The list of networks shows; the password shows as you type it; Enter joins. The password now reaches NetworkManager through a private file, not nmcli's command line: after joining, restart, and check it reconnects by itself (the password was saved). A wrong password says so and leaves an already-saved network working. Try a WPA3-only network if you have one.
- [ ] **USB Wi-Fi adapter**: plug a USB Wi-Fi dongle into the USB-A port. Settings > Wi-Fi shows an **Adapter** row; choose **USB**, join a network, and the heading says "(USB)". Restart: it reconnects on the dongle. Choose **Built-in**, and its networks are the built-in chip's. Unplug the dongle: the row says "USB (not plugged in)" and Wi-Fi still works. Please note the dongle's chip (`lsusb`) and driver (`readlink /sys/class/net/wlan1/device/driver`) in your report, whether it worked or not.
- [ ] **Date & time** (Settings, row 6, or type "time" on the home screen): with Wi-Fi on, the heading says In sync and the time is right. Time zone: Enter, type the start of your city, Enter; the status bar's clock changes at once. Time: Enter asks before turning Set automatically off, then 4 digits and Enter set it; Date takes 8 digits. Shut down, wait a minute, start with Wi-Fi off: the time you set has kept running (the hardware clock was set too). Turn Set automatically back on and the real time returns once online.
- [ ] **Settings**: brightness and volume change. In Bluetooth, Left/Right on the first row turns it off and on; the paired devices and Scan for devices are in the same list.
- [ ] **Files**: browse ~, open a .txt (opens in Notes once Notes is installed), Esc goes up a folder.
- [ ] **Terminal**: Enter opens a console; type `ls`; Ctrl+Q or `exit` comes back to the home screen.
- [ ] **Update**: says you're on the newest version.
- [ ] **Dock**: with an HDMI monitor plugged in, Start Dock mode shows the desktop; without one it says No monitor.
- [ ] **Apps**: Tab > R refreshes the list; you see the installable apps listed.
- [ ] **First login and SSH**: on a fresh card, `ssh pi@cardputer.local` with `raspberry` is refused (keys only). The Terminal app asks for a new password; after it, `ssh` with the new password works. Ctrl+C at the password prompt on a console ends the login.
- [ ] **Adding a source**: Apps > Tab > P > Add a GitHub repo asks for your password; a wrong one says "Wrong password" and adds nothing; the right one adds it. Installing and removing apps still needs no password.

## Started when needed

New after 0.9.1 and not yet checked on a unit ([PERFORMANCE.md](PERFORMANCE.md#started-when-needed)). Over SSH, `pgrep -l 'pipewire|wireplumber|bluetoothd'` shows what is running.

- [ ] **After boot, Bluetooth off, nothing playing**: two minutes after the home screen appears, `pgrep -l 'pipewire|wireplumber|bluetoothd'` prints nothing. `journalctl -u zeroshell -b | grep stopping` shows why.
- [ ] **First sound**: with PipeWire stopped, play a song in Media. It plays; note how long the start takes. `pgrep pipewire` now finds it. Stop the song: about a minute later PipeWire is gone again.
- [ ] **Volume while stopped**: with PipeWire stopped, Fn+S and Fn+D show the new volume at once, with no pause. Play a song: it plays at that volume. Mute (taskbar panel), play: silent; unmute: back.
- [ ] **Volume survives a restart**: set the volume to 30%, wait a minute, `sudo reboot`. Settings > Sound says 30%, and a song plays at 30%.
- [ ] **Paused music**: pause a song in Media and leave it five minutes. Play: it carries on (PipeWire was not stopped under it).
- [ ] **A game**: in Libretro, pause a game for five minutes, then carry on: it still has sound.
- [ ] **Recorder**: with PipeWire stopped, record a voice note. The start of the recording isn't cut off by more than a second or two.
- [ ] **Bluetooth on**: turn Bluetooth on in Settings. Within a few seconds `pgrep -l 'pipewire|bluetoothd'` shows both. Paired devices are listed; a paired headset or speaker connects and plays.
- [ ] **Bluetooth off**: turn it off (or let "Turn off when unused" do it). A minute later bluetoothd is gone, and PipeWire too if nothing plays. Settings > Bluetooth still opens at once and can be turned on again.
- [ ] **Status bar**: unplug the Wi-Fi router, or walk out of range: the Wi-Fi icon changes within about 5 s. Come back: it shows connected, with the address in System, within about 5 s of joining. Plug in Ethernet: shown within about 5 s.
- [ ] **Idle CPU**: `zeroos-measure` on the home screen, Wi-Fi connected, before and after this update: idle CPU should be clearly lower. `sudo perf top` or `top` should no longer show `nmcli` coming and going.
- [ ] **Car and Tether** (if installed): a phone's music still plays through the Cardputer, and Tether's status page still shows whether Bluetooth is on.

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
- [ ] **DOOM Zero** (M5Stack's store): after installing it, **DOOM (1993)** in its list is no longer MISSING GAME DATA and starts (ZeroOS installs the shareware episode with it). On a device that already had it: after **Apps > Tab > U**.
- [ ] **AdvanceWebUI** (M5Stack's store): it opens and asks for the password, where before it closed at once. `journalctl -u zeroshell -b | grep 'DRM cards swapped'` should show a line; `ls /sys/class/drm` shows which card is the LCD's (`cardN-SPI-1`).
- [ ] **RaspyJack** (M5Stack's store), if you ever installed it: after it is removed (or after updating ZeroOS, if it was removed earlier), no `raspyjack-*` lines in the boot text or in `systemctl list-units 'raspyjack*'`, `/var/swap.raspyjack` is gone and `swapon --show` doesn't list it; after **Apps > Tab > U**, `dpkg -l kismet` says it isn't installed.

## Remove

- [ ] Remove one app in **Apps** (Enter > Remove, then Enter to confirm): it disappears from All apps and `zeroos-apps list` no longer shows it installed.
