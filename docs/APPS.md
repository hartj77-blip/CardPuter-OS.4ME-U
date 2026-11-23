# Apps

ZeroOS itself is small: it comes with **Apps**, **Settings**, **Update** and **Wi-Fi**, which are
what a device needs to get online, update itself and install apps. Everything else is an app you
install from the **Apps** app, downloaded from the apps repository,
[CardPuter-Apps.4ME-U](https://github.com/hartj77-blip/CardPuter-Apps.4ME-U): Battery, Camera, Car,
Clock, Compass, Dock, Files, GPS, IR Remote, Media, Mesh, Notes, Remote, Sync, System, Terminal,
Tether and AI, the [Dev Tools](DEV.md) pack (Code, REPL, Git and Serial) and [Games](GAMES.md).

Terminal, Files and Dock were built in up to ZeroOS 0.1.x. The release SD card image still comes
with them installed. A device updated from 0.1.x gets them back the first time you open **Apps**
and press **U** (which that update needs anyway, for the new app ABI). Their system parts stay
in ZeroOS: the desktop on the monitor (`zeroos-dock`), which Files, Camera, Mesh and Remote also
use, and the console service behind Terminal. Remote's SSH and mosh need the Terminal app.

Apps installed under 0.1.x were built for the old app ABI and can't run on a newer ZeroOS. The
first install or **U** after the update takes those old builds off (your files and settings stay)
and **U** installs the new builds. It then clears extras that only the old builds needed. Before ZeroOS 0.3.1 the old builds were left on and every
install failed with "Could not install"; if you see that, update ZeroOS first, or run
`sudo apt-get -f install -y` over SSH once.

Each app brings its own extras with it, so the base image doesn't carry them: Remote brings
Tailscale, Sync brings Syncthing, Camera brings libcamera and M5Stack's camera program, Mesh brings
meshtasticd, GPS brings gpsd. Removing an app takes its extras away again, unless another app still
needs them.

## Using it

Open **Apps** (it's in the dock). It looks for new apps when its list is more than 6 hours old.

| Key | |
| --- | --- |
| Enter | an app's page: **Install**, **Update**, **Open**, **Remove** |
| U | update every app that has a newer version |
| R | look for new apps now |
| S | sources: where apps come from. **D** removes one; **Add a GitHub repo** adds one |

Installing needs a connection (Wi-Fi, Ethernet or a tethered phone). A new app appears on the home
screen as soon as it's installed. After an update to an app you already opened, the screen
restarts once (about a second) to run the new version.

From a terminal:

```sh
zeroos-apps list                 # what's on offer and what's installed
sudo zeroos-apps install media
sudo zeroos-apps remove media
sudo zeroos-apps upgrade         # every app with a newer version
sudo zeroos-apps add-source someone/cardputer-apps
sudo zeroos-apps install-file zeroos-app-media_0.5.0_arm64.deb   # a package you copied over
```

An SD card image can have apps on it from the start: the **Build image** workflow takes a list
(for example `notes media gps`), as does `ZEROOS_APPS="notes media gps" image/build.sh`.

## Where apps come from

A **source** is a GitHub repository whose newest release holds a file called `zeroos-apps.index`
and the packages it lists. ZeroOS starts with two, in this order:

| Source | What it is |
| --- | --- |
| [`hartj77-blip/CardPuter-Apps.4ME-U`](https://github.com/hartj77-blip/CardPuter-Apps.4ME-U) | The apps' own repository. Each app is developed and released there, with its own version. |
| `hartj77-blip/CardPuter-OS.4ME-U` | This repository. Its releases still carry the apps built from the same commit as the OS, as a fallback while the move is under way. |

When two sources offer an app with the same name, the one higher in the list wins, so apps come
from the apps repository whenever it has a build for this ZeroOS. A device updated from ZeroOS
0.3.4 or older gets the apps repository added to its list by the update. The first **U** after
that updates every installed app once, to the apps repository's builds.

Any other repository that publishes the same files works too: add it in
**Apps > S > Add a GitHub repo**, or add a line to `/etc/zeroos/apps.sources` (an `owner/repo`,
or any `https://` address that serves the index).

**Trust.** An app runs with the same access as the home screen, and installing one runs its
package's scripts as root. The SHA-256 in the index only proves the download is complete; the
index comes from the same place. Only add sources you trust.

## How it works

```
Apps app (ZeroShell)  --systemctl start-->  zeroos-apps@install:media.service  (root)
        ^                                         |  1. read the catalog (from every source's index)
        |  /run/zeroos/apps.status                |  2. download zeroos-app-media_<v>_arm64.deb, check SHA-256
        +-----------------------------------------+  3. apt-get install it, with what it depends on
                                                     4. ZeroShell loads /usr/lib/zeroos/apps/media.so
```

- **An app is a module**: a shared library, `/usr/lib/zeroos/apps/<id>.so`, that ZeroShell loads
  at start and after each install. It exports `zs_app_module()` (see `shell/src/ui/app.h`) and uses
  LVGL and the hardware layer from the zeroshell binary, which exports them. ZeroShell loads a
  module only if it was built for the same **app ABI** (`ZS_APP_ABI` in `app.h`;
  `zeroshell --app-abi`). The store only offers matching builds, and the `zeroos` package
  `Provides: zeroos-app-abi-<n>`, which every app package depends on.
- **An app is a Debian package**, `zeroos-app-<id>`: the module, the app's own files (services,
  scripts, settings) and `Depends:` on the Debian packages it needs. Apps that need a Debian
  repository of their own (Remote: Tailscale; Mesh: Meshtastic) carry its list file and key;
  `zeroos-apps` puts those in place before asking apt for the rest.
- **zeroos-apps** (`image/stage-zeroos/files/apps/zeroos-apps`) does the work as root, started
  through `zeroos-apps@<job>.service` (polkit lets the UI user start those). It writes the merged
  catalog to `/var/lib/zeroos/apps/catalog` and progress to `/run/zeroos/apps.status`.

## Making an app

Apps are made in the apps repository,
[CardPuter-Apps.4ME-U](https://github.com/hartj77-blip/CardPuter-Apps.4ME-U): one folder per app,
with its code, its package description and its docs. Its README shows how to add or change one.
It builds against this repository (pinned there as `base/`), using three things from here:

| From this repository | What it is |
| --- | --- |
| `shell/src/` headers, `ZS_APP_ABI` in `shell/src/ui/app.h` | What an app is compiled against, and the app level |
| `shell/cmake/ZeroShellApps.cmake` | `zs_app()` and `zs_apps_build()`, which turn an app into a module |
| `image/apps/make-app-deb.sh` | Packs a module into `zeroos-app-<id>.deb` and prints its block for the index |

A screen says what its keys do with `shell_set_actions()` (`ui/app.h`): the shell then draws the
soft-key bar, the Tab menu and Ctrl + letter shortcuts from that one list. See [UX.md](UX.md).

New apps go there, not here. The apps still in `shell/src/apps/` and `image/apps/` are the
fallback copies; they will be removed once a device has installed from the apps repository.
Don't change an app in both places: change it in the apps repository.

### The app level

Raise `ZS_APP_ABI` in the same pull request as any change that breaks already-built modules: a
header an app includes (`ui/`, `hal/`, `apps/common.h`), LVGL or `lv_conf.h`. ZeroShell skips
modules built for another level and the store doesn't offer them.

The apps repository watches this one. It builds every app for the level of the newest ZeroOS
release, and for the level on `main` when that is different, so the apps for the next ZeroOS are
published before it is released. Before releasing a ZeroOS that raises the level, check that the
apps repository's [newest release](https://github.com/hartj77-blip/CardPuter-Apps.4ME-U/releases/latest)
lists builds for it (its notes say which levels it was built for). If it doesn't yet, run its
Release workflow by hand.

## What's not done yet

- Nothing here has run on a Cardputer yet: the installer is tested against stand-ins for apt and
  GitHub (`image/tests/apps-test.sh`), and the modules in a headless ZeroShell on a PC.
- A ZeroOS update doesn't update apps by itself; open **Apps** and press **U**.
- Removing Terminal also removes one-key SSH from Remote (Remote says so); removing Dock leaves
  Dock mode reachable through Settings' "On HDMI" option.
- The store shows an app's real icon only once it's installed (until then, a grid).
- An app's page doesn't show screenshots.
