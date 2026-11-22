# Jellyfin

The **Media** app plays music and video (movies and TV shows) from a Jellyfin
server, as well as music from `~/Music`.

## Sign in

1. Open **Media** and press **Right** twice for the **Jellyfin** tab.
2. Press **Enter** on **Sign in...** and answer three questions:
   - **Server**: its name or address. `nas` or `192.168.1.20` means
     `http://nas:8096` (Jellyfin's usual port); type the whole address, such as
     `https://music.example.com`, for anything else.
   - **User name** and **password**: the ones you use in Jellyfin's own apps.

The password is used once and not kept. The server gives back a sign-in token,
which is saved in `~/.config/zeroos/settings.conf` (readable only by you).
**Sign out** forgets it; you can also remove "Cardputer Zero" under Devices in
Jellyfin's dashboard.

The Jellyfin tab's first list: **Search**, **Continue watching**, **Movies**,
**TV shows**, then the music lists (**Latest albums**, **Albums**, **Artists**,
**Playlists**) and **Sign out**.

## Playing music

| Key | Does |
| --- | --- |
| Enter | Open an album, artist or playlist; play a track |
| Esc or ` | Back one level |
| / | Search tracks, albums and artists |
| Space | Pause / resume |
| N / P | Next / previous track |
| S | Stop |
| + / - | Volume |

Playing a track queues the rest of the album or playlist after it. The queue
belongs to ZeroShell, not the Media app, so music carries on to the next track
after you leave the app and while the screen is off. Wi-Fi stays on while
something is playing.

Tracks in mp3, flac, ogg, opus, m4a or wav play as they are; the server
converts anything else to mp3. Lists show the first 200 entries, so use search
in a big library.

## Watching video

Pick a movie, or a show, then a season, then an episode. It plays across the
whole app area (16:9 fills 267 x 150 pixels), with the sound on the speaker,
the headphone jack or Bluetooth, whichever music would use.

| Key | Does |
| --- | --- |
| Space or Enter | Pause / resume |
| Left / Right | Back / forward 10 seconds |
| Down / Up | Back / forward a minute |
| 0 | From the start |
| + / - | Volume |
| M | Play it on the monitor instead (Dock mode must be running) |
| Esc or S | Stop |

Any key shows the title, time and progress for three seconds; they stay up
while paused or loading.

- **Continue watching** lists what you started on any device (Jellyfin's own
  "Continue watching"), and picks up where you stopped. **0** goes back to the
  start. Movies and episodes you left part-way show the time, e.g. `@36:50`.
- The Cardputer tells the server what it is playing: it shows in Jellyfin's
  dashboard, and the place you stopped is saved for every device.
- An episode that plays to the end goes on to the next one in the season.
- The screen stays on while a video plays (not while paused). Turning the
  Cardputer face down, or the screen turning off, pauses it.
- Video belongs to the Media app, unlike music: leaving the app stops it.
- **M** hands the video to the monitor's player in 720p, from the same place.

### What the server sends

The server converts each video to fit the Cardputer: H.264 and AAC, at most
427 x 240 and about 700 kbit/s (720p and 4 Mbit/s for the monitor), in short
HLS segments, so seeking works. A file that already fits is passed through.
The server needs transcoding enabled for the user (on by default), and a
server that has to convert a 4K film may need a moment before it starts.

On the Cardputer, mpv decodes the stream (libmpv, which comes with mpv) and
draws each picture into ZeroShell's own screen. The sign-in token travels in
the Authorization header of every request, never in an address: Jellyfin 12
refuses tokens in addresses unless "legacy authorization" is turned on. For
the monitor, the header goes to the Dock's player in a file only you can read,
`~/.config/zeroos/jellyfin-mpv.conf`.

## Away from home

With Tailscale on both the Cardputer and the server (see [REMOTE.md](REMOTE.md)),
sign in with the server's Tailscale name, e.g. `homelab`, and it works from any
network. Set **Settings > Tailscale** to **Always on** so it stays connected.

## Limits

- No album art or posters, and no subtitle or audio track choice (the
  server's defaults are used).
- Video needs Wi-Fi that keeps up with about 1 Mbit/s.
- Names in scripts the built-in font lacks (Chinese, Japanese, Cyrillic...)
  show as boxes.
- Asking the server for a list freezes the screen for a moment, up to 12
  seconds if the server doesn't answer.
- Not yet tried against a real server or on the device. The requests follow
  Jellyfin's published API description (12.4). Video playback is tested
  with the real player (libmpv) against a stand-in server that serves an HLS
  stream and checks every request's sign-in header.
