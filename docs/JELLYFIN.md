# Jellyfin

The **Jellyfin** app plays music and video (movies, shows, home videos...)
from every library on your [Jellyfin](https://jellyfin.org) server, and looks
and works like Jellyfin's own apps: the near-black dark theme, the
purple-to-blue logo, cover art everywhere, a home screen of card rows (My
Media, Continue Watching, Next Up, what is new in each library), poster
grids, a page for each album, film and episode, a search that finds as you
type, and a now-playing bar along the bottom.

It is an app from [CardPuter-Apps.4ME-U](https://github.com/hartj77-blip/CardPuter-Apps.4ME-U/blob/main/apps/jellyfin/README.md): install it
from **Apps** (or `sudo zeroos-apps install jellyfin`). **Its own guide is
[there](https://github.com/hartj77-blip/CardPuter-Apps.4ME-U/blob/main/apps/jellyfin/README.md)**, with every screen and key; this page keeps what ZeroOS
itself does for it.

It used to be the Jellyfin tab of [Media](https://github.com/hartj77-blip/CardPuter-Apps.4ME-U/blob/main/apps/media/README.md) (Media 2.x).
Media 3.0 is for your own files (the SD card, USB drives, server shares), so a
device that updates Media needs the Jellyfin app installed to keep Jellyfin. If
you signed in through Media, you're still signed in here.

## Sign in

Open **Jellyfin** and press **Enter** on **Sign in**. Three questions, as in
Jellyfin's own apps:

- **Server address**: its name or address. For a bare name such as `nas` or
  `192.168.1.20`, the Cardputer looks for the server over **https** first
  (`https://nas`, then `https://nas:8920`, Jellyfin's https port) and only
  then over plain **http** on Jellyfin's usual port, `http://nas:8096`. Type
  the whole address, such as `https://music.example.com`, for anything else;
  an address you type with `http://` is used as it is. Before the password is
  sent, the Cardputer asks the server for its public info (nothing secret), so
  a server that moves http to https on the same host is signed in to over https.
- **User** and **Password**: the ones you use in Jellyfin's own apps.

**Unencrypted servers.** Over plain http, the password, the sign-in token and
what you play cross the network as they are, readable by anyone on it. The
password question says *sent unencrypted* when you typed `http://`, and
signing in says *not encrypted (http)* when the server only answered over
http. That is fine on your own home network or over Tailscale (which encrypts
everything, see below); on other networks, give the server https (a reverse
proxy, or Jellyfin's own https setting) and type its `https://` address. A
server whose https certificate isn't trusted (self-signed) is refused over
https: use its http address on a network you trust instead.

Requests don't follow redirects to other addresses, so the token and password
only ever go to the server you signed in to; a sign-in the server redirects
says *The server moved; type its full address*. The token is handed to curl
and mpv in files only you can read, never on a command line other programs
can see.

The password is used once and not kept. The server gives back a sign-in token,
which is saved in `~/.config/zeroos/settings.conf` (readable only by you).
**Sign out** (in the menu, **Tab**) forgets it; you can also remove "Cardputer
Zero" under Devices in Jellyfin's dashboard.

## What ZeroOS does, and what the app does

| ZeroOS (`shell/src/hal/jellyfin.h`, the hardware layer) | The app |
| --- | --- |
| Signs in: finds the server (https first), sends the password once, keeps only the token | Everything on the screen: home, library grids, album and film pages, search, the player |
| Builds the addresses music and video stream from | The lists, searches and cover art, fetched in the background with curl ([how](https://github.com/hartj77-blip/CardPuter-Apps.4ME-U/blob/main/apps/jellyfin/README.md#how-it-is-built-and-tested)) |
| Plays music through ZeroShell's queue, so it carries on with the app closed | Keeping covers in `~/.cache/zeroos/jellyfin` |
| Plays video with libmpv into ZeroShell's own screen | The video's keys and its info bar |
| Tells the server what is playing, so the place you stopped is saved for every device | Watched and favourite marks |

The app asks the server for its lists itself, so they are the same on every
ZeroOS it installs on. The older calls for them (`jellyfin_list` in `hal.h`)
are still here: Jellyfin 1.x uses them, and the simulator's pretend server
answers through them.

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

Tracks in mp3, flac, ogg, opus, m4a or wav play as they are; the server
converts anything else to mp3.

## Away from home

With Tailscale on both the Cardputer and the server (see [REMOTE.md](REMOTE.md)),
sign in with the server's Tailscale name, e.g. `homelab`, and it works from any
network. Set **Settings > Tailscale** to **Always on** so it stays connected.

## Limits

See [the app's guide](https://github.com/hartj77-blip/CardPuter-Apps.4ME-U/blob/main/apps/jellyfin/README.md#limits). In short: no subtitle or audio track
choice, no photos, e-books or Live TV, and it has not yet been tried against
a real Jellyfin server or on the device (its requests are tested against a
stand-in server that answers in the shapes of Jellyfin's API description).
