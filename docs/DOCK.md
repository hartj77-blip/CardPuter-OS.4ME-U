# Dock mode

Plug the Cardputer Zero into a monitor or TV and it becomes a small desktop
computer: web browser, video player, terminal and file manager on the big
screen, with a USB keyboard and mouse (through a USB hub), or the Cardputer's
own keyboard.

## Using it

1. Install **Dock** from **Apps** once. It brings the desktop: the stock Raspberry Pi desktop
   and Chromium, about 300 MB, so the SD card image stays smaller for people who never dock.
   Removing Dock gives the space back.
2. Plug a monitor into the HDMI port. ZeroShell opens the Dock app.
3. Press **Start Dock mode**. The desktop appears on the monitor after a few
   seconds.
4. The built-in screen shows "Docked" and turns itself off after a minute.
   The Cardputer's keys now type on the monitor (a USB keyboard and mouse
   work too).
5. To leave, open the Raspberry Pi menu (top left) > **Log out** > **Exit to
   command line**, or just unplug the monitor. The built-in screen comes back.
   (Shutdown and Reboot there turn off or restart the whole Cardputer.)

In the Dock app:

- **On plug-in**: Ask (open the Dock app, the default), Start dock
  (automatically), or Do nothing.
- **Size**: 720p (default), 1080p, or the monitor's best mode. 720p is
  smoother and uses less memory; text is still sharp on most monitors.

## On the desktop

It's the Raspberry Pi desktop, as on Raspberry Pi OS: the menu (top left)
has Chromium, the terminal, the file manager and settings; the panel shows
open windows, network, volume, battery and the clock.

| | How |
| --- | --- |
| Terminal | Ctrl+Alt+T, or the menu |
| Web browser (Chromium) | the menu. It's slow on a Pi Zero 2: one tab and simple sites. |
| Files | the menu (File Manager). Double-click a video to play it with mpv (hardware H.264). |
| Leave Dock mode | menu > Log out > Exit to command line, or unplug the monitor |

The Cardputer's keyboard types on the desktop, symbols included (`dock-keys`);
a USB keyboard and mouse work too. Fn + the brightness keys do nothing while
docked: the only backlight is the Cardputer's own screen.

## How it works

| Piece | What |
| --- | --- |
| `zeroos-dock.service` | Runs as `pi` in its own login session on tty2, so it gets the monitor and input devices from logind. It switches to tty2 first (logind only hands them to the session in front) and back to tty1 when it stops. Started and stopped by ZeroShell (polkit allows `pi` exactly this unit). |
| `/usr/lib/zeroos/dock-session` | Finds the DRM card that has the HDMI connector (the vc4 GPU) and starts labwc on it only. The built-in LCD is a separate DRM card that ZeroShell keeps drawing to. |
| The desktop | The stock Raspberry Pi desktop, brought by the Dock app: `labwc-pi` with Raspberry Pi's labwc config, `wf-panel-pi`, `pcmanfm-pi`, the PiXflat theme, and Chromium. Its login screen, boot splash and background services (lightdm, plymouth, PackageKit, NFS, accountsservice) are masked by the zeroos package itself, so they're masked before Dock is ever installed and ZeroShell stays the UI. |
| `/usr/lib/zeroos/dock-keys` (`zeroos-dock-keys.service`) | The Cardputer's keyboard on the desktop. Its symbols come as keycodes of its own (26 for `!`, 71 for `-`) that only the console keymap understands, and M5's image keeps it off the desktop's seat (`seat1`). While docked, dock-keys takes the keyboard for itself and types through a virtual keyboard: each symbol as the standard key with Shift as needed, everything else as it is. Brightness keys are dropped (the only backlight is the LCD's). Stops with Dock mode, and ZeroShell has the keyboard again. |
| `/usr/lib/zeroos/dock-resolution` | Sets the monitor to the Dock app's **Size** (720p by default) when the stock desktop starts. |
| `/usr/lib/zeroos/vt-switch` | Brings tty2 to the front (and tty1 back). Not `chvt`: ZeroShell keeps tty1 in graphics mode, and then the kernel ignores `chvt` and it waits forever. vt-switch answers the switch itself (VT_PROCESS) so nothing is drawn on the LCD. |
| `zeroos-browser` | Chromium for 512 MB: low-end device mode, at most 2 page processes, 32 MB cache in RAM, a 192 MB JavaScript heap limit. |
| `zeroos-video` | mpv with hardware H.264 decoding (V4L2 M2M), a 32 MB stream cache; with no file, opens ~/Videos in the file manager. Plays videos the Jellyfin app sends to the monitor (M), with the sign-in header from `~/.config/zeroos/jellyfin-mpv.conf`. |
| `zeroos-dock` | `start`, `stop`, `status`, `open terminal\|browser\|files\|video FILE`. Works over ssh too. |
| ZeroShell | Watches the HDMI connector (`/sys/class/drm/*-HDMI-A-*/status`), offers or starts Dock mode, stops it 3 s after the monitor is unplugged, and stops reacting to keys while docked. It leaves a monitor that is plugged in alone while the open app drives HDMI itself (Games on a TV), which the app says by writing ZeroShell's process id to `$XDG_RUNTIME_DIR/zeroos-hdmi-app`. |

While docked the CPU never drops below the Balanced profile, even with the
built-in screen off. earlyoom closes Chromium first if memory runs out; the
desktop session itself is next (`OOMScoreAdjust=200`), never ZeroShell.

## Limits and things to measure

- **Memory.** Measured on a Cardputer Zero (2026-11-25): with ZeroOS's own
  desktop and a terminal, about 200 MB stays available; with the stock
  Raspberry Pi desktop, about 155-205 MB. Chromium at 1080p ran out of GPU
  memory ("Skia OOM"); at 720p it loads pages, slowly. Chromium is the big one: expect 150-250 MB with one
  or two tabs, so simple sites and one tab at a time. zram makes room, but
  heavy sites (big web apps, many tabs) are Remote mode territory.
- **Video.** H.264 up to 1080p30 is decoded in hardware; VP9 and H.265 are
  decoded by the CPU and are only smooth at low resolution **(measure)**.
  YouTube in the browser is heavy; a downloaded or streamed H.264 file in
  mpv is much better.
- **Video memory.** M5's image reserves 64 MB of CMA for the GPU
  (`dtoverlay=vc4-kms-v3d,cma-64`). That should cover a 720p desktop plus
  H.264 decoding; 1080p with video may need `cma-96` or `cma-128`
  **(measure)**. CMA is lent back to Linux when the GPU isn't using it.
- **Power.** Docked, the HDMI output and the faster CPU draw more; use USB
  power. On battery the Dock app still works.
- **Video on the built-in screen** (without a monitor) is not done yet.
