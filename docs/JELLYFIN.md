# Jellyfin

The **Jellyfin** app plays music and video (movies, shows, home videos...)
from every library on your [Jellyfin](https://jellyfin.org) server, and looks
like Jellyfin's own apps: the near-black dark theme, the purple-to-blue logo,
Jellyfin blue for what's selected, a home screen of card rows, and a
now-playing bar along the bottom.

It is an app from [CardPuter-Apps.4ME-U](https://github.com/hartj77-blip/CardPuter-Apps.4ME-U/blob/main/apps/jellyfin/README.md): install it
from **Apps** (or `sudo zeroos-apps install jellyfin`).

It used to be the Jellyfin tab of [Media](https://github.com/hartj77-blip/CardPuter-Apps.4ME-U/blob/main/apps/media/README.md) (Media 2.x).
Media 3.0 is for your own files (the SD card, USB drives, server shares), so a
device that updates Media needs the Jellyfin app installed to keep Jellyfin. If
you signed in through Media, you're still signed in here.

## Sign in

Open **Jellyfin** and press **Enter** on **Sign in**. Three questions, as in
Jellyfin's own apps:

- **Server address**: its name or address. `nas` or `192.168.1.20` means
  `http://nas:8096` (Jellyfin's usual port); type the whole address, such as
  `https://music.example.com`, for anything else.
- **User** and **Password**: the ones you use in Jellyfin's own apps.

The password is used once and not kept. The server gives back a sign-in token,
which is saved in `~/.config/zeroos/settings.conf` (readable only by you).
**Sign out** (in the menu, **Tab**) forgets it; you can also remove "Cardputer
Zero" under Devices in Jellyfin's dashboard.

## Home

```
 [=] jellyfin                                    Q (J)
 My Media
 +--------+ +--------+ +--------+ +--------+
 |   ##   | |   ##   | |   []   | |   []   |
 | Music  | | Vinyl  | | Movies | | Kids   |
 +--------+ +--------+ +--------+ +--------+
 Continue Watching
 +-------------------+ +-------------------+
 | Bench Talk        | | Sintel            |
 | S2E2 Reading      | | 2010  38m left    |
 |=======------------| |==============-----|
 +-------------------+ +-------------------+
 Music
   Latest   Albums   Artists   Playlists
 > Low Battery Blues - Bench Tapes           0:42 / 4:03
```

- **My Media**: every library on your server, by the names you gave them
  (two music libraries, Kids movies, Home videos, Music videos, Audiobooks,
  Collections...). On ZeroOS 0.4.0 and older you get fixed **Movies**,
  **Shows** and **Music** cards instead.
- **Continue Watching**: what you started on any device. Enter carries on.
- **Music**: Latest, Albums, Artists and Playlists across all your music.

| A library of | Opens as |
| --- | --- |
| Music | its albums |
| Movies | its movies |
| Shows | its shows, then seasons, then episodes |
| Anything else (home videos, music videos, books, mixed, collections) | its folders and files, folders first |

Live TV libraries aren't listed: there's no tuner support.

## Keys

| Key | Does |
| --- | --- |
| Arrows | Home: move between cards and rows. Lists: Up / Down |
| Enter | Open, or play |
| Esc or Left | Back (in a list) |
| / | Search the whole server: tracks, albums, artists, playlists, movies, shows, episodes, music videos, audiobooks |
| Space or Fn + Q | Pause / resume the music |
| N / P, Fn + E / Fn + W | Next / previous track |
| + / - | Volume |
| Tab | The menu: search, stop the music, home, refresh, sign out |

## Playing music

Playing a track queues the rest of the album or playlist after it. The playing
track is marked in blue with a speaker, and the bar along the bottom shows it
with its time and a blue progress line. The queue belongs to ZeroShell, not
the app, so music carries on to the next track after you leave the app and
while the screen is off. Wi-Fi stays on while something is playing. The
media keys (Fn + Q, W, E) and a Bluetooth headset's or car's buttons work
everywhere.

Tracks in mp3, flac, ogg, opus, m4a or wav play as they are; the server
converts anything else to mp3. Long lists come 200 at a time: **More...** at
the bottom fetches the next 200.

## Watching video

Pick a movie, or a show, then a season, then an episode, or a card under **Continue Watching**. It plays across the
whole app area (16:9 fills 267 x 150 pixels), with the sound on the speaker,
the headphone jack or Bluetooth, whichever music would use.

| Key | Does |
| --- | --- |
| Space, Enter or Fn + Q | Pause / resume |
| Left / Right | Back / forward 10 seconds |
| Fn + W / Fn + E | Back 10 / forward 30 seconds |
| Down / Up | Back / forward a minute |
| 0 | From the start |
| + / - | Volume |
| M | Play it on the monitor instead (Dock mode must be running) |
| Esc or S | Stop |

Any key shows the title, time and progress for three seconds; they stay up
while paused or loading.

- **Continue Watching** on the home screen lists what you started on any
  device, with a blue bar for how far you got, and picks up where you
  stopped. **0** goes back to the start. In lists, movies and episodes you
  left part-way say how much is left, e.g. `38m left`.
- The Cardputer tells the server what it is playing: it shows in Jellyfin's
  dashboard, and the place you stopped is saved for every device.
- An episode that plays to the end goes on to the next one in the season.
- A Bluetooth headset's or car's play and skip buttons pause and skip the
  video too (ZeroOS newer than 0.4.0), instead of starting your music over it.
- The screen stays on while a video plays (not while paused). Turning the
  Cardputer face down, or the screen turning off, pauses it.
- Video belongs to the Jellyfin app, unlike music: leaving the app stops it.
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

- No album art or posters (cards show an icon instead), and no subtitle or audio track choice (the
  server's defaults are used).
- No photos, e-books or Live TV.
- Video needs Wi-Fi that keeps up with about 1 Mbit/s.
- Names in scripts the built-in font lacks (Chinese, Japanese, Cyrillic...)
  show as boxes.
- Asking the server for a list freezes the screen for a moment, up to 12
  seconds if the server doesn't answer.
- Not yet tried against a real server or on the device. The requests follow
  Jellyfin's published API description (12.4). Video playback is tested
  with the real player (libmpv) against a stand-in server that serves an HLS
  stream and checks every request's sign-in header.
